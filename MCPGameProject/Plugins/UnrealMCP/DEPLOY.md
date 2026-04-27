# UnrealMCP 部署指南 (for Claude Code)

## 架构概述

unreal-mcp 由两部分组成:
- **C++ 插件** (`UnrealMCP`): UE Editor 模块，启动 TCP Server 监听命令
- **Python MCP Server** (`unreal_mcp_server.py`): Claude Code 的 stdio MCP 进程，通过 TCP 连接插件

两端通过 TCP 通信，默认端口 `55557`，可通过 `UNREAL_MCP_PORT` 环境变量统一配置。

## 仓库路径

- unreal-mcp 仓库: `C:/Workspace/git/unreal-mcp`
- C++ 插件源码: `C:/Workspace/git/unreal-mcp/MCPGameProject/Plugins/UnrealMCP`
- Python Server: `C:/Workspace/git/unreal-mcp/Python`

## 步骤

### 1. 链接插件到目标工程

在目标工程的 `Plugins/` 目录下创建目录符号链接:

```cmd
mklink /D "<TargetProject>/Plugins/UnrealMCP" "C:\Workspace\git\unreal-mcp\MCPGameProject\Plugins\UnrealMCP"
```

需要管理员权限或开发者模式。链接后所有工程共享同一份插件源码。

### 2. 编译

插件依赖以下 UE 模块，目标工程的 `.uproject` 需启用对应插件:
- EditorScriptingUtilities
- Chooser
- IKRig
- EnhancedInput
- PoseSearch

编译目标工程即可，插件 `bPrecompiled: true` 但含源码，首次编译会自动构建。

### 3. 创建 `.mcp.json`

在目标工程根目录创建 `.mcp.json`:

```json
{
  "mcpServers": {
    "unrealMCP": {
      "command": "uv",
      "args": [
        "--directory",
        "C:/Workspace/git/unreal-mcp/Python",
        "run",
        "--python",
        "3.12",
        "unreal_mcp_server.py"
      ]
    }
  }
}
```

Python 要求 3.12（避免 3.14 的 pydantic-core 源码编译问题）。`uv` 需在 PATH 中。

### 4. 端口配置

默认端口 `55557`。两个工程同时运行时需分配不同端口。

**唯一配置位置**: `<TargetProject>/.claude/settings.local.json` 的 `env` 字段:

```json
{
  "env": {
    "UNREAL_MCP_PORT": "55558"
  }
}
```

两端如何读取:
- **Python 侧**: `os.environ.get("UNREAL_MCP_PORT", "55557")` — Claude Code 启动 MCP Server 时自动注入 `settings.local.json` 中的 env
- **C++ 侧**: `GetMCPPortFromSettings()` 读取 `<ProjectDir>/.claude/settings.local.json` 和 `settings.json` 的 `env.UNREAL_MCP_PORT` 字段，优先 `settings.local.json`

两端读同一个 env 值，所以只需在 `settings.local.json` 配一次。

### 5. Claude Code settings 配置

`<TargetProject>/.claude/settings.local.json` 最小配置:

```json
{
  "env": {
    "UNREAL_MCP_PORT": "55557"
  },
  "enableAllProjectMcpServers": true,
  "enabledMcpjsonServers": [
    "unrealMCP"
  ]
}
```

如需对 Chooser/PoseSearch 写操作加确认，在 `settings.json` 的 `permissions.ask` 中列出相关工具名。参考 `C:/Workspace/git/LeoPractice/.claude/settings.json`。

### 6. 验证

1. 启动 UE Editor（插件自动启动 TCP Server）
2. 启动 Claude Code（自动启动 Python MCP Server）
3. 调用任意只读工具测试，如 `get_project_info`

## 多工程并行

每个工程分配不同端口:
- 工程 A: `UNREAL_MCP_PORT=55557`
- 工程 B: `UNREAL_MCP_PORT=55558`

各自的 `.claude/settings.local.json` 写对应端口即可。
