# unreal-mcp 扩展：get_blueprint_function_graph 增加 pin_payload_mode 参数

**日期**：2026-04-15
**涉及仓库**：
- `C:\Workspace\git\LeoPractice`（UnrealMCP 插件）
- `C:\Workspace\git\unreal-mcp`（Python MCP server）

## 修改目的

`get_blueprint_function_graph` 在读 AnimGraph 这类节点时会稳定超时。直接原因是 AnimGraph 上每个 anim node 的 pin `DefaultValue` 携带 InstancedStruct 文本（BlendStack 输入、Pose Search 配置、Foot Placement 设置等），单个 pin 经常几 KB，23 节点的 AnimGraph 序列化出来的 JSON 远超 MCP socket 响应窗口。

读 GASP 项目 `SandboxCharacter_Mover_ABP` 的 AnimGraph 时三次连续超时（同一工具读 6 节点的 `Update_Logic` 即时返回），证明瓶颈是单次响应体积而非工具本身。

新增能力：

| 模式 | 行为 |
|---|---|
| `full`（默认） | 完整 `default_value`，与原行为一致，向后兼容 |
| `summary` | `default_value.Len() > 256` 时改输出 `default_value_len` / `default_value_preview`（前 96 字符）/ `default_value_truncated=true`，短值仍按原样输出 |
| `names_only` | 完全丢弃 `default_value`，只保留 `name` / `direction` / `type` / `sub_type` / `links` |

`get_blueprint_info` 路径不变（仍走 `EPinPayloadMode::Full` 默认参数），不影响既有调用者。

## 修改文件清单（共 2 个）

### 1. `Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPBlueprintCommands.cpp`

- 匿名 namespace 顶部新增 `enum class EPinPayloadMode : uint8 { Full, Summary, NamesOnly }` 与 `ParsePinPayloadMode(const FString&)`，以及两个 `constexpr` 常量 `GPinSummaryThreshold = 256` / `GPinSummaryPreviewLen = 96`
- `SerializeGraphNodeToJson` 签名从 `(UEdGraphNode*)` 改为 `(UEdGraphNode*, EPinPayloadMode Mode = EPinPayloadMode::Full)`，pin 序列化里 `DefaultValue` 分支替换成 switch：
  - `Full` 走原 `SetStringField("default_value", ...)`
  - `Summary` 在长度 ≤256 时按原样输出，否则写 `default_value_len` / `default_value_preview` / `default_value_truncated`
  - `NamesOnly` 完全跳过 `DefaultValue` 字段
- `HandleGetBlueprintFunctionGraph` 解析 `pin_payload_mode` 字符串参数（缺失或未识别一律落回 `Full`），把解析结果传给 `SerializeGraphNodeToJson`
- 头文件 `UnrealMCPBlueprintCommands.h` **不用动**（handler 声明已经存在）

### 2. `C:/Workspace/git/unreal-mcp/Python/tools/blueprint_tools.py`

- `get_blueprint_function_graph` 函数签名追加 `pin_payload_mode: str = "full"` 参数
- docstring 说明三种模式与适用场景（重点指出 AnimGraph 必须用 `summary` 或 `names_only`）
- `send_command` 的 payload 字典新增 `"pin_payload_mode": pin_payload_mode` 字段

## 关键技术细节

- `EPinPayloadMode` 放在匿名 namespace 内随 `SerializeGraphNodeToJson` 一起隐藏，外部翻译单元看不到，避免和未来其他 mode 枚举冲突
- `ParsePinPayloadMode` 用 `Equals(..., ESearchCase::IgnoreCase)`，对前端大小写不敏感；非法值回落到 `Full` 而不是报错——保持向后兼容（旧 Python 客户端不传参数也能工作）
- 阈值 256 / 预览长度 96 的取值：96 字符足以包含 `(BoneName="root",Position=(X=0,Y=0,Z=0)` 这类 InstancedStruct 字面量的可读前缀；256 把"普通字面量字符串"保留原样、只截 InstancedStruct 那种长 payload
- `default_value_preview` 用 `FString::Left()` 取前 N 字符——FString 是 wide char (UCS-2)，不会切到 UTF-8 多字节中间
- 没改 `get_blueprint_info` 路径，因为它本来就只列举 `function_graphs` 名字 + 节点数（不展开 pin），不存在体积问题；但同一个 `SerializeGraphNodeToJson` 也会被 `event_graphs` 路径用到，那里默认 `Full` 模式不变
- C++ 端只新增了一个 `Params->TryGetStringField` 调用，零新依赖，零新 include

## 端到端验证步骤（编译重启后跑）

1. 关 UE → 重编 UnrealMCP 插件 → 启 UE → `/exit` 重启 Claude Code（让 Python MCP 重新注册带新参数的 tool schema）
2. **回归测试**：对一个小函数（例如 `Update_Logic`，6 节点）用默认 `full` 模式跑一次，确认输出与本次改动前一致——特别是 pin 的 `default_value` 字段还在
3. **summary 模式主验证**：对 `SandboxCharacter_Mover_ABP` 的 `AnimGraph` 跑 `pin_payload_mode="summary"`，期望：
   - 不超时，能返回 23 节点完整结构
   - 至少有一些 pin 出现 `default_value_truncated=true` + `default_value_len` + `default_value_preview` 三个字段
   - 短 pin（如 bool 默认值）仍以 `default_value` 形式存在
4. **names_only 模式验证**：同一 AnimGraph 跑 `pin_payload_mode="names_only"`，期望：
   - 比 summary 模式更小的响应体
   - 任何 pin 都没有 `default_value` / `default_value_len` / `default_value_preview` 字段
   - `links` / `type` / `sub_type` / `direction` / `name` 仍然完整
5. **未识别模式回落**：`pin_payload_mode="garbage"` 应该静默走 Full 路径，输出与默认调用一致

## 后续可扩展点（本次未做）

- 阈值与预览长度做成参数（目前是 constexpr 常量）——等出现实际需求再加，避免多余表面积
- 对 pin `type=pose / pose_component_space` 单独打标记，便于客户端识别 anim node 的 pose link——目前可以靠 `class` 字段（`AnimGraphNode_*`）反推
- `SerializeGraphNodeToJson` 暴露给其他 command（例如未来如果给 `get_blueprint_info` 加 `expand_event_graphs` 参数时）也可以共享 mode 选项
