/**
 * @file ActivityDataTableModifierBPLibrary.cpp
 * @brief 活动系统动态表修改器蓝图函数库实现
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 * 
 * @details 实现ActivitySubsystem动态表修改功能的蓝图接口
 */

#include "Tools/ActivityDataTableModifierBPLibrary.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

UUniversalDataTableModifier* UActivityDataTableModifierBPLibrary::GetActivityDataTableModifier(UObject* WorldContext)
{
	if (!WorldContext)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityDataTableModifierBPLibrary: WorldContext为空"));
		return nullptr;
	}

	UWorld* World = WorldContext->GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityDataTableModifierBPLibrary: 无法获取World"));
		return nullptr;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityDataTableModifierBPLibrary: 无法获取GameInstance"));
		return nullptr;
	}

	UActivitySubsystem* ActivitySubsystem = GameInstance->GetSubsystem<UActivitySubsystem>();
	if (!ActivitySubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityDataTableModifierBPLibrary: 无法获取ActivitySubsystem"));
		return nullptr;
	}

	return ActivitySubsystem->GetDataTableModifier();
}

bool UActivityDataTableModifierBPLibrary::ModifyDailyLoginReward(UObject* WorldContext, int32 ActivityID, int32 DayIndex, int32 NewRewardCount, bool bPersistent)
{
	UUniversalDataTableModifier* Modifier = GetActivityDataTableModifier(WorldContext);
	if (!Modifier)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityDataTableModifierBPLibrary: 无法获取动态表修改器"));
		return false;
	}

	// 构建行名称（假设格式为"ActivityID_DayIndex"）
	FString RowNameStr = FString::Printf(TEXT("%d_%d"), ActivityID, DayIndex);
	FName RowName(*RowNameStr);

	// 创建新的配置数据
	FDailyLoginConfigRow NewConfig;
	NewConfig.ActivityID = ActivityID;
	NewConfig.DayIndex = DayIndex;
	NewConfig.RewardCount = NewRewardCount;
	// 其他字段保持默认值或从原数据复制

	// 执行修改
	FGuid ModificationId = Modifier->UpdateTableRow(
		TEXT("/Game/UI/Activity/Data/DT_DailyLoginConfigRow"),
		RowName,
		reinterpret_cast<const uint8*>(&NewConfig),
		bPersistent
	);

	bool bSuccess = ModificationId.IsValid();
	if (bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("ActivityDataTableModifierBPLibrary: 成功修改每日登录奖励 - ActivityID=%d, Day=%d, NewCount=%d"), 
			ActivityID, DayIndex, NewRewardCount);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityDataTableModifierBPLibrary: 修改每日登录奖励失败"));
	}

	return bSuccess;
}

bool UActivityDataTableModifierBPLibrary::ModifyItemDetail(UObject* WorldContext, int32 ItemID, const FString& NewItemName, bool bPersistent)
{
	UUniversalDataTableModifier* Modifier = GetActivityDataTableModifier(WorldContext);
	if (!Modifier)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityDataTableModifierBPLibrary: 无法获取动态表修改器"));
		return false;
	}

	FName RowName(*FString::FromInt(ItemID));

	// 创建新的物品详情数据
	FItemDetailRow NewItemDetail;
	NewItemDetail.ItemID = ItemID;
	NewItemDetail.ItemName = FText::FromString(NewItemName);
	// 其他字段保持默认值

	// 执行修改
	FGuid ModificationId = Modifier->UpdateTableRow(
		TEXT("/Game/UI/Activity/Data/DT_ItemDetailRow"),
		RowName,
		reinterpret_cast<const uint8*>(&NewItemDetail),
		bPersistent
	);

	bool bSuccess = ModificationId.IsValid();
	if (bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("ActivityDataTableModifierBPLibrary: 成功修改物品详情 - ItemID=%d, NewName=%s"), 
			ItemID, *NewItemName);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityDataTableModifierBPLibrary: 修改物品详情失败"));
	}

	return bSuccess;
}

TArray<FDailyLoginConfigRow*> UActivityDataTableModifierBPLibrary::GetModifiedDailyLoginConfigs(UObject* WorldContext, int32 ActivityID)
{
	if (!WorldContext)
	{
		return TArray<FDailyLoginConfigRow*>();
	}

	UWorld* World = WorldContext->GetWorld();
	if (!World)
	{
		return TArray<FDailyLoginConfigRow*>();
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		return TArray<FDailyLoginConfigRow*>();
	}

	UActivitySubsystem* ActivitySubsystem = GameInstance->GetSubsystem<UActivitySubsystem>();
	if (!ActivitySubsystem)
	{
		return TArray<FDailyLoginConfigRow*>();
	}

	return ActivitySubsystem->GetModifiedDailyLoginConfigs(ActivityID);
}

TArray<FDailyLoginConfigRow*> UActivityDataTableModifierBPLibrary::GetModifiedRewardsByDay(UObject* WorldContext, int32 ActivityID, int32 Day)
{
	if (!WorldContext)
	{
		return TArray<FDailyLoginConfigRow*>();
	}

	UWorld* World = WorldContext->GetWorld();
	if (!World)
	{
		return TArray<FDailyLoginConfigRow*>();
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		return TArray<FDailyLoginConfigRow*>();
	}

	UActivitySubsystem* ActivitySubsystem = GameInstance->GetSubsystem<UActivitySubsystem>();
	if (!ActivitySubsystem)
	{
		return TArray<FDailyLoginConfigRow*>();
	}

	return ActivitySubsystem->GetModifiedRewardsByDay(ActivityID, Day);
}

const FItemDetailRow* UActivityDataTableModifierBPLibrary::GetModifiedItemDetail(UObject* WorldContext, int32 ItemID)
{
	if (!WorldContext)
	{
		return nullptr;
	}

	UWorld* World = WorldContext->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		return nullptr;
	}

	UActivitySubsystem* ActivitySubsystem = GameInstance->GetSubsystem<UActivitySubsystem>();
	if (!ActivitySubsystem)
	{
		return nullptr;
	}

	return ActivitySubsystem->GetModifiedItemDetail(ItemID);
}

bool UActivityDataTableModifierBPLibrary::SaveAllDataTableModifications(UObject* WorldContext)
{
	UUniversalDataTableModifier* Modifier = GetActivityDataTableModifier(WorldContext);
	if (!Modifier)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityDataTableModifierBPLibrary: 无法获取动态表修改器"));
		return false;
	}

	bool bSuccess = Modifier->SaveAllModifications();
	if (bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("ActivityDataTableModifierBPLibrary: 成功保存所有数据表修改"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityDataTableModifierBPLibrary: 保存数据表修改失败"));
	}

	return bSuccess;
}

bool UActivityDataTableModifierBPLibrary::LoadSavedDataTableModifications(UObject* WorldContext)
{
	UUniversalDataTableModifier* Modifier = GetActivityDataTableModifier(WorldContext);
	if (!Modifier)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityDataTableModifierBPLibrary: 无法获取动态表修改器"));
		return false;
	}

	bool bSuccess = Modifier->LoadSavedModifications();
	if (bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("ActivityDataTableModifierBPLibrary: 成功加载已保存的数据表修改"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ActivityDataTableModifierBPLibrary: 没有找到已保存的修改数据或加载失败"));
	}

	return bSuccess;
}