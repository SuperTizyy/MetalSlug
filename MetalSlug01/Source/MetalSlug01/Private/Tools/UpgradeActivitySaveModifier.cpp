/**
 * @file UpgradeActivitySaveModifier.cpp
 * @brief 升级活动存档动态修改器实现
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 * 
 * @details 实现升级活动存档动态修改器的核心功能
 *          与UpgradeActivitySubsystem解耦，提供独立的数据修改能力
 */

#include "Tools/UpgradeActivitySaveModifier.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Data/ActivitySaveGame.h"
#include "Systems/Activity/UpgradeActivitySubsystem.h"

// ==================== 结构体实现 ====================

// FUpgradeActivityModificationRecord的构造函数已在头文件中定义

// ==================== 主类实现 ====================

UUpgradeActivitySaveModifier::UUpgradeActivitySaveModifier()
	: Super(), TargetSubsystem(nullptr)
{
	// 父类已初始化 IsInitialized()=false, bHasPendingChanges=false
	// 本类额外清空 TargetSubsystem
}

// 【v232 进程隔离】从 OwningSubsystem 取真正的槽位名 (已含进程 ID 后缀)
FString UUpgradeActivitySaveModifier::GetSaveSlotName(int32 /*ActivityID*/) const
{
	if (TargetSubsystem)
	{
		return TargetSubsystem->SaveSlotName;
	}
	// 零兜底: 找不到子系统 → Log Error → 拒绝静默 fallback
	UE_LOG(LogTemp, Error,
		TEXT("[v232] UpgradeActivitySaveModifier: TargetSubsystem 为空, 无法获取存档槽位."
			 " 修复: 确保 InitializeModifier 已传入有效 Subsystem"));
	return FString(); // 返回空字符串让 SaveGameToSlot 失败, 显式报错而非静默
}

bool UUpgradeActivitySaveModifier::InitializeModifier(UObject* WorldContext, UUpgradeActivitySubsystem* Subsystem)
{
	// 【2026-06-15 修复】: 委托给基类 (基类会实际赋值 WorldContext 并置 IsInitialized()=true)
	if (!Super::InitializeBase(WorldContext))
	{
		return false;
	}

	// 强制映射UpgradeActivitySubsystem内存数据
	UGameInstance* GameInstance = WorldContext->GetWorld()->GetGameInstance();
	if (GameInstance)
	{
		TargetSubsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
		if (TargetSubsystem)
		{
			UE_LOG(LogAccount, Log, TEXT("UpgradeActivitySaveModifier: 成功映射Subsystem内存数据"));
			UE_LOG(LogAccount, Log, TEXT("   Subsystem地址: %p"), TargetSubsystem);
			UE_LOG(LogAccount, Log, TEXT("   当前RecordDate: %d"), TargetSubsystem->GetRecord().GetDayNumber());
		}
		else
		{
			UE_LOG(LogAccount, Error, TEXT("UpgradeActivitySaveModifier: 无法获取UpgradeActivitySubsystem"));
			return false;
		}
	}
	else
	{
		UE_LOG(LogAccount, Error, TEXT("UpgradeActivitySaveModifier: 无法获取GameInstance"));
		return false;
	}

	UE_LOG(LogAccount, Log, TEXT("UpgradeActivitySaveModifier: 内存映射初始化完成 - 运行时只操作内存, 游戏关闭时保存到磁盘"));
	return true;
}

void UUpgradeActivitySaveModifier::DestroyModifier()
{
	if (!IsInitialized())
	{
		return;
	}

	// 保存所有未保存的修改
	SaveAllRecords();

	// 委托给基类清理
	TargetSubsystem = nullptr;
	Super::DestroyBase();

	UE_LOG(LogAccount, Log, TEXT("UpgradeActivitySaveModifier: 已销毁"));
}

bool UUpgradeActivitySaveModifier::ModifyCurrentExperience(int32 RecordDate, int32 NewExp, bool bAutoSave)
{
	if (!ValidateAndLog(TEXT("ModifyCurrentExperience")) || NewExp < 0)
	{
		if (NewExp < 0) UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: 经验值不能为负数: %d"), NewExp);
		return false;
	}

	// 确保记录存在
	const FUpgradeRewardSaveRecord* RecordPtr = TargetSubsystem->GetRecordByDate(RecordDate);
	if (!RecordPtr)
	{
		CreateNewRecord(RecordDate, false, false);
		RecordPtr = TargetSubsystem->GetRecordByDate(RecordDate);
	}
	
	if (!RecordPtr)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 无法获取目标记录"));
		return false;
	}
	
	// 创建一个副本以避免引用问题
	FUpgradeRewardSaveRecord ModifiedRecord = *RecordPtr;
	
	int32 OriginalExp = ModifiedRecord.CurrentExperience;
	ModifiedRecord.CurrentExperience = NewExp;
	ModifiedRecord.LastUpdateTime = FDateTime::Now();

	// 🔧【Bug 1 修复】同步更新 AllRecords 映射表中的对应记录 (与 ModifyRewardIconIndex/ModifyTaskCompleteCount 保持一致)
	TargetSubsystem->AddOrUpdateRecord(RecordDate, ModifiedRecord);

	// 同步更新 CurrentRecord（如果 RecordDate 匹配）
	if (TargetSubsystem->GetRecord().GetDayNumber() == RecordDate)
	{
		TargetSubsystem->GetRecord() = ModifiedRecord;
	}

	LogModification(TEXT("经验值"), FString::Printf(TEXT("%d -> %d"), OriginalExp, NewExp));
	ForceRefreshAllPages();

	if (bAutoSave)
	{
		UE_LOG(LogTemp, Log, TEXT("标记为需要保存到磁盘（游戏关闭时执行）"));
	}

	return true;
}

bool UUpgradeActivitySaveModifier::ModifyRewardIconIndex(int32 RecordDate, int32 NewIndex, bool bAutoSave)
{
	if (!ValidateAndLog(TEXT("ModifyRewardIconIndex")) || NewIndex < 0)
	{
		if (NewIndex < 0) UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: 图标索引不能为负数: %d"), NewIndex);
		return false;
	}

	const FUpgradeRewardSaveRecord* RecordPtr = TargetSubsystem->GetRecordByDate(RecordDate);
	if (!RecordPtr)
	{
		UE_LOG(LogTemp, Error, TEXT("ModifyRewardIconIndex: 记录%d不存在"), RecordDate);
		return false;
	}
		
	// 创建一个副本以避免引用问题
	FUpgradeRewardSaveRecord ModifiedRecord = *RecordPtr;
		
	int32 OriginalIndex = ModifiedRecord.RewardIconIndex;
	ModifiedRecord.RewardIconIndex = NewIndex;
	ModifiedRecord.LastUpdateTime = FDateTime::Now();
		
	TargetSubsystem->AddOrUpdateRecord(RecordDate, ModifiedRecord);
		
	if (TargetSubsystem->GetRecord().GetDayNumber() == RecordDate)
	{
		TargetSubsystem->GetRecord() = ModifiedRecord;
	}
	
	LogModification(TEXT("图标索引"), FString::Printf(TEXT("%d -> %d"), OriginalIndex, NewIndex));
	ForceRefreshAllPages();
	
	if (bAutoSave)
	{
		UE_LOG(LogTemp, Log, TEXT("标记为需要保存到磁盘（游戏关闭时执行）"));
	}
	
	return true;
}

bool UUpgradeActivitySaveModifier::ModifyChestClaimStatus(int32 RecordDate, int32 ChestIndex, int32 IsClaimed, bool bAutoSave)
{
	// 【v228 SSOT 重构】ChestClaimStatus 改为全局真源（跨天共享）
	// 保留 RecordDate 参数仅为签名兼容 - 内部忽略
	// 严禁直接修改 per-day record - 那是反向伤害 (用户 2026.08.13 报告 Bug 2 真因)
	if (!ValidateAndLog(TEXT("ModifyChestClaimStatus")))
	{
		return false;
	}

	// 委派给 Subsystem 的全局接口 - 唯一允许写入 GlobalChestClaimStatus 的入口
	return TargetSubsystem->ModifyGlobalChestClaimStatus(ChestIndex, IsClaimed, bAutoSave);
}

bool UUpgradeActivitySaveModifier::ModifyTaskCompleteCount(int32 RecordDate, int32 TaskIndex, int32 Count, bool bAutoSave)
{
	if (!ValidateAndLog(TEXT("ModifyTaskCompleteCount")) || Count < 0)
	{
		if (Count < 0) UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: 任务完成次数不能为负数: %d"), Count);
		return false;
	}

	const FUpgradeRewardSaveRecord* RecordPtr = TargetSubsystem->GetRecordByDate(RecordDate);
	if (!RecordPtr)
	{
		UE_LOG(LogTemp, Error, TEXT("ModifyTaskCompleteCount: 记录%d不存在"), RecordDate);
		return false;
	}
		
	if (!RecordPtr->TaskCompleteCounts.IsValidIndex(TaskIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("ModifyTaskCompleteCount: 任务索引%d超出范围"), TaskIndex);
		return false;
	}
		
	// 创建一个副本以避免引用问题
	FUpgradeRewardSaveRecord ModifiedRecord = *RecordPtr;
		
	int32 OriginalCount = ModifiedRecord.TaskCompleteCounts[TaskIndex];
	ModifiedRecord.TaskCompleteCounts[TaskIndex] = Count;
	ModifiedRecord.LastUpdateTime = FDateTime::Now();
		
	// 调试日志：记录修改前后的 AllRecords 状态
	UE_LOG(LogTemp, Log, TEXT("DEBUG: ModifyTaskCompleteCount - 修改前 AllRecords 大小: %d"), TargetSubsystem->GetAllRecords().Num());
	for (auto& Pair : TargetSubsystem->GetAllRecords())
	{
		UE_LOG(LogTemp, Log, TEXT("DEBUG: ModifyTaskCompleteCount - RecordDate=%d, TaskCompleteCounts[0]=%d"), 
			Pair.Key, Pair.Value.TaskCompleteCounts.IsValidIndex(0) ? Pair.Value.TaskCompleteCounts[0] : -1);
	}
		
	TargetSubsystem->AddOrUpdateRecord(RecordDate, ModifiedRecord);
		
	// 调试日志：记录修改后的 AllRecords 状态
	UE_LOG(LogTemp, Log, TEXT("DEBUG: ModifyTaskCompleteCount - 修改后 AllRecords 大小: %d"), TargetSubsystem->GetAllRecords().Num());
	for (auto& Pair : TargetSubsystem->GetAllRecords())
	{
		UE_LOG(LogTemp, Log, TEXT("DEBUG: ModifyTaskCompleteCount - RecordDate=%d, TaskCompleteCounts[0]=%d"), 
			Pair.Key, Pair.Value.TaskCompleteCounts.IsValidIndex(0) ? Pair.Value.TaskCompleteCounts[0] : -1);
	}
		
	if (TargetSubsystem->GetRecord().GetDayNumber() == RecordDate)
	{
		TargetSubsystem->GetRecord() = ModifiedRecord;
	}
	
	LogModification(TEXT("任务完成次数"), FString::Printf(TEXT("任务%d [%d] -> [%d]"), TaskIndex, OriginalCount, Count));
	ForceRefreshAllPages();
	
	if (bAutoSave)
	{
		UE_LOG(LogTemp, Log, TEXT("标记为需要保存到磁盘（游戏关闭时执行）"));
	}
	
	return true;
}

bool UUpgradeActivitySaveModifier::ModifyTaskClaimStatus(int32 RecordDate, int32 TaskIndex, int32 IsClaimed, bool bAutoSave)
{
	if (!ValidateAndLog(TEXT("ModifyTaskClaimStatus")) || !ValidateBinaryState(IsClaimed, TEXT("任务状态")))
	{
		return false;
	}

	const FUpgradeRewardSaveRecord* RecordPtr = TargetSubsystem->GetRecordByDate(RecordDate);
	if (!RecordPtr)
	{
		UE_LOG(LogTemp, Error, TEXT("ModifyTaskClaimStatus: 记录%d不存在"), RecordDate);
		return false;
	}
		
	if (!RecordPtr->TaskClaimStatus.IsValidIndex(TaskIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("ModifyTaskClaimStatus: 任务索引%d超出范围"), TaskIndex);
		return false;
	}
		
	// 创建一个副本以避免引用问题
	FUpgradeRewardSaveRecord ModifiedRecord = *RecordPtr;
		
	int32 OriginalStatus = ModifiedRecord.TaskClaimStatus[TaskIndex];
	ModifiedRecord.TaskClaimStatus[TaskIndex] = IsClaimed;
	ModifiedRecord.LastUpdateTime = FDateTime::Now();
		
	TargetSubsystem->AddOrUpdateRecord(RecordDate, ModifiedRecord);
		
	if (TargetSubsystem->GetRecord().GetDayNumber() == RecordDate)
	{
		TargetSubsystem->GetRecord() = ModifiedRecord;
	}
	
	FString StatusText = (IsClaimed == 1) ? TEXT("已领取") : TEXT("未领取");
	FString OriginalText = OriginalStatus ? TEXT("已领取") : TEXT("未领取");
	LogModification(TEXT("任务领取状态"), FString::Printf(TEXT("任务%d [%s] -> [%s]"), TaskIndex, *OriginalText, *StatusText));
	ForceRefreshAllPages();
	
	if (bAutoSave)
	{
		UE_LOG(LogTemp, Log, TEXT("标记为需要保存到磁盘（游戏关闭时执行）"));
	}
	
	return true;
}

bool UUpgradeActivitySaveModifier::ModifyLimitedActivityCount(int32 RecordDate, int32 Count, bool bAutoSave)
{
	if (!ValidateAndLog(TEXT("ModifyLimitedActivityCount")) || Count < 0)
	{
		if (Count < 0) UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: 限时活动次数不能为负数: %d"), Count);
		return false;
	}

	const FUpgradeRewardSaveRecord* RecordPtr = TargetSubsystem->GetRecordByDate(RecordDate);
	if (!RecordPtr)
	{
		UE_LOG(LogTemp, Error, TEXT("ModifyLimitedActivityCount: 记录%d不存在"), RecordDate);
		return false;
	}
		
	// 创建一个副本以避免引用问题
	FUpgradeRewardSaveRecord ModifiedRecord = *RecordPtr;
		
	int32 OriginalCount = ModifiedRecord.LimitedActivityCompleteCount;
	ModifiedRecord.LimitedActivityCompleteCount = Count;
	ModifiedRecord.LastUpdateTime = FDateTime::Now();
		
	TargetSubsystem->AddOrUpdateRecord(RecordDate, ModifiedRecord);
		
	if (TargetSubsystem->GetRecord().GetDayNumber() == RecordDate)
	{
		TargetSubsystem->GetRecord() = ModifiedRecord;
	}
	
	LogModification(TEXT("限时活动次数"), FString::Printf(TEXT("%d -> %d"), OriginalCount, Count));
	ForceRefreshAllPages();
	
	if (bAutoSave)
	{
		UE_LOG(LogTemp, Log, TEXT("标记为需要保存到磁盘（游戏关闭时执行）"));
	}
	
	return true;
}

bool UUpgradeActivitySaveModifier::ResetRecordData(int32 RecordDate, bool bAutoSave)
{
	if (!ValidateAndLog(TEXT("ResetRecordData"))) return false;

	// 直接重置UpgradeActivitySubsystem内存数据
	UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
	UE_LOG(LogTemp, Log, TEXT("🔧 MEMORY_DATA_RESET_START"));
	UE_LOG(LogTemp, Log, TEXT("🆔 Subsystem地址: %p"), TargetSubsystem);
	UE_LOG(LogTemp, Log, TEXT("📊 重置记录数据: RecordDate=%d"), RecordDate);
	UE_LOG(LogTemp, Log, TEXT("⏰ 重置时间: %s"), *FDateTime::Now().ToString());
	UE_LOG(LogTemp, Log, TEXT("运行时模式: 仅内存操作，不写入磁盘"));
	UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
	
	// 保存原始数据用于日志
	int32 OriginalExp = TargetSubsystem->GetRecord().CurrentExperience;
	int32 OriginalIcon = TargetSubsystem->GetRecord().RewardIconIndex;
	
	// 重置数据
	TargetSubsystem->GetRecord().CurrentExperience = 0;
	TargetSubsystem->GetRecord().RewardIconIndex = 0;
	TargetSubsystem->GetRecord().LimitedActivityCompleteCount = 0;

	// 重置数组
	// 【v228 SSOT 重构】ChestClaimStatus 是全局状态，跨天共享，不在此处重置
	for (int32 i = 0; i < MAX_TASK_COUNT && i < TargetSubsystem->GetRecord().TaskCompleteCounts.Num(); ++i)
	{
		TargetSubsystem->GetRecord().TaskCompleteCounts[i] = 0;
		TargetSubsystem->GetRecord().TaskClaimStatus[i] = 0;
	}
	
	// 🔧 修复：同步更新 AllRecords 映射表中的对应记录
	TargetSubsystem->AddOrUpdateRecord(RecordDate, TargetSubsystem->GetRecord());
	
	TargetSubsystem->GetRecord().LastUpdateTime = FDateTime::Now();
	
	UE_LOG(LogTemp, Log, TEXT("📊 重置前后对比:"));
	UE_LOG(LogTemp, Log, TEXT("   经验值: %d -> 0"), OriginalExp);
	UE_LOG(LogTemp, Log, TEXT("   图标索引: %d -> 0"), OriginalIcon);
	
	// 强制刷新所有页面，重新获取内存数据
	ForceRefreshAllPages();
	
	// 游戏关闭时才会保存到磁盘
	if (bAutoSave)
	{
		UE_LOG(LogTemp, Log, TEXT("标记为需要保存到磁盘（游戏关闭时执行）"));
	}
	
	return true;
}

bool UUpgradeActivitySaveModifier::CreateNewRecord(int32 RecordDate, bool bInheritPrevious, bool bAutoSave)
{
	if (!ValidateAndLog(TEXT("CreateNewRecord"))) return false;

	// 直接操作UpgradeActivitySubsystem内存数据
	UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
	UE_LOG(LogTemp, Log, TEXT("🔧 MEMORY_DATA_CREATION_START"));
	UE_LOG(LogTemp, Log, TEXT("🆔 Subsystem地址: %p"), TargetSubsystem);
	UE_LOG(LogTemp, Log, TEXT("📊 创建新记录: RecordDate=%d"), RecordDate);
	UE_LOG(LogTemp, Log, TEXT("⏰ 创建时间: %s"), *FDateTime::Now().ToString());
	UE_LOG(LogTemp, Log, TEXT("运行时模式: 仅内存操作，不写入磁盘"));
	UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));

	// 检查是否已存在
	UActivitySaveGame* SaveGame = TargetSubsystem->GetSaveGameInstance();
	if (SaveGame && SaveGame->UpgradeRewardRecords.Contains(RecordDate))
	{
		UE_LOG(LogTemp, Warning, TEXT("⚠️ UpgradeActivitySaveModifier: 记录%d已存在"), RecordDate);
		return false;
	}

	FUpgradeRewardSaveRecord NewRecord;
	NewRecord.SetRecordDate(RecordDate);

	// 初始化默认值
	NewRecord.CurrentExperience = 0;
	NewRecord.RewardIconIndex = 0;
	NewRecord.LimitedActivityCompleteCount = 0;
	
	// 🔧 根据 ActivityID=102 且 DayIdentifier="day1"的 GameModes 数量动态确定数组长度
	const FDailyUpgradeRewardConfigRow* ExtraConfig = TargetSubsystem->GetExtraConfigForDay1();
	int32 TaskCount = MAX_TASK_COUNT; // 默认值
	if (ExtraConfig && ExtraConfig->GameModes.Num() > 0)
	{
		TaskCount = ExtraConfig->GameModes.Num();
		UE_LOG(LogTemp, Log, TEXT("📊 使用 ActivityID=102 day1 的 GameModes 数量: %d"), TaskCount);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("⚠️ 未找到 ActivityID=102 day1 的配置，使用默认任务数量: %d"), TaskCount);
	}
	
	// 初始化数组为默认值（不继承前一天的任务数据）
	// 【v228 SSOT 重构】ChestClaimStatus 不再属于 per-day record, 移到 SaveGame->GlobalChestClaimStatus
	// 不再初始化 NewRecord.ChestClaimStatus - 保留旧字段仅为兼容老存档反序列化
	NewRecord.TaskCompleteCounts.SetNumZeroed(TaskCount);
	NewRecord.TaskClaimStatus.SetNumZeroed(TaskCount);

	// 🔧 明确设置所有任务数据为 0，确保不会继承
	for (int32 i = 0; i < TaskCount; ++i)
	{
		NewRecord.TaskCompleteCounts[i] = 0;
		NewRecord.TaskClaimStatus[i] = 0;
	}
	// 【v228 SSOT 重构】不再初始化 ChestClaimStatus - 已移至 GlobalChestClaimStatus (跨天共享)
	
	UE_LOG(LogTemp, Log, TEXT("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"));
	UE_LOG(LogTemp, Log, TEXT("【Upgrade.CreateRecord - 初始化后验证】刚创建时的任务数据:"));
	UE_LOG(LogTemp, Log, TEXT("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"));
	UE_LOG(LogTemp, Log, TEXT("📊 TaskCompleteCounts 数组内容 (应该全为 0):"));
	for (int32 i = 0; i < NewRecord.TaskCompleteCounts.Num(); ++i)
	{
		UE_LOG(LogTemp, Log, TEXT("    [%d] = %d"), i, NewRecord.TaskCompleteCounts[i]);
	}
	UE_LOG(LogTemp, Log, TEXT("📊 TaskClaimStatus 数组内容 (应该全为 0):"));
	for (int32 i = 0; i < NewRecord.TaskClaimStatus.Num(); ++i)
	{
		UE_LOG(LogTemp, Log, TEXT("    [%d] = %d"), i, NewRecord.TaskClaimStatus[i]);
	}

	if (bInheritPrevious && RecordDate > 1 && SaveGame)
	{
		// 尝试继承前一天的数据
		const FUpgradeRewardSaveRecord* PreviousRecord = SaveGame->UpgradeRewardRecords.Find(RecordDate - 1);
		if (PreviousRecord)
		{
			UE_LOG(LogTemp, Log, TEXT("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"));
			UE_LOG(LogTemp, Log, TEXT("【Upgrade.CreateRecord - 继承检查】第%d天 → 第%d天"), RecordDate - 1, RecordDate);
			UE_LOG(LogTemp, Log, TEXT("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"));
			
			// 显示前一天的数据
			UE_LOG(LogTemp, Log, TEXT("  📋 前一天 (第%d天) 的数据:"), RecordDate - 1);
			UE_LOG(LogTemp, Log, TEXT("    经验值：%d"), PreviousRecord->CurrentExperience);
			UE_LOG(LogTemp, Log, TEXT("    图标索引：%d"), PreviousRecord->RewardIconIndex);
			UE_LOG(LogTemp, Log, TEXT("    TaskCompleteCounts:"));
			for (int32 i = 0; i < PreviousRecord->TaskCompleteCounts.Num(); ++i)
			{
				UE_LOG(LogTemp, Log, TEXT("      [%d] = %d"), i, PreviousRecord->TaskCompleteCounts[i]);
			}
			UE_LOG(LogTemp, Log, TEXT("    TaskClaimStatus:"));
			for (int32 i = 0; i < PreviousRecord->TaskClaimStatus.Num(); ++i)
			{
				UE_LOG(LogTemp, Log, TEXT("      [%d] = %d"), i, PreviousRecord->TaskClaimStatus[i]);
			}
			
			// 只继承经验值和奖励图标索引，不继承任务数据
			NewRecord.CurrentExperience = PreviousRecord->CurrentExperience;
			NewRecord.RewardIconIndex = PreviousRecord->RewardIconIndex;
			
			UE_LOG(LogTemp, Log, TEXT("\n  ✅ 继承操作:"));
			UE_LOG(LogTemp, Log, TEXT("    ✓ 经验值：%d"), NewRecord.CurrentExperience);
			UE_LOG(LogTemp, Log, TEXT("    ✓ 图标索引：%d"), NewRecord.RewardIconIndex);
			UE_LOG(LogTemp, Log, TEXT("    ✗ 任务完成数：保持为 0 (不继承)"));
			UE_LOG(LogTemp, Log, TEXT("    ✗ 任务领取状态：保持为 0 (不继承)"));
		}
	}

	NewRecord.LastUpdateTime = FDateTime::Now();
	
	UE_LOG(LogTemp, Log, TEXT("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"));
	UE_LOG(LogTemp, Log, TEXT("【Upgrade.CreateRecord - 最终验证】准备写入 SaveGame 前的完整数据:"));
	UE_LOG(LogTemp, Log, TEXT("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"));
	UE_LOG(LogTemp, Log, TEXT("  📊 基础数据:"));
	UE_LOG(LogTemp, Log, TEXT("    RecordDate: %d"), NewRecord.RecordDate);
	UE_LOG(LogTemp, Log, TEXT("    经验值：%d"), NewRecord.CurrentExperience);
	UE_LOG(LogTemp, Log, TEXT("    图标索引：%d"), NewRecord.RewardIconIndex);
	UE_LOG(LogTemp, Log, TEXT("  📊 TaskCompleteCounts (必须全为 0):"));
	for (int32 i = 0; i < NewRecord.TaskCompleteCounts.Num(); ++i)
	{
		UE_LOG(LogTemp, Log, TEXT("    [%d] = %d"), i, NewRecord.TaskCompleteCounts[i]);
	}
	UE_LOG(LogTemp, Log, TEXT("  📊 TaskClaimStatus (必须全为 0):"));
	for (int32 i = 0; i < NewRecord.TaskClaimStatus.Num(); ++i)
	{
		UE_LOG(LogTemp, Log, TEXT("    [%d] = %d"), i, NewRecord.TaskClaimStatus[i]);
	}
	UE_LOG(LogTemp, Log, TEXT("  📊 ChestClaimStatus (v228: 已移至 GlobalChestClaimStatus, 此字段保留仅兼容老存档):"));
	UE_LOG(LogTemp, Log, TEXT("    GlobalChestClaimStatus 大小=%d"), TargetSubsystem->GetGlobalChestClaimStatus().Num());
	UE_LOG(LogTemp, Log, TEXT("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"));
	
	// 更新Subsystem中的记录
	if (!SaveGame)
	{
		SaveGame = NewObject<UActivitySaveGame>();
	}
	SaveGame->UpgradeRewardRecords.Add(RecordDate, NewRecord);
	
	// 同时更新 TargetSubsystem 的 CurrentRecord
	TargetSubsystem->GetRecord() = NewRecord;
		
	UE_LOG(LogTemp, Log, TEXT("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"));
	UE_LOG(LogTemp, Log, TEXT("【Upgrade.CreateRecord - 写入后验证】SaveGame.UpgradeRewardRecords 中的数据:"));
	UE_LOG(LogTemp, Log, TEXT("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"));
	const FUpgradeRewardSaveRecord& RecordRef = SaveGame->UpgradeRewardRecords[RecordDate];
	UE_LOG(LogTemp, Log, TEXT("  📊 TaskCompleteCounts:"));
	for (int32 i = 0; i < RecordRef.TaskCompleteCounts.Num(); ++i)
	{
		UE_LOG(LogTemp, Log, TEXT("    [%d] = %d"), i, RecordRef.TaskCompleteCounts[i]);
	}
	UE_LOG(LogTemp, Log, TEXT("  📊 TaskClaimStatus:"));
	for (int32 i = 0; i < RecordRef.TaskClaimStatus.Num(); ++i)
	{
		UE_LOG(LogTemp, Log, TEXT("    [%d] = %d"), i, RecordRef.TaskClaimStatus[i]);
	}
	UE_LOG(LogTemp, Log, TEXT("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"));
	
	// 🔧 修复：将新记录添加到 AllRecords 表格中
	TargetSubsystem->AddOrUpdateRecord(RecordDate, NewRecord);
	
	UE_LOG(LogTemp, Log, TEXT("✅ 成功创建新记录 RecordDate=%d"), RecordDate);
	UE_LOG(LogTemp, Log, TEXT("🔄 已同步更新TargetSubsystem的CurrentRecord"));
	UE_LOG(LogTemp, Log, TEXT("✅ 新记录已添加到 AllRecords 表格 [%d]"), RecordDate);
	
	// 强制刷新所有页面，重新获取内存数据
	ForceRefreshAllPages();
	
	// 游戏关闭时才会保存到磁盘
	if (bAutoSave)
	{
		UE_LOG(LogTemp, Log, TEXT("标记为需要保存到磁盘（游戏关闭时执行）"));
	}

	return true;
}

// ==================== 查询接口实现 ====================

const FUpgradeRewardSaveRecord* UUpgradeActivitySaveModifier::GetRecordOrNull(int32 RecordDate) const
{
	if (!IsInitialized() || !CachedSaveGame)
	{
		return nullptr;
	}
	return CachedSaveGame->UpgradeRewardRecords.Find(RecordDate);
}

int32 UUpgradeActivitySaveModifier::GetCurrentExperience(int32 RecordDate) const
{
	const FUpgradeRewardSaveRecord* Record = GetRecordOrNull(RecordDate);
	return Record ? Record->CurrentExperience : 0;
}

int32 UUpgradeActivitySaveModifier::GetRewardIconIndex(int32 RecordDate) const
{
	const FUpgradeRewardSaveRecord* Record = GetRecordOrNull(RecordDate);
	return Record ? Record->RewardIconIndex : 0;
}

int32 UUpgradeActivitySaveModifier::GetChestClaimStatus(int32 RecordDate, int32 ChestIndex) const
{
	// 【v228 SSOT 重构】ChestClaimStatus 是全局状态, 跨天共享, 必须从 Subsystem 读取全局真源
	// RecordDate 参数已被废弃, 内部忽略
	if (!TargetSubsystem)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UUpgradeActivitySaveModifier] GetChestClaimStatus: TargetSubsystem 为空, 无法读取全局 ChestClaimStatus"));
		return 0;
	}
	const TArray<int32>& GlobalChestStatus = TargetSubsystem->GetGlobalChestClaimStatus();
	return GlobalChestStatus.IsValidIndex(ChestIndex) ? GlobalChestStatus[ChestIndex] : 0;
}

int32 UUpgradeActivitySaveModifier::GetTaskCompleteCount(int32 RecordDate, int32 TaskIndex) const
{
	const FUpgradeRewardSaveRecord* Record = GetRecordOrNull(RecordDate);
	return (Record && Record->TaskCompleteCounts.IsValidIndex(TaskIndex)) ? Record->TaskCompleteCounts[TaskIndex] : 0;
}

int32 UUpgradeActivitySaveModifier::GetTaskClaimStatus(int32 RecordDate, int32 TaskIndex) const
{
	const FUpgradeRewardSaveRecord* Record = GetRecordOrNull(RecordDate);
	return (Record && Record->TaskClaimStatus.IsValidIndex(TaskIndex)) ? Record->TaskClaimStatus[TaskIndex] : 0;
}

int32 UUpgradeActivitySaveModifier::GetLimitedActivityCount(int32 RecordDate) const
{
	const FUpgradeRewardSaveRecord* Record = GetRecordOrNull(RecordDate);
	return Record ? Record->LimitedActivityCompleteCount : 0;
}

// ==================== 保存接口实现 ====================

bool UUpgradeActivitySaveModifier::SaveRecord(int32 RecordDate)
{
	if (!IsInitialized() || !CachedSaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 无法保存，存档实例无效"));
		return false;
	}

	// 🔧 重要：游戏运行过程中不保存到磁盘，只在内存中修改
	// 实际保存将在游戏关闭时执行
	UE_LOG(LogTemp, Log, TEXT("📝 数据已标记为需要保存 - RecordDate=%d（游戏关闭时执行）"), RecordDate);
	
	// 设置脏标记，表示需要保存
	bHasPendingChanges = true;
	
	return true;
}

bool UUpgradeActivitySaveModifier::SaveAllRecords()
{
	if (!IsInitialized() || !CachedSaveGame)
	{
		return false;
	}

	bool bAllSuccess = true;

	// 🔧 这是唯一的实际磁盘保存操作
	FString SaveSlotName = GetSaveSlotName(0);
	bool bSuccess = UGameplayStatics::SaveGameToSlot(CachedSaveGame, SaveSlotName, 0);

	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("✅ UpgradeActivitySaveModifier: 成功保存所有记录到磁盘（游戏关闭）"));
		bHasPendingChanges = false; // 清除脏标记
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 保存所有记录失败"));
		bAllSuccess = false;
	}

	return bAllSuccess;
}

void UUpgradeActivitySaveModifier::SavePendingChangesOnShutdown()
{
	if (!IsInitialized() || !CachedSaveGame)
	{
		UE_LOG(LogTemp, Warning, TEXT("⚠️ SavePendingChangesOnShutdown: 存档实例无效，跳过保存"));
		return;
	}
	
	if (!bHasPendingChanges)
	{
		UE_LOG(LogTemp, Log, TEXT("📝 SavePendingChangesOnShutdown: 没有待处理的更改，跳过保存"));
		return;
	}
	
	// 🔧 这是唯一的实际磁盘保存操作
	UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
	UE_LOG(LogTemp, Log, TEXT("💾 GAME_SHUTDOWN_SAVE_DATA_START"));
	UE_LOG(LogTemp, Log, TEXT("🆔 Subsystem 地址：%p"), TargetSubsystem);
	UE_LOG(LogTemp, Log, TEXT("📊 保存所有待处理的更改到磁盘"));
	UE_LOG(LogTemp, Log, TEXT("⏰ 保存时间：%s"), *FDateTime::Now().ToString());
	UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
	
	SaveAllRecords();
	
	UE_LOG(LogTemp, Log, TEXT("\n✅ 游戏数据保存完成 - 下次启动时将加载最新数据"));
}

bool UUpgradeActivitySaveModifier::LoadRecord(int32 RecordDate)
{
	if (!IsInitialized())
	{
		return false;
	}

	// 使用与Subsystem一致的存档槽位
	FString SaveSlotName = GetSaveSlotName(0);
	UActivitySaveGame* LoadedSaveGame = Cast<UActivitySaveGame>(
		UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0)
	);

	if (LoadedSaveGame)
	{
		CachedSaveGame = LoadedSaveGame;
		UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 成功加载RecordDate=%d的记录"), RecordDate);
		return true;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: 未找到RecordDate=%d的记录，将创建新记录"), RecordDate);
		CachedSaveGame = GetOrCreateSaveGame(RecordDate);
		return CachedSaveGame != nullptr;
	}
}

// ==================== 内部方法实现 ====================

UActivitySaveGame* UUpgradeActivitySaveModifier::GetOrCreateSaveGame(int32 RecordDate)
{
	if (CachedSaveGame)
	{
		return CachedSaveGame;
	}

	// 尝试加载现有存档 - 使用与Subsystem一致的存档槽位
	FString SaveSlotName = GetSaveSlotName(0);
	UActivitySaveGame* LoadedSaveGame = Cast<UActivitySaveGame>(
		UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0)
	);

	if (!LoadedSaveGame)
	{
		// 创建新的存档
		LoadedSaveGame = Cast<UActivitySaveGame>(
			UGameplayStatics::CreateSaveGameObject(UActivitySaveGame::StaticClass())
		);

		if (LoadedSaveGame)
		{
			FUpgradeRewardSaveRecord NewRecord;
			InitializeNewRecord(NewRecord, RecordDate);
			LoadedSaveGame->UpgradeRewardRecords.Add(RecordDate, NewRecord);

			// 🔧 重要：游戏运行过程中不保存到磁盘
			// 数据已经在内存中，游戏关闭时会统一保存
			UE_LOG(LogTemp, Log, TEXT("📝 新记录已创建在内存中 - RecordDate=%d（游戏关闭时保存）"), RecordDate);
		}
	}

	CachedSaveGame = LoadedSaveGame;
	return LoadedSaveGame;
}

// ==================== 控台命令实现 ====================

void UUpgradeActivitySaveModifier::RegisterConsoleCommands()
{
	if (!GEngine)
	{
		return;
	}

	// 注册控制台命令处理器
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Upgrade.SetExp"),
		TEXT("设置经验值: Upgrade.SetExp RecordDate ExperienceValue"),
		FConsoleCommandWithArgsDelegate::CreateLambda([WorldContext = GetWorldContext(), this](const TArray<FString>& Args)
		{
			if (Args.Num() >= 2)
			{
				int32 RecordDate = FCString::Atoi(*Args[0]);
				int32 Exp = FCString::Atoi(*Args[1]);
										
				// 直接使用已初始化的 TargetSubsystem
				if (this->TargetSubsystem)
				{
					// 获取指定 RecordDate 的记录
					const FUpgradeRewardSaveRecord* RecordPtr = this->TargetSubsystem->GetRecordByDate(RecordDate);
					FUpgradeRewardSaveRecord ModifiedRecord;
										
					if (RecordPtr)
					{
						// 使用现有记录
						ModifiedRecord = *RecordPtr;
					}
					else
					{
						// 创建新记录
						ModifiedRecord.SetRecordDate(RecordDate);
						ModifiedRecord.CurrentExperience = 0;
						ModifiedRecord.RewardIconIndex = 0;
						ModifiedRecord.LimitedActivityCompleteCount = 0;
						// 【v228 SSOT 重构】ChestClaimStatus 已迁至 GlobalChestClaimStatus, 不再初始化 per-day record 字段
						ModifiedRecord.TaskCompleteCounts.SetNumZeroed(MAX_TASK_COUNT);
						ModifiedRecord.TaskClaimStatus.SetNumZeroed(MAX_TASK_COUNT);
					}
										
					int32 OriginalExp = ModifiedRecord.CurrentExperience;
					ModifiedRecord.CurrentExperience = Exp;
					ModifiedRecord.LastUpdateTime = FDateTime::Now();
																						
					// 同步更新 AllRecords
					this->TargetSubsystem->AddOrUpdateRecord(RecordDate, ModifiedRecord);
											
					// 如果修改的是当前记录，同步更新 CurrentRecord
					if (this->TargetSubsystem->GetRecord().GetDayNumber() == RecordDate)
					{
						this->TargetSubsystem->GetRecord() = ModifiedRecord;
					}
											
					UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
					UE_LOG(LogTemp, Log, TEXT("[SET_EXP_DEBUG] MEMORY_DATA_MODIFICATION_START"));
					UE_LOG(LogTemp, Log, TEXT("[SET_EXP_DEBUG] Subsystem地址: %p"), this->TargetSubsystem);
					UE_LOG(LogTemp, Log, TEXT("[SET_EXP_DEBUG] 经验值修改: %d -> %d"), OriginalExp, Exp);
					UE_LOG(LogTemp, Log, TEXT("[SET_EXP_DEBUG] 修改时间: %s"), *FDateTime::Now().ToString());
					UE_LOG(LogTemp, Log, TEXT("[SET_EXP_DEBUG] 运行时模式: 仅内存操作，不写入磁盘"));
					UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
											
					// 强制刷新所有页面，重新获取内存数据
					ForceRefreshAllPages();
											
					// 游戏关闭时才会保存到磁盘
					UE_LOG(LogTemp, Log, TEXT("[SET_EXP_DEBUG] 标记为需要保存到磁盘（游戏关闭时执行）"));
											
					UE_LOG(LogTemp, Log, TEXT("[SET_EXP_DEBUG] Upgrade控制台: 设置经验值 RecordDate=%d Exp=%d 成功"), RecordDate, Exp);
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("[SET_EXP_DEBUG] Upgrade控制台: 无法获取Subsystem实例"));
				}

			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[SET_EXP_DEBUG] Upgrade控制台: 用法 - Upgrade.SetExp RecordDate ExperienceValue"));
			}
		}),
		ECVF_Default
	);
	
		// 注册设置 CreatedTime 的控制台命令
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Upgrade.SetCreatedTime"),
			TEXT("设置创建时间: Upgrade.SetCreatedTime RecordDate Year Month Day Hour Minute Second"),
			FConsoleCommandWithArgsDelegate::CreateLambda([WorldContext = GetWorldContext(), this](const TArray<FString>& Args)
			{
				if (Args.Num() >= 7)
				{
					int32 RecordDate = FCString::Atoi(*Args[0]);
					int32 Year = FCString::Atoi(*Args[1]);
					int32 Month = FCString::Atoi(*Args[2]);
					int32 Day = FCString::Atoi(*Args[3]);
					int32 Hour = FCString::Atoi(*Args[4]);
					int32 Minute = FCString::Atoi(*Args[5]);
					int32 Second = FCString::Atoi(*Args[6]);
									
					// 直接使用已初始化的 TargetSubsystem
					if (this->TargetSubsystem)
					{
						// 获取指定 RecordDate 的记录
						const FUpgradeRewardSaveRecord* RecordPtr = this->TargetSubsystem->GetRecordByDate(RecordDate);
						FUpgradeRewardSaveRecord ModifiedRecord;
										
						if (RecordPtr)
						{
							// 使用现有记录
							ModifiedRecord = *RecordPtr;
						}
						else
						{
							// 创建新记录
							ModifiedRecord.SetRecordDate(RecordDate);
							ModifiedRecord.CurrentExperience = 0;
							ModifiedRecord.RewardIconIndex = 0;
							ModifiedRecord.LimitedActivityCompleteCount = 0;
							// 【v228 SSOT 重构】ChestClaimStatus 已迁至 GlobalChestClaimStatus, 不再初始化 per-day record 字段
							ModifiedRecord.TaskCompleteCounts.SetNumZeroed(MAX_TASK_COUNT);
							ModifiedRecord.TaskClaimStatus.SetNumZeroed(MAX_TASK_COUNT);
						}
										
						// 设置新的 CreatedTime
						FDateTime NewCreatedTime(Year, Month, Day, Hour, Minute, Second);
						ModifiedRecord.CreatedTime = NewCreatedTime;
						ModifiedRecord.LastUpdateTime = FDateTime::Now();
										
						// 同步更新 AllRecords
						this->TargetSubsystem->AddOrUpdateRecord(RecordDate, ModifiedRecord);
										
						// 如果修改的是当前记录，同步更新 CurrentRecord
						if (this->TargetSubsystem->GetRecord().GetDayNumber() == RecordDate)
						{
							this->TargetSubsystem->GetRecord() = ModifiedRecord;
						}
										
						UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
						UE_LOG(LogTemp, Log, TEXT("[SET_CREATED_TIME_DEBUG] MEMORY_DATA_MODIFICATION_START"));
						UE_LOG(LogTemp, Log, TEXT("[SET_CREATED_TIME_DEBUG] Subsystem地址: %p"), this->TargetSubsystem);
						UE_LOG(LogTemp, Log, TEXT("[SET_CREATED_TIME_DEBUG] 创建时间修改: %s -> %s"), 
							*ModifiedRecord.CreatedTime.ToString(), *NewCreatedTime.ToString());
						UE_LOG(LogTemp, Log, TEXT("[SET_CREATED_TIME_DEBUG] 修改时间: %s"), *FDateTime::Now().ToString());
						UE_LOG(LogTemp, Log, TEXT("[SET_CREATED_TIME_DEBUG] 运行时模式: 仅内存操作，不写入磁盘"));
						UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
										
						// 强制刷新所有页面，重新获取内存数据
						ForceRefreshAllPages();
										
						// 游戏关闭时才会保存到磁盘
						UE_LOG(LogTemp, Log, TEXT("[SET_CREATED_TIME_DEBUG] 标记为需要保存到磁盘（游戏关闭时执行）"));
										
						UE_LOG(LogTemp, Log, TEXT("[SET_CREATED_TIME_DEBUG] Upgrade控制台: 设置创建时间 RecordDate=%d 成功"), RecordDate);
					}
					else
					{
						UE_LOG(LogTemp, Error, TEXT("[SET_CREATED_TIME_DEBUG] Upgrade控制台: 无法获取Subsystem实例"));
					}
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[SET_CREATED_TIME_DEBUG] Upgrade控制台: 用法 - Upgrade.SetCreatedTime RecordDate Year Month Day Hour Minute Second"));
				}
			}),
			ECVF_Default
		);

	

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Upgrade.CreateRecord"),
		TEXT("创建新记录: Upgrade.CreateRecord RecordDate [InheritPrevious=1]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([WorldContext = GetWorldContext(), this](const TArray<FString>& Args)
		{
			if (Args.Num() >= 1)
			{
				int32 RecordDate = FCString::Atoi(*Args[0]);
				bool bInherit = Args.Num() >= 2 ? (FCString::Atoi(*Args[1]) != 0) : true;
								
				// 通过 GameInstance 获取最新的 Subsystem 实例
				// 注意: GetWorldContext() 返回原始 UObject*, 因此用 nullptr 判空, 不可调用 .IsValid()
				if (WorldContext)
				{
					UWorld* World = WorldContext->GetWorld();
					if (World)
					{
						UGameInstance* GameInstance = World->GetGameInstance();
						if (GameInstance)
						{
							UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
							if (Subsystem)
							{
								// 🔧 直接调用 CreateNewRecord 函数
								UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
								UE_LOG(LogTemp, Log, TEXT("🔧 MEMORY_DATA_CREATION_START"));
								UE_LOG(LogTemp, Log, TEXT("🆔 Subsystem 地址：%p"), Subsystem);
								UE_LOG(LogTemp, Log, TEXT("📊 创建新记录：RecordDate=%d"), RecordDate);
								UE_LOG(LogTemp, Log, TEXT("⏰ 创建时间：%s"), *FDateTime::Now().ToString());
								UE_LOG(LogTemp, Log, TEXT("运行时模式：仅内存操作，不写入磁盘"));
								UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
								
								bool bSuccess = CreateNewRecord(RecordDate, bInherit, false);
								
								ForceRefreshAllPages();
								
								UE_LOG(LogTemp, Log, TEXT("✅ Upgrade 控制台：创建记录 RecordDate=%d Inherit=%s 成功"), RecordDate, bInherit ? TEXT("是") : TEXT("否"));
								return;
							}
						}
					}
				}
								
				UE_LOG(LogTemp, Error, TEXT("Upgrade控制台: 无法获取Subsystem实例"));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Upgrade控制台: 用法 - Upgrade.CreateRecord RecordDate [InheritPrevious=1]"));
			}
		}),
		ECVF_Default
	);

	
	
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Upgrade.ShowAllInfo"),
		TEXT("显示所有天数记录信息: Upgrade.ShowAllInfo"),
		FConsoleCommandWithArgsDelegate::CreateLambda([WorldContext = GetWorldContext(), this](const TArray<FString>& Args)
		{
			// 直接使用已初始化的 TargetSubsystem
			if (this->TargetSubsystem)
			{
				// 显示所有记录信息（内存优先模式）
				UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
				UE_LOG(LogTemp, Log, TEXT("ALL_RECORDS_MEMORY_FIRST_DISPLAY_START"));
				UE_LOG(LogTemp, Log, TEXT("Subsystem地址: %p"), this->TargetSubsystem);
				UE_LOG(LogTemp, Log, TEXT("📊 显示所有记录信息（内存优先）"));
				UE_LOG(LogTemp, Log, TEXT("⏰ 显示时间: %s"), *FDateTime::Now().ToString());
				UE_LOG(LogTemp, Log, TEXT("💾 运行时模式: 内存数据优先，游戏关闭时保存到磁盘"));
				UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
									
				// 🔧 显示 AllRecords 表格中的所有数据
				const TMap<int32, FUpgradeRewardSaveRecord>& AllRecords = this->TargetSubsystem->GetAllRecords();
				if (AllRecords.Num() == 0)
				{
					UE_LOG(LogTemp, Warning, TEXT("⚠️ AllRecords 表格为空，没有任何记录"));
					return;
				}
									
				// 按 RecordDate 排序
				TArray<int32> SortedDates;
				for (auto& Pair : AllRecords)
				{
					SortedDates.Add(Pair.Key);
				}
				SortedDates.Sort();
									
				// 查找当前内存中的经验值属于哪一天
				int32 CurrentRecordDate = this->TargetSubsystem->GetRecord().GetDayNumber();
				int32 CurrentExperience = this->TargetSubsystem->GetRecord().CurrentExperience;
				UE_LOG(LogTemp, Log, TEXT("💡 当前内存经验值: %d (来自第%d天记录)"), CurrentExperience, CurrentRecordDate);
				UE_LOG(LogTemp, Log, TEXT(""));
									
				// 显示每条记录的详细信息
				for (int32 RecordDate : SortedDates)
				{
					const FUpgradeRewardSaveRecord* Record = this->TargetSubsystem->GetRecordByDate(RecordDate);
					if (Record)
					{
						UE_LOG(LogTemp, Log, TEXT("\n--- 第%d天记录 (RecordDate=%d) ---"), RecordDate, RecordDate);
						UE_LOG(LogTemp, Log, TEXT("📅 创建时间：%s"), *Record->CreatedTime.ToString());
						UE_LOG(LogTemp, Log, TEXT("⏰ 最后更新：%s"), *Record->LastUpdateTime.ToString());
						UE_LOG(LogTemp, Log, TEXT("📊 当前经验值：%d"), Record->CurrentExperience);
						UE_LOG(LogTemp, Log, TEXT("🎁 奖励图标索引：%d"), Record->RewardIconIndex);
						
// 显示宝箱状态 - 【v228 SSOT 重构】ChestClaimStatus 是全局状态, 跨天共享, 只显示一次
					{
						FString ChestStatus = TEXT("📦 全局宝箱领取状态 (跨天共享): ");
						const TArray<int32>& GlobalChestStatus5 = this->TargetSubsystem->GetGlobalChestClaimStatus();
						for (int32 i = 0; i < GlobalChestStatus5.Num() && i < MAX_CHEST_COUNT; ++i)
						{
							FString StatusText = (GlobalChestStatus5[i] == 1) ? TEXT("已领取") : TEXT("未领取");
							ChestStatus += FString::Printf(TEXT("[%d]=%s "), i, *StatusText);
						}
						UE_LOG(LogTemp, Log, TEXT("%s"), *ChestStatus);
					}
						
						// 显示任务状态
						UE_LOG(LogTemp, Log, TEXT("📝 任务完成情况:"));
						for (int32 i = 0; i < Record->TaskCompleteCounts.Num() && i < MAX_TASK_COUNT; ++i)
						{
							FString ClaimStatus = (Record->TaskClaimStatus[i] == 1) ? TEXT("已领取") : TEXT("未领取");
							UE_LOG(LogTemp, Log, TEXT("   任务 [%d]: 完成%d次，奖励%s"), i, Record->TaskCompleteCounts[i], *ClaimStatus);
						}
					}
				}
				
					UE_LOG(LogTemp, Log, TEXT("\n====================================="));
				UE_LOG(LogTemp, Log, TEXT("修改器状态：已启用热数据连接"));
				UE_LOG(LogTemp, Log, TEXT("所有记录显示完成"));
				UE_LOG(LogTemp, Log, TEXT("====================================="));
				return;
			}
		}),
		ECVF_Default
	);

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Upgrade.SetTaskCount"),
		TEXT("设置任务完成次数: Upgrade.SetTaskCount RecordDate TaskIndex Count"),
		FConsoleCommandWithArgsDelegate::CreateLambda([WorldContext = GetWorldContext(), this](const TArray<FString>& Args)
		{
			if (Args.Num() >= 3)
			{
				int32 RecordDate = FCString::Atoi(*Args[0]);
				int32 TaskIndex = FCString::Atoi(*Args[1]);
				int32 Count = FCString::Atoi(*Args[2]);
												
				// 直接使用已初始化的 TargetSubsystem
				if (this->TargetSubsystem)
				{
					// 获取目标记录
					const FUpgradeRewardSaveRecord* RecordPtr = this->TargetSubsystem->GetRecordByDate(RecordDate);
					if (!RecordPtr)
					{
						UE_LOG(LogTemp, Error, TEXT("Upgrade.SetTaskCount: 记录%d不存在"), RecordDate);
						return;
					}
					
					if (!RecordPtr->TaskCompleteCounts.IsValidIndex(TaskIndex))
					{
						UE_LOG(LogTemp, Error, TEXT("Upgrade.SetTaskCount: 任务索引%d超出范围"), TaskIndex);
						return;
					}
					
					// 创建一个副本以避免引用问题
					FUpgradeRewardSaveRecord ModifiedRecord = *RecordPtr;
					
					int32 OriginalCount = ModifiedRecord.TaskCompleteCounts[TaskIndex];
					ModifiedRecord.TaskCompleteCounts[TaskIndex] = Count;
					ModifiedRecord.LastUpdateTime = FDateTime::Now();
					
					// 调试日志：记录修改前后的 AllRecords 状态
					UE_LOG(LogTemp, Log, TEXT("DEBUG: Upgrade.SetTaskCount - 修改前 AllRecords 大小: %d"), this->TargetSubsystem->GetAllRecords().Num());
					for (auto& Pair : this->TargetSubsystem->GetAllRecords())
					{
						UE_LOG(LogTemp, Log, TEXT("DEBUG: Upgrade.SetTaskCount - RecordDate=%d, TaskCompleteCounts[0]=%d"), 
							Pair.Key, Pair.Value.TaskCompleteCounts.IsValidIndex(0) ? Pair.Value.TaskCompleteCounts[0] : -1);
					}
					
					// 更新 AllRecords 中的指定记录
					this->TargetSubsystem->AddOrUpdateRecord(RecordDate, ModifiedRecord);
					
					// 调试日志：记录修改后的 AllRecords 状态
					UE_LOG(LogTemp, Log, TEXT("DEBUG: Upgrade.SetTaskCount - 修改后 AllRecords 大小: %d"), this->TargetSubsystem->GetAllRecords().Num());
					for (auto& Pair : this->TargetSubsystem->GetAllRecords())
					{
						UE_LOG(LogTemp, Log, TEXT("DEBUG: Upgrade.SetTaskCount - RecordDate=%d, TaskCompleteCounts[0]=%d"), 
							Pair.Key, Pair.Value.TaskCompleteCounts.IsValidIndex(0) ? Pair.Value.TaskCompleteCounts[0] : -1);
					}
					
					// 如果修改的是当前记录，同步更新 CurrentRecord
					if (this->TargetSubsystem->GetRecord().GetDayNumber() == RecordDate)
					{
						this->TargetSubsystem->GetRecord() = ModifiedRecord;
					}
					
					UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
					UE_LOG(LogTemp, Log, TEXT("MEMORY_DATA_MODIFICATION_START"));
					UE_LOG(LogTemp, Log, TEXT("🆔 Subsystem地址: %p"), this->TargetSubsystem);
					UE_LOG(LogTemp, Log, TEXT("📊 任务完成次数修改: 任务%d [%d] -> [%d]"), TaskIndex, OriginalCount, Count);
					UE_LOG(LogTemp, Log, TEXT("修改时间: %s"), *FDateTime::Now().ToString());
					UE_LOG(LogTemp, Log, TEXT("运行时模式: 仅内存操作，不写入磁盘"));
					UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
					
					// 强制刷新所有页面，重新获取内存数据
					ForceRefreshAllPages();
					
					UE_LOG(LogTemp, Log, TEXT("Upgrade控制台: 设置任务完成次数 RecordDate=%d TaskIndex=%d Count=%d 成功"), RecordDate, TaskIndex, Count);
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("Upgrade控制台: 无法获取Subsystem实例"));
				}
			}
			else
				{
				UE_LOG(LogTemp, Warning, TEXT("Upgrade控制台: 用法 - Upgrade.SetTaskCount RecordDate TaskIndex Count"));
			}
		}),
		ECVF_Default
	);




}

bool UUpgradeActivitySaveModifier::ValidateAndLog(const TCHAR* FunctionName) const
{
	if (!IsInitialized() || !TargetSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier::%s: 未初始化或Subsystem无效"), FunctionName);
		return false;
	}
	return true;
}

bool UUpgradeActivitySaveModifier::ValidateBinaryState(int32 Value, const TCHAR* ValueName) const
{
	if (Value != 0 && Value != 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: %s只能是0或1: %d"), ValueName, Value);
		return false;
	}
	return true;
}

void UUpgradeActivitySaveModifier::LogModification(const TCHAR* FieldName, const FString& ChangeDesc) const
{
	UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
	UE_LOG(LogTemp, Log, TEXT("MEMORY_DATA_MODIFICATION_START"));
	UE_LOG(LogTemp, Log, TEXT("🆔 Subsystem地址: %p"), TargetSubsystem);
	UE_LOG(LogTemp, Log, TEXT("%s修改: %s"), FieldName, *ChangeDesc);
	UE_LOG(LogTemp, Log, TEXT("修改时间: %s"), *FDateTime::Now().ToString());
	UE_LOG(LogTemp, Log, TEXT("运行时模式: 仅内存操作，不写入磁盘"));
	UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
}

void UUpgradeActivitySaveModifier::ForceRefreshAllPages()
{
	if (!TargetSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("ForceRefreshAllPages: TargetSubsystem为空"));
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
	UE_LOG(LogTemp, Log, TEXT("🔄 FORCE_PAGE_REFRESH_START"));
	UE_LOG(LogTemp, Log, TEXT("🆔 Subsystem地址: %p"), TargetSubsystem);
	UE_LOG(LogTemp, Log, TEXT("📊 当前数据状态:"));
	UE_LOG(LogTemp, Log, TEXT("   RecordDate: %d"), TargetSubsystem->GetRecord().GetDayNumber());
	UE_LOG(LogTemp, Log, TEXT("   CurrentExperience: %d"), TargetSubsystem->GetRecord().CurrentExperience);
	UE_LOG(LogTemp, Log, TEXT("   RewardIconIndex: %d"), TargetSubsystem->GetRecord().RewardIconIndex);
	UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
	
	// 触发全局刷新事件，强制所有页面重新获取内存数据
	UE_LOG(LogTemp, Log, TEXT("🔊 触发OnGlobalRefresh.Broadcast() - 强制页面刷新"));
	TargetSubsystem->OnGlobalRefresh.Broadcast();
	UE_LOG(LogTemp, Log, TEXT("✅ OnGlobalRefresh.Broadcast()执行完成"));
	
	// 触发奖励图标变更事件
	UE_LOG(LogTemp, Log, TEXT("🔊 触发OnRewardIconIndexChanged.Broadcast()"));
	TargetSubsystem->OnRewardIconIndexChanged.Broadcast(TargetSubsystem->GetCurrentRewardIconIndex());
	UE_LOG(LogTemp, Log, TEXT("✅ OnRewardIconIndexChanged.Broadcast()执行完成"));
	
	UE_LOG(LogTemp, Log, TEXT("\n🎯 页面刷新完成 - 所有 UI 组件已重新获取最新内存数据"));
}

void UUpgradeActivitySaveModifier::InitializeNewRecord(FUpgradeRewardSaveRecord& Record, int32 RecordDate)
{
	Record.SetRecordDate(RecordDate);
	Record.CurrentExperience = 0;
	Record.RewardIconIndex = 0;
	Record.LimitedActivityCompleteCount = 0;
	
	// 初始化数组
	// 【v228 SSOT 重构】ChestClaimStatus 已迁至 GlobalChestClaimStatus, 不再初始化 per-day record 字段
	Record.TaskCompleteCounts.SetNumZeroed(MAX_TASK_COUNT);
	Record.TaskClaimStatus.SetNumZeroed(MAX_TASK_COUNT);
	
	Record.LastUpdateTime = FDateTime::Now();
	Record.CreatedTime = FDateTime::Now();
	
	UE_LOG(LogTemp, Log, TEXT("✅ 新记录初始化完成 - RecordDate: %d"), RecordDate);
}

void UUpgradeActivitySaveModifier::ShowDailyUpgradePage()
{
	UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: ShowDailyUpgradePage 功能待实现"));
}

void UUpgradeActivitySaveModifier::AutoSaveOnGameExit()
{
	if (!TargetSubsystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("⚠️ AutoSaveOnGameExit: TargetSubsystem 为空，无需保存"));
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
	UE_LOG(LogTemp, Log, TEXT("💾 AUTO_SAVE_ON_GAME_EXIT_START"));
	UE_LOG(LogTemp, Log, TEXT("🆔 Subsystem 地址：%p"), TargetSubsystem);
	UE_LOG(LogTemp, Log, TEXT("📊 执行游戏退出自动保存"));
	UE_LOG(LogTemp, Log, TEXT("⏰ 保存时间：%s"), *FDateTime::Now().ToString());
	UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
	
	// 保存当前内存数据到磁盘
	TargetSubsystem->SaveStatus();
	
	UE_LOG(LogTemp, Log, TEXT("✅ 游戏退出自动保存完成 - 所有内存修改已持久化到磁盘"));
}

void UUpgradeActivitySaveModifier::UnregisterConsoleCommands()
{
	// 🔧 注意：UE 的 IConsoleManager 不支持动态注销命令
	// 命令会在编辑器关闭时自动清理
	UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: UnregisterConsoleCommands - 命令将在编辑器关闭时自动清理"));
}
