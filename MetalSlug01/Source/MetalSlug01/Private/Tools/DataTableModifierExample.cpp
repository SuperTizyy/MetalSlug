/**
 * @file DataTableModifierExample.cpp
 * @brief 动态表修改器使用示例实现
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 * 
 * @details 实现动态表修改器的各种使用示例
 */

#include "Tools/DataTableModifierExample.h"
#include "Engine/Engine.h"

void UDataTableModifierExample::DemoBasicModification(UObject* WorldContext)
{
	// 获取修改器实例
	UUniversalDataTableModifier* Modifier = GetModifierInstance(WorldContext);
	if (!Modifier)
	{
		UE_LOG(LogTemp, Error, TEXT("DataTableModifierExample: 无法获取修改器实例"));
		return;
	}

	// 示例：修改每日登录配置表
	FString TablePath = TEXT("/Game/UI/Activity/Data/DT_DailyLoginConfigRow");
	FName RowName = FName(TEXT("1")); // 第一天的配置

	// 加载表并获取原始数据
	if (UDataTable* Table = Modifier->LoadDataTable(TablePath))
	{
		static const FString ContextString(TEXT("ExampleContext"));
		FDailyLoginConfigRow* OriginalRow = Table->FindRow<FDailyLoginConfigRow>(RowName, ContextString);
		
		if (OriginalRow)
		{
			// 创建修改后的数据
			FDailyLoginConfigRow ModifiedRow = *OriginalRow;
			ModifiedRow.RewardCount = 10; // 修改奖励数量
			
			// 执行更新操作
			FGuid ModificationId = Modifier->UpdateTableRow(
				TablePath,
				RowName,
				reinterpret_cast<const uint8*>(&ModifiedRow),
				false // 临时修改，不持久化
			);

			if (ModificationId.IsValid())
			{
				UE_LOG(LogTemp, Log, TEXT("DataTableModifierExample: 成功修改行 %s，新的奖励数量: %d"), 
					*RowName.ToString(), ModifiedRow.RewardCount);
			}
		}
	}
}

void UDataTableModifierExample::DemoTransactionalModification(UObject* WorldContext)
{
	UUniversalDataTableModifier* Modifier = GetModifierInstance(WorldContext);
	if (!Modifier)
	{
		return;
	}

	// 开始事务
	FGuid TransactionId = Modifier->BeginTransaction(TEXT("奖励配置更新"));

	// 执行多个相关的修改操作
	FString LoginTablePath = TEXT("/Game/UI/Activity/Data/DT_DailyLoginConfigRow");
	FString ItemTablePath = TEXT("/Game/UI/Activity/Data/DT_ItemDetailRow");

	// 修改登录奖励
	FDailyLoginConfigRow LoginRow;
	LoginRow.ActivityID = 101;
	LoginRow.DayIndex = 1;
	LoginRow.RewardType = ELoginRewardType::NormalItem;
	LoginRow.RewardItemID = 1001;
	LoginRow.RewardCount = 5;
	
	Modifier->AddTableRow(
		LoginTablePath,
		FName(TEXT("NewDay1")),
		reinterpret_cast<const uint8*>(&LoginRow)
	);

	// 修改物品详情
	FItemDetailRow ItemRow;
	ItemRow.ItemID = 1001;
	ItemRow.ItemName = FText::FromString(TEXT("新手礼包"));
	ItemRow.ItemDescription = FText::FromString(TEXT("给新玩家的特别奖励"));
	
	Modifier->AddTableRow(
		ItemTablePath,
		FName(TEXT("NewItem1001")),
		reinterpret_cast<const uint8*>(&ItemRow)
	);

	// 提交事务
	if (Modifier->CommitTransaction(TransactionId))
	{
		UE_LOG(LogTemp, Log, TEXT("DataTableModifierExample: 事务提交成功"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("DataTableModifierExample: 事务提交失败，已自动回滚"));
	}
}

void UDataTableModifierExample::DemoPersistentModification(UObject* WorldContext)
{
	// 创建启用持久化的配置
	FDataTableModificationConfig Config = CreateExampleConfig();
	Config.bEnablePersistentModification = true;
	Config.bAutoSaveChanges = true;

	// 重新初始化修改器
	UUniversalDataTableModifier* Modifier = NewObject<UUniversalDataTableModifier>();
	if (Modifier->InitializeModifier(Config, WorldContext))
	{
		// 执行需要持久化的修改
		FString TablePath = TEXT("/Game/UI/Activity/Data/DT_ActivityInfoRow");
		FActivityInfoRow NewActivity;
		NewActivity.ActivityID = 999;
		NewActivity.ActivityTitle = TEXT("限时活动");
		NewActivity.ActivityType = EActivityType::NormalActivity;
		NewActivity.TotalDays = 7;
		
		FGuid ModificationId = Modifier->AddTableRow(
			TablePath,
			FName(TEXT("LimitedTimeActivity")),
			reinterpret_cast<const uint8*>(&NewActivity),
			true // 持久化修改
		);

		if (ModificationId.IsValid())
		{
			// 手动触发保存
			Modifier->SaveAllModifications();
			UE_LOG(LogTemp, Log, TEXT("DataTableModifierExample: 持久化修改已保存"));
		}
	}
}

void UDataTableModifierExample::DemoBatchModification(UObject* WorldContext)
{
	UUniversalDataTableModifier* Modifier = GetModifierInstance(WorldContext);
	if (!Modifier)
	{
		return;
	}

	// 准备批量修改数据
	TArray<FTableModificationRecord> Modifications;
	FString TablePath = TEXT("/Game/UI/Activity/Data/DT_DailyLoginConfigRow");

	// 创建多个修改记录
	for (int32 i = 1; i <= 3; ++i)
	{
		FDailyLoginConfigRow Row;
		Row.ActivityID = 101;
		Row.DayIndex = i;
		Row.RewardCount = i * 2; // 第1天2个，第2天4个，第3天6个
		
		FTableModificationRecord Record(
			ETableModificationType::UpdateRow,
			TablePath,
			FName(*FString::Printf(TEXT("%d"), i)),
			false
		);
		
		Record.ModifiedDataSnapshot = UUniversalDataTableModifier::SerializeStructToJson(
			reinterpret_cast<const uint8*>(&Row),
			FDailyLoginConfigRow::StaticStruct()
		);
		
		Modifications.Add(Record);
	}

	// 执行批量修改
	FGuid BatchId = Modifier->BatchModifyTable(TablePath, Modifications);
	
	if (BatchId.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("DataTableModifierExample: 批量修改执行成功"));
	}
}

void UDataTableModifierExample::DemoHistoryAndRollback(UObject* WorldContext)
{
	UUniversalDataTableModifier* Modifier = GetModifierInstance(WorldContext);
	if (!Modifier)
	{
		return;
	}

	// 获取修改历史
	TArray<FTableModificationRecord> History = Modifier->GetModificationHistory(10);
	
	UE_LOG(LogTemp, Log, TEXT("DataTableModifierExample: 最近的修改记录:"));
	for (const FTableModificationRecord& Record : History)
	{
		UE_LOG(LogTemp, Log, TEXT("  - ID: %s, 类型: %s, 表: %s, 行: %s"), 
			*Record.ModificationId.ToString(),
			*UEnum::GetValueAsString(Record.ModificationType),
			*Record.TargetTablePath,
			*Record.TargetRowName.ToString());
	}

	// 演示回滚操作（如果有修改记录的话）
	if (History.Num() > 0)
	{
		const FTableModificationRecord& LastRecord = History.Last();
		if (Modifier->RevertModification(LastRecord.ModificationId))
		{
			UE_LOG(LogTemp, Log, TEXT("DataTableModifierExample: 成功回滚最后一次修改"));
		}
	}
}

UUniversalDataTableModifier* UDataTableModifierExample::GetModifierInstance(UObject* WorldContext)
{
	// 这里应该实现单例模式或从Subsystem获取
	// 简化示例直接创建新实例
	static UUniversalDataTableModifier* Instance = nullptr;
	
	if (!Instance)
	{
		Instance = NewObject<UUniversalDataTableModifier>();
		FDataTableModificationConfig Config = CreateExampleConfig();
		Instance->InitializeModifier(Config, WorldContext);
	}
	
	return Instance;
}

FDataTableModificationConfig UDataTableModifierExample::CreateExampleConfig()
{
	FDataTableModificationConfig Config;
	Config.bEnableMemoryModification = true;
	Config.bEnablePersistentModification = false;
	Config.MaxHistoryRecords = 100;
	Config.bAutoSaveChanges = false;
	Config.AutoSaveInterval = 30.0f;
	Config.bEnableTransactionSupport = true;
	Config.TransactionTimeout = 60.0f;
	return Config;
}