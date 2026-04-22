# unreal-mcp 修复：`set_ik_retargeter_op_field` value 字段类型 coerce bug

**日期**：2026-04-22
**涉及仓库**：
- `C:\Workspace\git\unreal-mcp`（Python MCP server，仅 Python 改动）

## 问题

Batch C.2 验证期间发现：调用 `set_ik_retargeter_op_field(..., value="false")` 会被 pydantic 拒绝：
```
1 validation error for set_ik_retargeter_op_fieldArguments
value
  Input should be a valid string [type=string_type, input_value=False, input_type=bool]
```

根因：Claude 模型在工具调用 JSON 里把 `"false"` / `"true"` / `"123"` 这样的字面值序列化成 JSON literal（bool / number）。Python wrapper 声明 `value: str`，pydantic 按严格模式拒收。

**临时绕过**：传首字母大写 `"False"` / `"True"`（避开 JSON literal 模式）。能工作但反直觉。

## 修复

`set_ik_retargeter_op_field` 的 Python wrapper：
- 签名 `value: str` → `value: Union[str, bool, int, float]`
- wrapper 内部按类型 stringify：
  - `True`/`False` → `"true"`/`"false"`（UE `ImportText_Direct` 接受小写）
  - `int`/`float` → `str(value)`
  - `str` → verbatim
- C++ 侧不变（原本就按字符串走 `ImportText_Direct`）
- 不支持的结构化类型（list/dict）保持要求 T3D 格式字符串 —— docstring 明确标注

## 修改的文件

| 文件 | 改动 |
|---|---|
| `Python/tools/animation_tools.py` | `import Union`；`set_ik_retargeter_op_field` 签名 + 内部 stringify 分支 + docstring 更新 |

无 C++ 改动、无 Build.cs / Bridge 改动。

## 生效方式

仅需重启 MCP server（关 Claude Code / `/mcp reload`）即可，无需 UE 插件重编。

## 验证

- `value=True`（Python bool）→ 应等价于 `value="true"`（passing through）
- `value=False` → 等价 `"false"`
- `value=10` → 等价 `"10"`
- `value="CopyGlobalPosition"` → verbatim
- `value="(X=1,Y=2,Z=3)"` → verbatim（T3D FVector）

## 其他 write 工具的同类风险

当前只 fix 了 `set_ik_retargeter_op_field`。扫一眼其他可能吃 JSON literal 的 wrapper：

| 工具 | value 字段 | 风险 |
|---|---|---|
| `set_ik_retargeter_op_enabled` | `enabled: bool` | 无 —— 签名就是 bool，Claude 发 `true`/`false` 天然匹配 |
| `set_ik_retargeter_op_field` | `value: ...` | **本次修** |
| `set_actor_property` / `set_component_property` / `set_blueprint_property` | 类似情况 | **未确认**；如果签名 `value: str` 且被调用者遇到，相同 workaround 适用 |

`set_actor_property` 等"按属性名泛型写"的工具都可能有类似 coerce 风险。留作 followup，用户遇到再治本。
