# ActivitySubsystem 动态表修改器集成说明

## 概述

本文档说明如何在ActivitySubsystem中集成万能动态表修改器，实现在运行时修改DataTable数据(.sav文件)并自动保存的功能。

## 集成要点

### ✅ 保持原有业务逻辑不变
- 所有原有的ActivitySubsystem接口保持不变
- 每日登录页面的业务逻辑完全不受影响
- 向后兼容，不会破坏现有功能

### ✅ 新增功能特性
- 运行时动态修改DataTable数据
- 自动保存修改到.sav文件
- 支持事务性操作
- 提供蓝图友好的接口

## 核心改动

### 1. ActivitySubsystem.h 增强
```cpp
// 新增头文件引用
#include "Tools/UniversalDataTableModifier.h"

// 新增成员变量
UPROPERTY()
UUniversalDataTableModifier* DataTableModifier = nullptr;

// 新增接口方法
UFUNCTION(BlueprintCallable, Category = "DataTable Modifier")
UUniversalDataTableModifier* GetDataTableModifier() const;

UFUNCTION(BlueprintCallable, Category = "DataTable Modifier")
bool InitializeDataTableModifier(UObject* WorldContext);

// 修改后数据获取方法
UFUNCTION(BlueprintCallable, Category = "DataTable Modifier")
TArray<FDailyLoginConfigRow*> GetModifiedDailyLoginConfigs(int32 ActivityID) const;

UFUNCTION(BlueprintCallable, Category = "DataTable Modifier")
TArray<FDailyLoginConfigRow*> GetModifiedRewardsByDay(int32 ActivityID, int32 Day) const;

UFUNCTION(BlueprintCallable, Category = "DataTable Modifier")
const FItemDetailRow* GetModifiedItemDetail(int32 ItemID) const;

UFUNCTION(BlueprintCallable, Category = "DataTable Modifier")
const FTreasureBoxItemRow* GetModifiedTreasureBoxItem(int32 BoxID) const;
```

### 2. ActivitySubsystem.cpp 实现
```cpp
// 初始化时创建修改器实例
DataTableModifier = NewObject<UUniversalDataTableModifier>(this);
FDataTableModificationConfig Config;
Config.bEnablePersistentModification = true;
Config.bAutoSaveChanges = true;
Config.AutoSaveInterval = 30.0f;
DataTableModifier->InitializeModifier(Config, this);

// 销毁时清理资源
if (DataTableModifier)
{
    DataTableModifier->DestroyModifier();
    DataTableModifier = nullptr;
}
```

### 3. 蓝图函数库支持
创建了`UActivityDataTableModifierBPLibrary`类，提供蓝图友好的接口：
- `GetActivityDataTableModifier()` - 获取修改器实例
- `ModifyDailyLoginReward()` - 修改每日登录奖励
- `ModifyItemDetail()` - 修改物品详情
- `GetModifiedDailyLoginConfigs()` - 获取修改后的配置
- `SaveAllDataTableModifications()` - 保存所有修改
- `LoadSavedDataTableModifications()` - 加载已保存的修改

## 使用示例

### 在C++中使用
```cpp
// 获取修改器
UUniversalDataTableModifier* Modifier = ActivitySubsystem->GetDataTableModifier();

// 修改数据
FDailyLoginConfigRow NewConfig;
NewConfig.RewardCount = 100; // 设置新奖励数量

FGuid ModificationId = Modifier->UpdateTableRow(
    TEXT("/Game/UI/Activity/Data/DT_DailyLoginConfigRow"),
    FName(TEXT("1_1")), // 第1天的配置
    reinterpret_cast<const uint8*>(&NewConfig),
    true // 持久化
);

// 自动保存会在30秒后触发，也可以手动保存
Modifier->SaveAllModifications();
```

### 在蓝图中使用
1. 使用`GetActivityDataTableModifier`节点获取修改器
2. 使用`ModifyDailyLoginReward`节点修改奖励数量
3. 使用`GetModifiedDailyLoginConfigs`获取修改后的数据
4. 使用`SaveAllDataTableModifications`手动保存

## 数据流说明

```
原始DataTable ←→ 动态表修改器 ←→ 修改后的数据
     ↓                ↓                  ↓
  静态资源        内存修改缓存       运行时使用
     ↓                ↓                  ↓
   .uasset         JSON序列化        UI显示更新
     ↓                ↓                  ↓
                   .sav文件           自动保存
```

## 注意事项

### 🚫 不会影响的功能
- 每日登录Track的原有逻辑
- 红点管理系统的功能
- 页面管理器的行为
- 存档系统的正常工作

### ✅ 新增的安全保障
- 修改操作有完整的验证机制
- 支持事务回滚
- 修改历史记录追踪
- 自动保存防止数据丢失

### ⚙️ 配置参数
```cpp
FDataTableModificationConfig Config;
Config.bEnablePersistentModification = true;  // 启用持久化
Config.bAutoSaveChanges = true;               // 启用自动保存
Config.AutoSaveInterval = 30.0f;              // 30秒保存间隔
Config.MaxHistoryRecords = 100;               // 最大历史记录数
Config.bEnableTransactionSupport = true;      // 启用事务支持
```

## 测试建议

1. **功能测试**：验证修改后的数据能否正确显示
2. **持久化测试**：重启游戏后检查修改是否保留
3. **兼容性测试**：确保原有功能不受影响
4. **性能测试**：大量修改操作的性能表现

## 故障排除

### 常见问题
- **修改不生效**：检查是否调用了正确的获取方法（Modified版本）
- **数据丢失**：确认自动保存或手动保存已执行
- **蓝图节点找不到**：重新编译项目，确保模块正确加载

### 日志关键字
- `ActivitySubsystem: 动态表修改器初始化完成`
- `UniversalDataTableModifier: 成功保存所有修改`
- `ActivityDataTableModifierBPLibrary: 成功修改每日登录奖励`

---
*此集成为了在不影响原有业务逻辑的前提下，提供灵活的动态数据修改能力*