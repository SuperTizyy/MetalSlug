# DailyLoginSave 动态存档修改器集成说明

## 概述

本文档说明如何在ActivitySubsystem中集成DailyLoginSave动态存档修改器，实现在运行时修改玩家动态数据（.sav文件）并自动保存的功能。

## 正确理解需求

### ✅ 动态表 vs 静态表
- **静态表**：DataTable资源文件（.uasset），在UE编辑器中修改
- **动态表**：DailyLoginSave中的运行时数据（.sav文件），在游戏中运行时修改

### ✅ 修改目标
修改DailyLoginSaveGame中的FPlayerLoginRecord结构数据：
- `Progress`：玩家当前进度
- `ClaimedDays`：已领取的天数数组  
- `CurrentClaimCount`：当前领取次数
- `ClaimedHistoryMask`：领取历史掩码

## 核心功能

### 1. 运行时数据修改
```cpp
// 修改玩家进度
ActivitySubsystem->ModifyPlayerProgress(ActivityID, 5, true);

// 修改某天领取状态
ActivitySubsystem->ModifyDayClaimedStatus(ActivityID, 3, true, true);

// 批量修改已领取天数
TArray<int32> Days = {1, 2, 3, 5};
ActivitySubsystem->ModifyClaimedDays(ActivityID, Days, true);

// 重置玩家记录
ActivitySubsystem->ResetPlayerRecord(ActivityID, true);
```

### 2. 自动保存机制
- 修改后自动保存到.sav文件
- 支持手动保存所有记录
- 修改历史记录追踪

### 3. 数据查询接口
```cpp
// 查询当前进度
int32 Progress = ActivitySubsystem->GetSaveModifier()->GetPlayerProgress(ActivityID);

// 查询已领取天数
TArray<int32> ClaimedDays = ActivitySubsystem->GetSaveModifier()->GetClaimedDays(ActivityID);

// 检查某天是否已领取
bool bClaimed = ActivitySubsystem->GetSaveModifier()->IsDayClaimed(ActivityID, 3);
```

## 集成改动

### ActivitySubsystem.h 增强
```cpp
// 新增头文件
#include "Tools/DailyLoginSaveModifier.h"

// 新增成员变量
UPROPERTY()
UDailyLoginSaveModifier* SaveModifier = nullptr;

// 新增接口方法
UFUNCTION(BlueprintCallable, Category = "Save Modifier")
UDailyLoginSaveModifier* GetSaveModifier() const;

UFUNCTION(BlueprintCallable, Category = "Save Modifier")
bool ModifyPlayerProgress(int32 ActivityID, int32 NewProgress, bool bAutoSave = true);

UFUNCTION(BlueprintCallable, Category = "Save Modifier")
bool ModifyDayClaimedStatus(int32 ActivityID, int32 DayIndex, bool bClaimed, bool bAutoSave = true);

// ... 更多接口
```

### ActivitySubsystem.cpp 实现
```cpp
// 初始化时创建修改器
SaveModifier = NewObject<UDailyLoginSaveModifier>(this);
SaveModifier->InitializeModifier(this);

// 销毁时清理资源
if (SaveModifier)
{
    SaveModifier->DestroyModifier();
    SaveModifier = nullptr;
}
```

## 使用场景示例

### 1. 测试时调整玩家进度
```cpp
// 将玩家进度调整到第5天
ActivitySubsystem->ModifyPlayerProgress(7, 5, true);

// 手动领取前几天奖励
ActivitySubsystem->ModifyDayClaimedStatus(7, 1, true, false);
ActivitySubsystem->ModifyDayClaimedStatus(7, 2, true, false);
ActivitySubsystem->ModifyDayClaimedStatus(7, 3, true, false);
ActivitySubsystem->ModifyDayClaimedStatus(7, 4, true, true); // 最后一次自动保存
```

### 2. 重置测试数据
```cpp
// 重置玩家记录，重新开始测试
ActivitySubsystem->ResetPlayerRecord(7, true);
```

### 3. 批量设置测试数据
```cpp
// 设置玩家已经领取了前3天的奖励
TArray<int32> TestDays = {1, 2, 3};
ActivitySubsystem->ModifyClaimedDays(7, TestDays, true);
```

## 数据流向

```
FPlayerLoginRecord (内存) ←→ DailyLoginSaveModifier ←→ .sav文件
       ↓                            ↓                      ↓
   运行时数据                 修改操作和历史记录        持久化存储
       ↓                            ↓                      ↓
   UI界面显示              自动保存/手动保存            磁盘文件
```

## 注意事项

### 🚫 不影响的功能
- 每日登录Track原有逻辑
- 红点管理系统
- 页面管理器行为
- 静态DataTable数据

### ✅ 新增的安全特性
- 修改操作有完整验证
- 修改历史记录追踪
- 自动保存防止数据丢失
- 事件广播通知UI更新

### ⚙️ 配置说明
```cpp
// 自动保存默认开启
bool bAutoSave = true;

// 修改会自动触发OnActivityDataChanged事件
// UI可以监听此事件进行相应更新
```

## 蓝图使用

在蓝图中可以通过以下节点使用：
1. `GetSaveModifier` - 获取修改器实例
2. `ModifyPlayerProgress` - 修改玩家进度
3. `ModifyDayClaimedStatus` - 修改天数领取状态
4. `ModifyClaimedDays` - 批量修改已领取天数
5. `ResetPlayerRecord` - 重置玩家记录

## 测试建议

1. **功能测试**：验证修改后的数据显示正确
2. **持久化测试**：重启游戏后检查数据是否保留
3. **兼容性测试**：确保原有功能不受影响
4. **边界测试**：测试无效输入和边界条件

---
*此集成为了在不影响原有业务逻辑的前提下，提供灵活的动态存档数据修改能力*