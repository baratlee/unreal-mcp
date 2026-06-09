# 2026-06-09 · `set_*` 工具支持 FArrayProperty 写入（Phase A）

## 背景

`SetObjectProperty` 的 type-switch 此前不识别 `FArrayProperty`，所有 `TArray<...>` 写入直接落到末尾 "Unsupported property type: ArrayProperty"。

实测两次卡点：
- 2026-06-03 `set_data_asset_property` 写 GE Modifiers array
- 2026-06-09 `set_component_property` 写 `GeometryCollectionComponent.CollisionProfilePerLevel: TArray<FName>`（LT6 #9）

## 改动

`UnrealMCPCommonUtils.cpp` 在 `FEnumProperty` 分支与 generic `ImportText` fallback 之间新增 `FArrayProperty` 分支：把 JSON Array 转 T3D 数组字面量 `("a","b",1.0,True)`，喂给 `Property->ImportText_Direct`。

```cpp
if (Property->IsA<FArrayProperty>() && Value->Type == EJson::Array)
{
    const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
    FString T3D = TEXT("(");
    for (int32 i = 0; i < Arr.Num(); ++i)
    {
        if (i > 0) T3D += TEXT(",");
        switch (Arr[i]->Type)
        {
        case EJson::String:
            T3D += FString::Printf(TEXT("\"%s\""), *Arr[i]->AsString().ReplaceCharWithEscapedChar());
            break;
        case EJson::Number:
            T3D += FString::SanitizeFloat(Arr[i]->AsNumber());
            break;
        case EJson::Boolean:
            T3D += Arr[i]->AsBool() ? TEXT("True") : TEXT("False");
            break;
        default:
            OutErrorMessage = ...;
            return false;
        }
    }
    T3D += TEXT(")");
    if (Property->ImportText_Direct(*T3D, PropertyAddr, Object, PPF_None))
    { bWritten = true; break; }
    OutErrorMessage = ...; return false;
}
```

## 覆盖范围

| 元素类型 | 支持 | JSON 形态 |
|---|---|---|
| `TArray<FName>` | ✅ | `["None", "GC_PhysicsOnly"]` |
| `TArray<FString>` / `TArray<FText>` | ✅ | `["str1", "str2"]` |
| `TArray<float>` / `TArray<int32>` | ✅ | `[1.0, 2.5, 3]` |
| `TArray<bool>` | ✅ | `[true, false]` |
| `TArray<TSoftObjectPtr<...>>` / `TArray<TObjectPtr<...>>` | ✅ | `["/Game/Foo/Bar.Bar"]` |
| `TArray<FStruct>` | ❌ Phase B 再做 | — |
| 嵌套 `TArray<TArray<...>>` | ❌ Phase B 再做 | — |

## 风险

- 纯新增分支，老路径未改 → 任何不传数组的现有调用行为不变
- T3D 整数被 `SanitizeFloat` 输出成 `5.000000` —— UE 的 `ImportText_Direct` 对 int element 解析普遍宽容，未踩坑前不处理
- `Inner = FStructProperty` 时 JSON Object 元素会被显式拒绝（清晰错误信息），不会静默成功

## 调用范围

所有走 `SetObjectProperty` 的工具自动获益：
- `set_actor_property`
- `set_blueprint_property`
- `set_component_property`
- `set_data_asset_property`
- `set_animation_notify_property`
- `set_state_tree_node_property`

## 验证（用户重编后跑）

1. **回归**：`set_data_asset_property` 写 `bRemoveOnMaxSleep: true` 仍能成
2. **新功能**：`set_component_property` 写 `CollisionProfilePerLevel: ["None", "GC_PhysicsOnly", "GC_PhysicsOnly", "GC_PhysicsOnly"]` 应能成
3. **错误路径**：传 `[{"Key": "Val"}]`（FStruct 元素）应报清晰错误

## 后续

Phase B 计划重构 `SetObjectProperty` 拆出 `WriteValueIntoProperty(FProperty*, void* ValuePtr, FJsonValue, ...)` helper，解锁：
- `TArray<FStruct>`（如 `SizeSpecificData`、Niagara emitter list、GE Modifiers）
- FStruct 字段递归直写（如 `BodyInstance.CollisionResponses`）
- 嵌套 `TArray<TArray<...>>`

Phase B 启动时机：Phase A 验证稳定 + 实际遇到 FStruct 数组需求时。
