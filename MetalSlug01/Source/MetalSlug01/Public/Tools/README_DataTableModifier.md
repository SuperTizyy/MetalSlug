# 万能动态表修改器 (Universal DataTable Modifier)

## 概述

这是一个功能强大的UE C++工具，用于在运行时动态修改DataTable数据。它支持内存修改和持久化存储两种模式，提供事务支持、修改历史记录和回滚功能。

## 主要特性

### 🔄 双模式支持
- **内存修改模式**: 修改仅在当前游戏会话中有效
- **持久化修改模式**: 修改数据保存到磁盘，下次启动时自动加载

### 📊 完整的修改操作
- 添加表行 (AddRow)
- 更新表行 (UpdateRow)  
- 删除表行 (DeleteRow)
- 批量修改 (BatchModify)

### 🔐 事务支持
- 原子性操作保证
- 事务提交和回滚
- 超时自动回滚机制

### 📝 修改追踪
- 完整的修改历史记录
- 修改前后数据快照
- 支持单个修改回滚

### ⚙️ 灵活配置
- 可配置的自动保存间隔
- 自定义历史记录上限
- 灵活的初始化选项

## 文件结构

```
Source/MetalSlug01/
├── Public/Tools/
│   ├── UniversalDataTableModifier.h          # 核心头文件
│   ├── DataTableModifierExample.h           # 使用示例
│   └── README_DataTableModifier.md          # 本文档
└── Private/Tools/
    ├── UniversalDataTableModifier.cpp       # 核心实现
    └── DataTableModifierExample.cpp         # 示例实现
```

## 快速开始

### 1. 基本初始化

```cpp
// 创建修改器实例
UUniversalDataTableModifier* Modifier = NewObject<UUniversalDataTableModifier>();

// 配置修改器
FDataTableModificationConfig Config;
Config.bEnableMemoryModification = true;
Config.bEnablePersistentModification = false;

// 初始化
Modifier->InitializeModifier(Config, GetWorld());
```

### 2. 简单修改操作

```cpp
// 修改每日登录奖励数量
FString TablePath = TEXT("/Game/UI/Activity/Data/DT_DailyLoginConfigRow");
FName RowName = FName(TEXT("1")); // 第一天

// 获取原始数据
UDataTable* Table = Modifier->LoadDataTable(TablePath);
FDailyLoginConfigRow* OriginalRow = Table->FindRow<FDailyLoginConfigRow>(RowName, TEXT("Context"));

if (OriginalRow)
{
    // 创建修改后的数据
    FDailyLoginConfigRow ModifiedRow = *OriginalRow;
    ModifiedRow.RewardCount = 10; // 修改为10个奖励
    
    // 执行更新
    Modifier->UpdateTableRow(
        TablePath,
        RowName,
        reinterpret_cast<const uint8*>(&ModifiedRow),
        false // 临时修改
    );
}
```

### 3. 事务性操作

```cpp
// 开始事务
FGuid TransactionId = Modifier->BeginTransaction(TEXT("批量奖励更新"));

// 执行多个相关修改
Modifier->AddTableRow(LoginTablePath, RowName1, &DataRow1);
Modifier->UpdateTableRow(ItemTablePath, RowName2, &ItemRow2);
Modifier->DeleteTableRow(RemoveTablePath, RowName3);

// 提交事务（全部成功）或自动回滚（任一失败）
if (Modifier->CommitTransaction(TransactionId))
{
    UE_LOG(LogTemp, Log, TEXT("所有修改已成功应用"));
}
```

### 4. 持久化修改

```cpp
// 启用持久化配置
FDataTableModificationConfig PersistentConfig;
PersistentConfig.bEnablePersistentModification = true;
PersistentConfig.bAutoSaveChanges = true;
PersistentConfig.AutoSaveInterval = 60.0f; // 每60秒自动保存

// 初始化支持持久化的修改器
UUniversalDataTableModifier* PersistentModifier = NewObject<UUniversalDataTableModifier>();
PersistentModifier->InitializeModifier(PersistentConfig, GetWorld());

// 执行需要保存的修改
Modifier->AddTableRow(TablePath, RowName, &NewRowData, true); // true表示持久化

// 手动保存（也可依赖自动保存）
Modifier->SaveAllModifications();
```

## 高级功能

### 修改历史查询

```cpp
// 获取最近的修改记录
TArray<FTableModificationRecord> RecentModifications = Modifier->GetModificationHistory(50);

for (const FTableModificationRecord& Record : RecentModifications)
{
    UE_LOG(LogTemp, Log, TEXT("修改类型: %s, 目标表: %s, 行: %s"), 
        *UEnum::GetValueAsString(Record.ModificationType),
        *Record.TargetTablePath,
        *Record.TargetRowName.ToString());
}
```

### 单个修改回滚

```cpp
// 回滚指定的修改
FGuid ModificationId = /* 从历史记录中获取 */;
if (Modifier->RevertModification(ModificationId))
{
    UE_LOG(LogTemp, Log, TEXT("修改已成功回滚"));
}
```

### 批量操作

```cpp
// 准备批量修改数据
TArray<FTableModificationRecord> BatchOperations;

// 添加多个修改操作到批次
FTableModificationRecord AddRecord(ETableModificationType::AddRow, TablePath, NewRowName);
AddRecord.ModifiedDataSnapshot = SerializeToJson(&NewRowData);
BatchOperations.Add(AddRecord);

FTableModificationRecord UpdateRecord(ETableModificationType::UpdateRow, TablePath, ExistingRowName);
UpdateRecord.ModifiedDataSnapshot = SerializeToJson(&UpdatedRowData);
BatchOperations.Add(UpdateRecord);

// 执行批量修改
FGuid BatchId = Modifier->BatchModifyTable(TablePath, BatchOperations);
```

## 最佳实践

### 1. 配置建议

```cpp
// 生产环境推荐配置
FDataTableModificationConfig ProductionConfig;
ProductionConfig.bEnableMemoryModification = true;      // 启用内存修改
ProductionConfig.bEnablePersistentModification = true;  // 启用持久化
ProductionConfig.MaxHistoryRecords = 1000;              // 保留1000条历史
ProductionConfig.bAutoSaveChanges = true;               // 自动保存
ProductionConfig.AutoSaveInterval = 120.0f;             // 每2分钟保存一次
ProductionConfig.bEnableTransactionSupport = true;      // 启用事务
ProductionConfig.TransactionTimeout = 30.0f;            // 30秒事务超时
```

### 2. 错误处理

```cpp
// 验证修改操作
FString ErrorMessage;
if (!Modifier->ValidateModification(TablePath, RowName, ModificationType, ErrorMessage))
{
    UE_LOG(LogTemp, Error, TEXT("修改验证失败: %s"), *ErrorMessage);
    return;
}

// 检查修改结果
FGuid ModificationId = Modifier->UpdateTableRow(/* 参数 */);
if (!ModificationId.IsValid())
{
    UE_LOG(LogTemp, Error, TEXT("修改操作失败"));
    return;
}
```

### 3. 性能优化

```cpp
// 批量操作优于多次单次操作
// ❌ 不推荐
for (int i = 0; i < 100; ++i)
{
    Modifier->UpdateTableRow(TablePath, FName(*FString::FromInt(i)), &Data[i]);
}

// ✅ 推荐
TArray<FTableModificationRecord> BatchMods;
for (int i = 0; i < 100; ++i)
{
    FTableModificationRecord Record(/* ... */);
    BatchMods.Add(Record);
}
Modifier->BatchModifyTable(TablePath, BatchMods);
```

## 与其他系统的集成

### 与ActivitySubsystem集成

```cpp
// 在ActivitySubsystem中使用修改器
class UActivitySubsystem : public UGameInstanceSubsystem
{
private:
    UPROPERTY()
    UUniversalDataTableModifier* DataTableModifier;

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override
    {
        Super::Initialize(Collection);
        
        // 初始化修改器
        DataTableModifier = NewObject<UUniversalDataTableModifier>();
        FDataTableModificationConfig Config;
        DataTableModifier->InitializeModifier(Config, GetGameInstance()->GetWorld());
    }

    // 提供便捷的修改接口
    bool ModifyDailyLoginReward(int32 DayIndex, int32 NewRewardCount)
    {
        FDailyLoginConfigRow ModifiedRow;
        ModifiedRow.DayIndex = DayIndex;
        ModifiedRow.RewardCount = NewRewardCount;
        
        return DataTableModifier->UpdateTableRow(
            TEXT("/Game/UI/Activity/Data/DT_DailyLoginConfigRow"),
            FName(*FString::FromInt(DayIndex)),
            reinterpret_cast<const uint8*>(&ModifiedRow),
            true // 持久化修改活动配置
        ).IsValid();
    }
};
```

## 注意事项

⚠️ **重要提醒**:

1. **内存管理**: 修改器会自动管理分配的内存，但在销毁时确保调用`DestroyModifier()`
2. **线程安全**: 当前版本不支持多线程并发修改同一表
3. **性能考虑**: 频繁的小修改建议使用批量操作
4. **存档兼容性**: 持久化数据格式可能会随版本更新而变化
5. **蓝图使用**: 所有公共方法都标记为`BlueprintCallable`，可在蓝图中使用

## 故障排除

### 常见问题

**Q: 修改后数据没有生效？**
A: 检查是否正确调用了`GetModifiedRow()`而不是直接从DataTable获取

**Q: 持久化数据丢失？**
A: 确保`SaveAllModifications()`被正确调用，检查存档文件权限

**Q: 事务提交失败？**
A: 查看详细的错误日志，通常是因为某个修改操作验证失败

**Q: 内存占用过高？**
A: 调整`MaxHistoryRecords`配置，定期清理不需要的历史记录

## 版本历史

- **v1.0.0** (2026-02-18): 初始版本发布
  - 基本的增删改查功能
  - 内存和持久化双模式
  - 事务支持
  - 修改历史追踪

---

*遵循UE工业级编码规范，提供完整的中文注释和文档*