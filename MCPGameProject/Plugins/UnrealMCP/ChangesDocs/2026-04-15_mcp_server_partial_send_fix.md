# MCPServerRunnable: 修复 Send() partial-send bug 与 UTF-8 字节长度错误

**日期**：2026-04-15
**涉及仓库**：`C:\Workspace\git\LeoPractice`（UnrealMCP 插件）

## 问题现象

加完 `pin_payload_mode=summary/names_only` 之后，`get_blueprint_function_graph` 读 `SandboxCharacter_Mover_ABP::AnimGraph` 仍然在三种模式下全部超时。诊断对比：

- 控件函数 `Update_CVarDrivenVariables`（29 节点）→ 三种模式都成功，30044 字节响应，Python 收 7 个 chunk 后拼出完整 JSON
- AnimGraph（23 节点）→ UE 端日志 `Response sent successfully, bytes: 22710`，Python 端只收到 6 个 partial chunk，5 秒后超时报 `Timeout receiving Unreal response`

UE 端服务侧序列化总耗时 197ms（`Received` → `Sending response`），Send 调用 1ms 内返回成功，1ms 后客户端断开——证明瓶颈不在序列化、也不在工具逻辑，而在**socket 发送路径**。

## 根因

`Plugins/UnrealMCP/Source/UnrealMCP/Private/MCPServerRunnable.cpp` 两处 Send 调用都有同样的 bug：

```cpp
ClientSocket->Send((uint8*)TCHAR_TO_UTF8(*Response), Response.Len(), BytesSent)
```

两个独立缺陷：

1. **字节长度错误**：`Response.Len()` 是 TCHAR（Windows 上 UTF-16 wchar_t）数量，不是 UTF-8 字节数。如果响应里出现任何非 ASCII 字符，UTF-8 字节数会大于 TCHAR 数，传给 Send 的 length 偏小，缓冲区被截断。
2. **没有 partial-send 循环**：socket 在非阻塞模式下，`Send()` 可能返回 `true` 但 `BytesSent < requested`。代码只跑一次就走 success 分支，剩余字节直接丢失。日志里 `bytes: 22710` 实际是 `BytesSent` 的值——并不能证明这就是 `Response` 的全长。

两种缺陷都会让 Python 端收到截断/不完整的 JSON，`json.loads` 永远拼不出合法对象，5 秒后 socket timeout。在小响应（< 4KB）下两个 bug 都不容易触发，所以之前 9 个动画工具都能正常工作，AnimGraph 是第一个稳定踩中的场景。

## 修复

`MCPServerRunnable.cpp` 两处 Send（`Run` 主循环 line 94 附近 + `ProcessMessage` line 317 附近）改成同样的模式：

```cpp
FTCHARToUTF8 Utf8Converter(*Response);
const uint8* Utf8Data = (const uint8*)Utf8Converter.Get();
int32 TotalBytes = Utf8Converter.Length();   // 真实 UTF-8 字节数
int32 TotalSent = 0;
while (TotalSent < TotalBytes)
{
    int32 BytesSent = 0;
    if (!ClientSocket->Send(Utf8Data + TotalSent, TotalBytes - TotalSent, BytesSent))
    {
        // log + bail
        break;
    }
    if (BytesSent <= 0) { FPlatformProcess::Sleep(0.001f); continue; }
    TotalSent += BytesSent;
}
```

**关键点**：

- `FTCHARToUTF8::Length()` 返回的是真实 UTF-8 字节数，不含尾部 `\0`
- 循环条件用 `TotalSent < TotalBytes`，每次 Send 只发剩余部分
- `BytesSent == 0` 时短暂 sleep 1ms 避免空转
- 日志里同时打 `TCHARLen` 和 `UTF8Bytes` 和 `Sent`，回归对比能直接看出哪些响应有非 ASCII

## 验证步骤（重启后跑）

1. 关 UE → 重编 UnrealMCP 插件 → 启 UE
2. **回归测试**：对 `Update_CVarDrivenVariables`（29 节点）三种模式各跑一遍，确认仍能成功，日志里出现新格式的 `Response sent successfully (TCHARLen=... UTF8Bytes=... Sent=...)`
3. **AnimGraph 主验证**：对 `SandboxCharacter_Mover_ABP::AnimGraph` 跑 `pin_payload_mode="summary"`，期望：
   - 不再超时
   - UE 日志里 `Sent` 等于 `UTF8Bytes`
   - 如果 `UTF8Bytes != TCHARLen`，证明命中了非 ASCII 路径（可能就是 BlendStack/PoseSearch 节点 InstancedStruct 里的特殊字符）
4. **AnimGraph names_only 验证**：同上但 `names_only`，期望响应更小、依然成功
5. **AnimGraph full 验证**：同上但 `full`——这个原本是 timeout 最严重的，期望也能成功（响应会很大，但循环 send 应该能搞定）

## 与 pin_payload_mode 改动的关系

`pin_payload_mode` 改动**仍然有意义**：
- 它把 AnimGraph 响应从可能几十 KB 压到十几 KB，对 Python 客户端、context window、网络都更友好
- 但**它本身不是 root cause**——partial-send bug 才是。即使把 `pin_payload_mode` 撤掉，单独修这个 bug，AnimGraph 也应该能 work（响应可能 100KB+，但循环 send 能写完）

两个改动正交、互补，都保留。
