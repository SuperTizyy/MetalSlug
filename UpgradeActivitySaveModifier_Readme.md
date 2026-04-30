# 活动系统控制台命令手册

本文档包含项目所有活动相关的控制台调试命令，可直接在 Unreal Editor 控制台（`~` 键）或命令行中使用。

---

## 一、每日登录（DailyLogin）控制台命令

> 对应实现：`DailyLoginSaveModifier.cpp`
> 注册时机：通过 `UDailyLoginSaveModifier::RegisterConsoleCommands()` 在运行时注册
> ActivityID 说明：固定为 `101`（对应 DT_ActivityInfoRow 中的每日登录活动 ID）

### 1. DailyLogin.SetProgress
```
DailyLogin.SetProgress [ActivityID] [ProgressValue]
```
- **功能**：设置玩家当前登录进度（用于判断第几天可领取）
- **参数**：
  - ActivityID：活动 ID，固定填 `101`
  - ProgressValue：进度值（表示第 1~N 天可领取）
- **示例**：
```
DailyLogin.SetProgress 101 3   // 设置为第1~3天可领取
DailyLogin.SetProgress 101 8   // 设置为第1~8天全部可领取
```

### 2. DailyLogin.SetDayClaimed
```
DailyLogin.SetDayClaimed [ActivityID] [DayIndex] [IsClaimed]
```
- **功能**：设置某一天的领取状态
- **参数**：
  - ActivityID：活动 ID，固定填 `101`
  - DayIndex：天数索引（从 1 开始）
  - IsClaimed：0 = 未领取，1 = 已领取
- **示例**：
```
DailyLogin.SetDayClaimed 101 1 1   // 设置第1天为已领取
DailyLogin.SetDayClaimed 101 3 0   // 设置第3天为未领取
```

### 3. DailyLogin.Reset
```
DailyLogin.Reset [ActivityID]
```
- **功能**：重置指定活动的所有存档数据（进度、领取状态全部清零）
- **参数**：ActivityID，固定填 `101`
- **示例**：
```
DailyLogin.Reset 101   // 重置每日登录活动数据
```

### 4. DailyLogin.ShowInfo
```
DailyLogin.ShowInfo [ActivityID]
```
- **功能**：打印指定活动的完整存档数据到 Output Log
- **参数**：ActivityID，固定填 `101`
- **示例**：
```
DailyLogin.ShowInfo 101
```

---

## 二、每日升级奖励（Upgrade）控制台命令

> 对应实现：`UpgradeActivitySaveModifier.cpp`
> 注册时机：通过 `UUpgradeActivitySaveModifier::RegisterConsoleCommands()` 在运行时注册
> RecordDate 说明：用整数表示日期，格式为 YYYYMMDD（如 `20240429`），代表某一天的记录

### 1. Upgrade.SetExp
```
Upgrade.SetExp [RecordDate] [ExperienceValue]
```
- **功能**：设置指定日期的经验值，立即生效并触发所有页面刷新
- **参数**：
  - RecordDate：记录日期整数，如 `20240429`
  - ExperienceValue：新的经验值
- **示例**：
```
Upgrade.SetExp 20240429 500    // 设置 2024-04-29 那天的经验为 500
```

### 2. Upgrade.SetCreatedTime
```
Upgrade.SetCreatedTime [RecordDate] [Year] [Month] [Day] [Hour] [Minute] [Second]
```
- **功能**：设置指定日期记录的创建时间，用于测试不同日期阶段的活动状态判定
- **参数**：年 / 月 / 日 / 时 / 分 / 秒均为整数
- **示例**：
```
Upgrade.SetCreatedTime 20240429 2024 4 29 12 0 0
```

### 3. Upgrade.CreateRecord
```
Upgrade.CreateRecord [RecordDate] [InheritPrevious]
```
- **功能**：在 AllRecords 中创建指定日期的新记录
- **参数**：
  - RecordDate：记录日期整数
  - InheritPrevious：0 = 不继承前一天数据，1 = 继承（默认 1）
- **继承规则**：继承经验值和奖励图标索引；宝箱状态和任务状态重置为未完成
- **示例**：
```
Upgrade.CreateRecord 20240430 1    // 创建 2024-04-30 记录，继承前一天数据
Upgrade.CreateRecord 20240501 0    // 创建 2024-05-01 记录，不继承
```

### 4. Upgrade.ShowAllInfo
```
Upgrade.ShowAllInfo
```
- **功能**：打印所有日期记录（按 RecordDate 升序排列）的完整数据，包含经验值、奖励图标、宝箱状态、任务状态、最后更新时间等
- **参数**：无
- **示例**：
```
Upgrade.ShowAllInfo
```

### 5. Upgrade.SetTaskCount
```
Upgrade.SetTaskCount [RecordDate] [TaskIndex] [Count]
```
- **功能**：设置指定日期记录的某个任务的完成次数
- **参数**：
  - RecordDate：记录日期整数
  - TaskIndex：任务索引（从 0 开始）
  - Count：完成次数
- **示例**：
```
Upgrade.SetTaskCount 20240429 0 10    // 设置第0号任务完成10次
Upgrade.SetTaskCount 20240429 1 5     // 设置第1号任务完成5次
```

---

## 三、活动子系统（ActivitySubsystem）作弊函数

> 对应实现：`ActivitySubsystem.cpp` 及 `DailyLoginPage.cpp` 中的 `Cheat_*` 函数
> 调用方式：通过蓝图或 C++ 直接调用

### 1. Cheat_JumpToDay（每日登录作弊跳转）
```
ActivitySub->Cheat_JumpToDay(ActivityID, NewDay)
```
- **功能**：作弊函数，将指定活动的玩家进度设为 NewDay，并自动批量领取第 1~NewDay 天的奖励
- **参数**：
  - ActivityID：活动 ID，固定填 `101`
  - NewDay：目标天数（表示第 1~N 天可领取）
- **示例**（蓝图或 C++ 中调用）：
```
ActivitySubsystem->Cheat_JumpToDay(101, 8);   // 第1~8天全部可领取
```
- **用途**：用于测试每日登录页面 UI 各种天数的显示和领取逻辑

### 2. Cheat_SetDayAndRefresh（UI 刷新跳转）
```
DailyLoginPage->Cheat_SetDayAndRefresh(NewDay)
```
- **功能**：在 `UDailyLoginPage` 中封装的天数跳转函数，内部调用 `Cheat_JumpToDay`，并触发页面列表刷新
- **参数**：NewDay = 目标天数
- **用途**：通过每日登录页面的作弊面板（`WBP_DailyLoginCheatWidget`）输入天数并点击应用按钮来触发

### 3. TryClaimReward（领取奖励）
```
ActivitySub->TryClaimReward(ActivityID, DayIndex)
```
- **功能**：尝试领取指定活动的指定天数奖励
- **参数**：ActivityID（固定 `101`）和 DayIndex（天数，从 1 开始）
- **返回**：true = 领取成功，false = 失败（未到该天或已领取）

### 4. 存档修改器接口（通过 ActivitySubsystem 暴露）
通过 `UActivitySubsystem::GetSaveModifier()` 获取 `UDailyLoginSaveModifier` 实例后，可调用以下蓝图可调用接口：

| 接口 | 功能 |
|---|---|
| `ModifyPlayerProgress(ActivityID, Progress, bAutoSave)` | 修改玩家进度 |
| `ModifyDayClaimedStatus(ActivityID, DayIndex, bClaimed, bAutoSave)` | 修改领取状态 |
| `ModifyClaimedDays(ActivityID, ClaimedDays[], bAutoSave)` | 批量修改已领取天数 |
| `ModifyCurrentClaimCount(ActivityID, NewCount, bAutoSave)` | 修改当前领取次数 |
| `ResetPlayerRecord(ActivityID, bAutoSave)` | 重置玩家记录 |
| `ResetDailyLoginData(ActivityID, bAutoSave)` | 重置每日登录数据 |

---

## 四、升级奖励子系统（UpgradeActivitySubsystem）作弊接口

通过 `UUpgradeActivitySubsystem` 暴露的蓝图可调用接口（需通过 `UUpgradeActivitySaveModifier` 间接调用）：

| 接口 | 功能 |
|---|---|
| `ModifyCurrentExperience(RecordDate, Exp, bAutoSave)` | 修改经验值 |
| `ModifyRewardIconIndex(RecordDate, IconIndex, bAutoSave)` | 修改奖励图标索引 |
| `ModifyChestClaimStatus(RecordDate, ChestIndex, IsClaimed, bAutoSave)` | 修改宝箱领取状态 |
| `ModifyTaskCompleteCount(RecordDate, TaskIndex, Count, bAutoSave)` | 修改任务完成次数 |
| `ModifyTaskClaimStatus(RecordDate, TaskIndex, IsClaimed, bAutoSave)` | 修改任务领取状态 |
| `ModifyLimitedActivityCount(RecordDate, Count, bAutoSave)` | 修改限时活动完成次数 |
| `ResetRecordData(RecordDate, bAutoSave)` | 重置指定日期所有数据 |
| `CreateNewRecord(RecordDate, bInherit, bAutoSave)` | 创建新日期记录 |
| `ShowDailyUpgradePage()` | 弹出每日升级奖励页面（调试用） |

---

## 五、常用调试流程

### 流程一：测试每日登录 UI（第 1~8 天）
```
DailyLogin.Reset 101                 // 先重置数据
DailyLogin.SetProgress 101 8         // 设置为第1~8天全部可领取
// 或通过作弊面板输入天数并点击应用
```

### 流程二：测试每日升级奖励某天的经验值和任务
```
Upgrade.SetExp 20240429 500          // 设置经验值
Upgrade.SetTaskCount 20240429 0 10  // 设置任务0完成10次
Upgrade.ShowAllInfo                  // 查看所有记录状态
```

### 流程三：模拟不同日期的活动状态
```
Upgrade.SetCreatedTime 20240429 2024 4 29 12 0 0   // 设为2024-04-29创建的记录
Upgrade.SetCreatedTime 20240430 2024 4 30 23 59 59 // 设为2024-04-30最后一秒
Upgrade.ShowAllInfo                                     // 观察系统对日期的判定
```

---

## 六、已知限制和注意事项

1. **控制台命令参数错误**：参数不足时会输出使用方法提示到 Output Log，不会崩溃。
2. **Subsystem 未就绪**：如果游戏尚未运行到 ActivitySubsystem 初始化阶段，控制台命令会输出"无法获取 Subsystem 实例"。
3. **存档保存时机**：`DailyLogin.SetProgress` 和 `DailyLogin.SetDayClaimed` 默认自动保存到磁盘；`Upgrade.*` 系列命令仅修改内存数据，在游戏正常退出时自动保存到磁盘。
4. **ActivityID 固定为 101**：所有 `DailyLogin.*` 命令中的 ActivityID 参数在当前版本固定为 `101`。
5. **RecordDate 格式**：所有 `Upgrade.*` 命令使用 YYYYMMDD 整数格式（如 `20240429`），而非"第几天"。

---

## 七、版本信息

- **适用引擎**：Unreal Engine 5.4+
- **平台支持**：Windows 64-bit
- **最后更新**：2026 年 4 月
- **维护者**：MetalSlug01 开发团队

---

*本文档由代码自动生成，如发现命令与实际行为不符，请以源代码为准。*
