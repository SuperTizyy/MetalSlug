# UpgradeActivitySaveModifier 使用说明

## 概述
UpgradeActivitySaveModifier是一个专门用于运行时修改UpgradeActivitySubsystem数据的独立工具类。它与Subsystem完全解耦，提供了灵活的数据修改、查询和持久化功能。

## 设计理念
- **职责分离**: 修改器专注数据操作，Subsystem专注业务逻辑
- **独立运行**: 可以单独使用，无需依赖Subsystem实例
- **数据共享**: 与Subsystem操作相同的数据源，实现数据一致性
- **开发友好**: 提供丰富的调试接口和详细的日志输出

## 核心特性
- **热数据连接**: 直接操作Subsystem内存中的活动数据，修改立即生效
- **双重操作模式**: 优先修改热数据，可选同步保存到磁盘
- **智能降级**: Subsystem不可用时自动回退到存档操作
- **完全解耦**: 独立于UpgradeActivitySubsystem运行
- **自动UI刷新**: 修改后自动调用Broadcast触发界面更新
- **数据持久化**: 所有修改自动保存到.sav文件
- **控制台命令**: 丰富的调试指令支持
- **参数验证**: 完善的输入参数检查

## 可用控制台命令

### 1. 设置经验值
```
Upgrade.SetExp [记录日期] [经验值]
```
**功能**: 设置指定日期的经验值  
**示例**: 
```
Upgrade.SetExp 1 150
```

### 2. 设置奖励图标索引
```
Upgrade.SetIcon [记录日期] [图标索引]
```
**功能**: 设置指定日期的奖励图标索引  
**示例**: 
```
Upgrade.SetIcon 1 2
```

### 3. 设置宝箱领取状态
```
Upgrade.SetChest [记录日期] [宝箱索引] [状态]
```
**功能**: 设置指定宝箱的领取状态  
**参数**: 状态(0=未领取, 1=已领取)  
**示例**: 
```
Upgrade.SetChest 1 0 1    // 设置第一个宝箱为已领取
```

### 4. 创建新记录
```
Upgrade.CreateRecord [记录日期] [继承前一天数据]
```
**功能**: 创建指定日期的新记录  
**参数**: 继承参数(0=不继承, 1=继承，默认为1)  
**示例**: 
```
Upgrade.CreateRecord 2 1    // 创建第2天记录并继承第1天数据
```

### 5. 重置记录数据
```
Upgrade.Reset [记录日期]
```
**功能**: 重置指定日期的所有数据到初始状态  
**示例**: 
```
Upgrade.Reset 1
```

### 6. 显示记录信息
```
Upgrade.ShowInfo [记录日期]
```
**功能**: 显示指定日期的完整数据信息  
**示例**: 
```
Upgrade.ShowInfo 1
```

## C++ API接口

### 初始化和销毁
```cpp
// 初始化修改器
bool InitializeModifier(UObject* WorldContext);

// 销毁修改器
void DestroyModifier();
```

### 数据修改接口
```cpp
// 修改经验值
bool ModifyCurrentExperience(int32 RecordDate, int32 NewExp, bool bAutoSave = true);

// 修改奖励图标索引
bool ModifyRewardIconIndex(int32 RecordDate, int32 NewIndex, bool bAutoSave = true);

// 修改宝箱领取状态
bool ModifyChestClaimStatus(int32 RecordDate, int32 ChestIndex, int32 IsClaimed, bool bAutoSave = true);

// 修改任务完成次数
bool ModifyTaskCompleteCount(int32 RecordDate, int32 TaskIndex, int32 Count, bool bAutoSave = true);

// 修改任务领取状态
bool ModifyTaskClaimStatus(int32 RecordDate, int32 TaskIndex, int32 IsClaimed, bool bAutoSave = true);

// 修改限时活动完成次数
bool ModifyLimitedActivityCount(int32 RecordDate, int32 Count, bool bAutoSave = true);

// 重置记录数据
bool ResetRecordData(int32 RecordDate, bool bAutoSave = true);

// 创建新记录
bool CreateNewRecord(int32 RecordDate, bool bInheritPrevious = true, bool bAutoSave = true);
```

### 数据查询接口
```cpp
// 获取经验值
int32 GetCurrentExperience(int32 RecordDate) const;

// 获取奖励图标索引
int32 GetRewardIconIndex(int32 RecordDate) const;

// 获取宝箱领取状态
int32 GetChestClaimStatus(int32 RecordDate, int32 ChestIndex) const;

// 获取任务完成次数
int32 GetTaskCompleteCount(int32 RecordDate, int32 TaskIndex) const;

// 获取任务领取状态
int32 GetTaskClaimStatus(int32 RecordDate, int32 TaskIndex) const;

// 获取限时活动完成次数
int32 GetLimitedActivityCount(int32 RecordDate) const;
```

### 保存接口
```cpp
// 保存指定记录
bool SaveRecord(int32 RecordDate);

// 保存所有记录
bool SaveAllRecords();

// 加载记录
bool LoadRecord(int32 RecordDate);
```

## 使用示例

### 基本使用流程
```cpp
// 1. 创建修改器实例
UUpgradeActivitySaveModifier* Modifier = NewObject<UUpgradeActivitySaveModifier>();

// 2. 初始化 - 推荐方式：传入Subsystem实例
UUpgradeActivitySubsystem* ActivitySubsystem = GetGameInstance()->GetSubsystem<UUpgradeActivitySubsystem>();
Modifier->InitializeModifier(GetWorld(), ActivitySubsystem);

// 或者让修改器自动获取Subsystem
// Modifier->InitializeModifier(GetWorld());

// 3. 执行修改操作（直接修改内存数据）
Modifier->ModifyCurrentExperience(1, 200);  // 立即生效
Modifier->ModifyRewardIconIndex(1, 3);      // 立即生效

// 4. 查看结果
int32 CurrentExp = Modifier->GetCurrentExperience(1);
UE_LOG(LogTemp, Log, TEXT("当前经验值: %d"), CurrentExp);

// 5. 销毁修改器
Modifier->DestroyModifier();
```

### 控制台调试示例
```bash
# 设置第1天的经验值为150
Upgrade.SetExp 1 150

# 设置第1天的奖励图标为索引2
Upgrade.SetIcon 1 2

# 设置第1天第0个宝箱为已领取
Upgrade.SetChest 1 0 1

# 查看第1天的完整信息
Upgrade.ShowInfo 1

# 重置第1天数据
Upgrade.Reset 1
```

## 重要说明

### Broadcast机制
修改器在每次修改数据后会自动调用`TargetSubsystem->OnGlobalRefresh.Broadcast()`，确保UI界面能够及时刷新显示最新数据。

### 初始化要求
UpgradeActivitySubsystem会在Initialize阶段自动创建并注册修改器：
```cpp
// 在UpgradeActivitySubsystem::Initialize中
SaveModifier = NewObject<UUpgradeActivitySaveModifier>(this);
SaveModifier->InitializeModifier(this, this);  // 传入自身作为Subsystem
SaveModifier->RegisterConsoleCommands();
```

## 注意事项

1. **数据独立性**: 修改器与Subsystem完全独立，修改不会直接影响Subsystem运行时状态
2. **自动刷新**: 所有修改都会自动触发UI刷新，无需手动操作
3. **存档管理**: 所有修改都会自动保存到独立的.sav文件中
4. **参数范围**: 注意各参数的有效范围，超出范围会有警告提示
5. **继承机制**: 创建新记录时可以选择是否继承前一天的部分数据

## 文件结构
```
Source/MetalSlug01/
├── Public/Tools/
│   └── UpgradeActivitySaveModifier.h      # 头文件
└── Private/Tools/
    └── UpgradeActivitySaveModifier.cpp    # 实现文件
```

## 架构说明

### 解耦设计
原来的调试功能已从UpgradeActivitySubsystem中完全剥离，转移到独立的修改器类中：

**旧架构**：
```
UpgradeActivitySubsystem (包含所有调试指令)
```

**新架构**：
```
UpgradeActivitySubsystem (专注业务逻辑)
        ↑
        |
UpgradeActivitySaveModifier (独立调试功能)
```

### 功能迁移对照表

| 原Subsystem方法 | 新修改器方法 | 功能说明 |
|----------------|-------------|----------|
| `SetCurrentExp()` | `ModifyCurrentExperience()` | 设置经验值 |
| `SetRewardIconIndex()` | `ModifyRewardIconIndex()` | 设置奖励图标索引 |
| `SetChestClaimStatus()` | `ModifyChestClaimStatus()` | 设置宝箱领取状态 |
| `SetTaskCompleteCount()` | `ModifyTaskCompleteCount()` | 设置任务完成次数 |
| `SetTaskClaimStatus()` | `ModifyTaskClaimStatus()` | 设置任务领取状态 |
| `SetLimitedActivityCount()` | `ModifyLimitedActivityCount()` | 设置限时活动次数 |
| `ShowCurrentRecordInfo()` | `DisplayUpgradeActivityInfo()` | 显示记录信息 |
| `ResetAllData()` | `ResetUpgradeActivityData()` | 重置数据 |
| `CreateRecordForDay()` | `CreateNewRecord()` | 创建新记录 |
| `ShowAllSaveRecords()` | *(通过查询接口实现)* | 显示所有记录 |

## 集成使用示例

### 在Subsystem中启用修改器
```cpp
// UpgradeActivitySubsystem.h 中添加
UPROPERTY()
UUpgradeActivitySaveModifier* DebugModifier;

UPROPERTY()
bool bEnableDebugModifier;

// 初始化时创建修改器
void InitializeWithModifier();

// 关闭时销毁修改器
void ShutdownWithModifier();

// 提供外部访问接口
UFUNCTION(BlueprintCallable)
UUpgradeActivitySaveModifier* GetDebugModifier() const;
```

### 条件编译最佳实践
```cpp
// 只在开发环境中启用
#if WITH_EDITOR || UE_BUILD_DEVELOPMENT
    bEnableDebugModifier = true;
#else
    bEnableDebugModifier = false;
#endif

// 或者使用配置变量
if (GetDefault<UYourGameSettings>()->bEnableDebugTools)
{
    InitializeWithModifier();
}
```

### 数据同步机制
```cpp
// 当修改器数据变化时，手动同步到Subsystem
void SyncDataFromModifier(int32 RecordDate)
{
    if (DebugModifier)
    {
        int32 NewExp = DebugModifier->GetCurrentExperience(RecordDate);
        CurrentRecord.CurrentExperience = NewExp;
        SaveStatus();
        OnGlobalRefresh.Broadcast();
    }
}
```
