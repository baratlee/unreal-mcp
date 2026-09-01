# MCP 传输生命周期与只读批处理优化（2026-09-01）

## 概述

本次变更修复 UnrealMCP TCP 传输与服务关闭流程中的正确性问题，并在不显著增加模型 token 消耗的前提下，降低连接轮询、分片收包、多次只读查询和大体积蓝图响应的耗时。

## 正确性修复

- C++ 服务端按完整请求累积 TCP 分片，不再假定一次 `Recv` 即得到完整 JSON。
- 修复接收缓冲区满载时写入字符串终止符可能越界的问题。
- 请求与聚合响应均设置 4 MB 上限；单请求输入设置 1 MB 上限，超限时返回明确错误。
- 服务线程、监听 Socket、客户端 Socket 与 `FRunnable` 的所有权改为显式管理，避免停止或重启时遗留线程和 Socket。
- 停止状态使用原子变量，并通过弱 UObject 引用、完成事件和限时 Future 轮询避免关闭期间死锁或访问已销毁对象。
- 删除未被现行协议使用的旧换行分帧处理路径，保持单连接单请求语义。

## 传输与日志优化

- 监听端使用 `WaitForPendingConnection` 等待连接，替代固定周期轮询。
- 客户端收发使用 Socket `Wait`，减少 `WouldBlock` 后的短间隔睡眠。
- C++ 服务端发送一个完整响应后关闭客户端连接；Python 端以 EOF 作为正常响应结束标记。
- Python 端使用 `bytearray` 聚合 64 KB 分片，在响应完成后只进行一次 UTF-8 解码和 JSON 解析。
- 保留旧版服务端无显式结束标记时的超时兼容路径，便于插件与 Python 端分阶段更新。
- 请求 JSON 使用紧凑分隔符编码；日志改为延迟打开的轮转文件（4 MB，保留 2 份）。默认不记录完整请求与响应正文，仅在 `UNREAL_MCP_LOG_PAYLOADS=1` 时启用。
- 增加连接、发送、接收和解析阶段耗时日志，便于区分编辑器主线程、网络传输与 Python 解析耗时。

## `batch_read` 只读批处理

- 新增 `batch_read` 工具，一次请求可执行 1–8 个只读操作。
- 仅允许显式白名单内的查询命令；禁止嵌套批处理和写操作。
- 操作在 GameThread 上按顺序派发，保持现有 Unreal 对象访问约束，不引入并行访问 UObject 的风险。
- 批次总时限为 10 秒，在相邻操作之间检查；聚合响应上限为 4 MB。
- 每个子操作独立返回 `id`、`status`、`result` 或 `error`，单项失败不会丢失其他已完成结果。
- 对蓝图与动画图等高体积查询，在调用方未显式指定时自动补充 summary/compact 参数，避免批处理抵消往返次数收益并显著增加 token。

## Blueprint 输出契约

- Python `get_blueprint_info` 默认使用 `output_profile="summary"`，减少组件可编辑属性、变量默认值和事件图节点详情等大体积内容。
- C++ 命令在缺少 `output_profile` 时仍默认 `full`，保留旧调用方的直接协议兼容性。
- 调用方需要完整细节时可显式传入 `output_profile="full"`。

## 兼容性与验证边界

- 新 Python 端可连接尚未更新的旧版插件；旧插件不关闭连接时会在兼容超时后解析已接收的完整响应。
- 新协议效果需要重新编译并重启 Unreal Editor 中的插件，再重启 Python MCP 服务进程。
- 已通过 Python 传输测试，覆盖大响应分片、EOF 完成、旧协议超时兼容、响应大小限制以及紧凑单请求发送，共 5 项。
- 已完成 Python AST、模块导入和工具 Schema 检查。
- 本次未执行 C++ 编译、Editor 启动或真实编辑器联调，相关运行验证仍需在插件重编译后完成。

## 修改文件

插件：

- `Source/UnrealMCP/Public/MCPServerRunnable.h`
- `Source/UnrealMCP/Private/MCPServerRunnable.cpp`
- `Source/UnrealMCP/Public/UnrealMCPBridge.h`
- `Source/UnrealMCP/Private/UnrealMCPBridge.cpp`
- `Source/UnrealMCP/Private/Commands/UnrealMCPBlueprintCommands.cpp`

Python MCP 服务（仓库根目录）：

- `Python/unreal_mcp_server.py`
- `Python/tools/project_tools.py`
- `Python/tools/blueprint_tools.py`
- `Python/tools/editor_tools.py`
- `Python/test_transport.py`
