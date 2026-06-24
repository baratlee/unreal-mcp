# execute_python_script / execute_python_file

**日期：** 2026-06-24  
**任务：** LT16 UnrealMCP 持续扩展  
**需求背景：** 项目需要能通过 MCP 在 UE 编辑器内执行 Python 脚本（字符串代码或 .py 文件），以替代手动打开控制台粘贴的繁琐流程，并允许 Claude Code 自动化编辑器级别的批量操作。

---

## 改动清单

| 文件 | 类型 | 变更 |
|---|---|---|
| `Public/Commands/UnrealMCPPythonCommands.h` | 新建 | `FUnrealMCPPythonCommands` 类声明 |
| `Private/Commands/UnrealMCPPythonCommands.cpp` | 新建 | `HandleExecutePythonScript` / `HandleExecutePythonFile` 实现 |
| `UnrealMCP.Build.cs` | 修改 | Editor 依赖块追加 `PythonScriptPlugin` |
| `UnrealMCP.uplugin` | 修改 | Plugins 数组追加 `PythonScriptPlugin` |
| `Public/UnrealMCPBridge.h` | 修改 | include + `TSharedPtr<FUnrealMCPPythonCommands> PythonCommands` 成员 |
| `Private/UnrealMCPBridge.cpp` | 修改 | include + 构造/析构 + 路由条目 |
| `Python/tools/python_tools.py` | 新建 | `execute_python_script` / `execute_python_file` MCP 工具 |
| `Python/unreal_mcp_server.py` | 修改 | import + `register_python_tools(mcp)` |

---

## 核心实现

**C++ 引擎侧（`IPythonScriptPlugin`）：**
- 调 `IPythonScriptPlugin::Get()->ExecPythonCommandEx(Cmd)`
- `Cmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile`（两个命令均如此）
- `Cmd.Command`：P0 填脚本字符串，P1 填文件绝对路径
- 命令执行在 Game Thread（Bridge 已用 `AsyncTask(ENamedThreads::GameThread)` 调度）
- `LogOutput` 里 Info/Warning → `output`，Error → `error`
- `CommandResult`：成功时追加到 `output`，失败时（含 Python 异常 traceback）追加到 `error`

**返回格式：**
```json
{
  "success": true,
  "output": "Hello World\n...",
  "error": ""
}
```

---

## 覆盖矩阵

| 工具 | 场景 | 说明 |
|---|---|---|
| `execute_python_script` | 多行脚本字符串 | `print()` / `unreal.log()` 均捕获 |
| `execute_python_script` | 单行语句 | 同上 |
| `execute_python_file` | 绝对路径 .py 文件 | 如 `Content/Python/batch_retarget.py` |

---

## 风险 & 限制

1. **阻塞 Game Thread**：Python 脚本执行期间编辑器 UI 冻结。长时间脚本（批量重定向等）需用户接受等待，或在脚本内用 `unreal.AssetTools` 异步接口。
2. **PIE 期间可能有副作用**：与所有 `set_*` 命令相同，建议退 PIE 后执行。
3. **仅 Editor 可用**：`PythonScriptPlugin` 是 Editor-only 插件，打包游戏后不可用。
4. **`IsPythonInitialized()` 守门**：编辑器启动期间过早调用会返回错误，而非静默失败。

---

## 生效条件

- **C++ 端**：VS 重编 UnrealMCP 模块 + 重启编辑器（加载新 DLL）
- **Python 端**：重连 MCP 服务（重启 Claude Code 会话），重启编辑器对 Python 工具无效

---

## 验证步骤

```
# P0 round-trip
execute_python_script(script="print('hello from MCP')")
# 期望：success=true, output="hello from MCP"

# P0 捕获 unreal.log
execute_python_script(script="import unreal; unreal.log('test log')")
# 期望：success=true, output 含 "test log"

# P0 错误路径
execute_python_script(script="raise ValueError('test error')")
# 期望：success=false, error 含 "ValueError: test error" traceback

# P1 文件执行
execute_python_file(file_path="C:/Workspace/.../Content/Python/batch_retarget.py")
# 期望：success=true（依脚本逻辑）
```

---

## 后续

- 如需超长脚本支持（批量操作），建议脚本内部自行做分批 / 进度打印
- 可复用此能力替代手动运行 `Content/Python/*.py` 的所有场景
