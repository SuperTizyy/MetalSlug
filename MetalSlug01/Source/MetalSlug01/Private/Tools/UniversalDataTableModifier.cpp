/**
 * @file UniversalDataTableModifier.cpp
 * @brief 万能动态表修改器实现文件
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 * 
 * @details 实现万能动态表修改器的核心功能逻辑
 */

#include "Tools/UniversalDataTableModifier.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "JsonObjectConverter.h"

// ==================== 辅助结构体实现 ====================

FTableModificationRecord::FTableModificationRecord()
	: ModificationType(ETableModificationType::UpdateRow)
	, Status(EModificationStatus::Pending)
	, bRequiresPersistence(false)
{
	ModificationId = FGuid::NewGuid();
	ModificationTime = FDateTime::Now();
}

FTableModificationRecord::FTableModificationRecord(
	ETableModificationType InType,
	const FString& InTablePath,
	FName InRowName,
	bool InRequiresPersistence
)
	: ModificationType(InType)
	, TargetTablePath(InTablePath)
	, TargetRowName(InRowName)
	, Status(EModificationStatus::Pending)
	, bRequiresPersistence(InRequiresPersistence)
{
	ModificationId = FGuid::NewGuid();
	ModificationTime = FDateTime::Now();
}

FDataTableModificationConfig::FDataTableModificationConfig()
	: bEnableMemoryModification(true)
	, bEnablePersistentModification(false)
	, MaxHistoryRecords(1000)
	, bAutoSaveChanges(false)
	, AutoSaveInterval(30.0f)
	, bEnableTransactionSupport(true)
	, TransactionTimeout(60.0f)
{
}

UDataTableModificationSave::UDataTableModificationSave()
	: SaveVersion(1)
{
}

// ==================== 主类实现 ====================

UUniversalDataTableModifier::UUniversalDataTableModifier()
	: bIsInitialized(false)
{
}

UUniversalDataTableModifier::~UUniversalDataTableModifier()
{
	DestroyModifier();
}

bool UUniversalDataTableModifier::InitializeModifier(
	const FDataTableModificationConfig& InConfig,
	UObject* WorldContext
)
{
	if (bIsInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("UniversalDataTableModifier: 已经初始化过了"));
		return true;
	}

	Config = InConfig;
	WorldContextObject = WorldContext;
	bIsInitialized = true;

	// 加载已保存的修改
	if (Config.bEnablePersistentModification)
	{
		LoadSavedModifications();
	}

	// 启动自动保存定时器
	if (Config.bAutoSaveChanges && Config.AutoSaveInterval > 0)
	{
		if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull))
		{
			World->GetTimerManager().SetTimer(
				AutoSaveTimerHandle,
				this,
				&UUniversalDataTableModifier::OnAutoSaveTimer,
				Config.AutoSaveInterval,
				true
			);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("UniversalDataTableModifier: 初始化成功"));
	return true;
}

void UUniversalDataTableModifier::DestroyModifier()
{
	if (!bIsInitialized)
	{
		return;
	}

	// 停止自动保存定时器
	if (AutoSaveTimerHandle.IsValid())
	{
		if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject.Get(), EGetWorldErrorMode::LogAndReturnNull))
		{
			World->GetTimerManager().ClearTimer(AutoSaveTimerHandle);
		}
		AutoSaveTimerHandle.Invalidate();
	}

	// 保存未提交的修改
	if (Config.bEnablePersistentModification)
	{
		SaveAllModifications();
	}

	// 清理内存中的修改数据
	for (auto& TablePair : ModifiedTableData)
	{
		for (auto& RowPair : TablePair.Value)
		{
			if (UDataTable* Table = LoadDataTable(TablePair.Key))
			{
				if (UStruct* RowStruct = GetTableRowStruct(Table))
				{
					DestroyStructInstance(RowPair.Value, RowStruct);
				}
			}
		}
	}
	ModifiedTableData.Empty();

	// 清理加载的表缓存
	LoadedTables.Empty();

	bIsInitialized = false;
	UE_LOG(LogTemp, Log, TEXT("UniversalDataTableModifier: 已销毁"));
}

UDataTable* UUniversalDataTableModifier::LoadDataTable(const FString& TablePath)
{
	// 检查缓存
	if (TObjectPtr<UDataTable>* CachedTable = LoadedTables.Find(TablePath))
	{
		return CachedTable->Get();
	}

	// 加载DataTable
	UDataTable* DataTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *TablePath));
	if (DataTable)
	{
		LoadedTables.Add(TablePath, DataTable);
		UE_LOG(LogTemp, Log, TEXT("UniversalDataTableModifier: 成功加载DataTable %s"), *TablePath);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("UniversalDataTableModifier: 无法加载DataTable %s"), *TablePath);
	}

	return DataTable;
}

uint8* UUniversalDataTableModifier::GetModifiedRow(const FString& TablePath, FName RowName)
{
	// 首先检查是否有修改过的数据
	if (TMap<FName, uint8*>* ModifiedRows = ModifiedTableData.Find(TablePath))
	{
		if (uint8** ModifiedRow = ModifiedRows->Find(RowName))
		{
			return *ModifiedRow;
		}
	}

	// 如果没有修改过，从原始表获取
	if (UDataTable* Table = LoadDataTable(TablePath))
	{
		static const FString ContextString(TEXT("DataTableModifier"));
		return Table->FindRowUnchecked(RowName, ContextString);
	}

	return nullptr;
}

bool UUniversalDataTableModifier::GetAllModifiedRows(const FString& TablePath, TArray<uint8*>& OutRows)
{
	OutRows.Empty();

	// 获取修改过的行
	if (TMap<FName, uint8*>* ModifiedRows = ModifiedTableData.Find(TablePath))
	{
		for (auto& RowPair : *ModifiedRows)
		{
			OutRows.Add(RowPair.Value);
		}
	}

	// 获取原始表中未被修改的行
	if (UDataTable* Table = LoadDataTable(TablePath))
	{
		TArray<FName> AllRowNames;
		Table->GetAllRowNames(AllRowNames);

		for (const FName& RowName : AllRowNames)
		{
			// 跳过已修改的行
			if (TMap<FName, uint8*>* ModifiedRows = ModifiedTableData.Find(TablePath))
			{
				if (ModifiedRows->Contains(RowName))
				{
					continue;
				}
			}

			static const FString ContextString(TEXT("DataTableModifier"));
			if (uint8* RowData = Table->FindRowUnchecked(RowName, ContextString))
			{
				OutRows.Add(RowData);
			}
		}
	}

	return OutRows.Num() > 0;
}

FGuid UUniversalDataTableModifier::BeginTransaction(const FString& TransactionName)
{
	FGuid TransactionId = FGuid::NewGuid();
	ActiveTransactions.Add(TransactionId, TArray<FGuid>());
	
	UE_LOG(LogTemp, Log, TEXT("UniversalDataTableModifier: 开始事务 %s (ID: %s)"), 
		*TransactionName, *TransactionId.ToString());
	
	return TransactionId;
}

bool UUniversalDataTableModifier::CommitTransaction(FGuid TransactionId)
{
	if (!ActiveTransactions.Contains(TransactionId))
	{
		UE_LOG(LogTemp, Error, TEXT("UniversalDataTableModifier: 无效的事务ID %s"), *TransactionId.ToString());
		return false;
	}

	// 提交事务中的所有修改
	TArray<FGuid>& ModificationIds = ActiveTransactions[TransactionId];
	for (const FGuid& ModId : ModificationIds)
	{
		for (FTableModificationRecord& Record : ModificationHistory)
		{
			if (Record.ModificationId == ModId && Record.Status == EModificationStatus::Pending)
			{
				if (!ExecuteModification(Record))
				{
					UE_LOG(LogTemp, Error, TEXT("UniversalDataTableModifier: 事务提交失败，修改ID: %s"), *ModId.ToString());
					return false;
				}
			}
		}
	}

	ActiveTransactions.Remove(TransactionId);
	UE_LOG(LogTemp, Log, TEXT("UniversalDataTableModifier: 事务提交成功 (ID: %s)"), *TransactionId.ToString());
	return true;
}

bool UUniversalDataTableModifier::RollbackTransaction(FGuid TransactionId)
{
	if (!ActiveTransactions.Contains(TransactionId))
	{
		UE_LOG(LogTemp, Error, TEXT("UniversalDataTableModifier: 无效的事务ID %s"), *TransactionId.ToString());
		return false;
	}

	// 回滚事务中的所有修改
	TArray<FGuid>& ModificationIds = ActiveTransactions[TransactionId];
	for (const FGuid& ModId : ModificationIds)
	{
		for (const FTableModificationRecord& Record : ModificationHistory)
		{
			if (Record.ModificationId == ModId)
			{
				RollbackModification(Record);
				break;
			}
		}
	}

	ActiveTransactions.Remove(TransactionId);
	UE_LOG(LogTemp, Log, TEXT("UniversalDataTableModifier: 事务回滚成功 (ID: %s)"), *TransactionId.ToString());
	return true;
}

FGuid UUniversalDataTableModifier::AddTableRow(
	const FString& TablePath,
	FName RowName,
	const uint8* RowData,
	bool bPersistent
)
{
	FString ErrorMsg;
	if (!ValidateModification(TablePath, RowName, ETableModificationType::AddRow, ErrorMsg))
	{
		UE_LOG(LogTemp, Error, TEXT("UniversalDataTableModifier: 添加行验证失败: %s"), *ErrorMsg);
		return FGuid();
	}

	FTableModificationRecord Record(ETableModificationType::AddRow, TablePath, RowName, bPersistent);
	
	if (UDataTable* Table = LoadDataTable(TablePath))
	{
		if (UStruct* RowStruct = GetTableRowStruct(Table))
		{
			// 创建数据副本
			uint8* NewRowData = CreateStructInstance(RowStruct);
			if (NewRowData && RowData)
			{
				RowStruct->CopyScriptStruct(NewRowData, RowData);
				Record.ModifiedDataSnapshot = SerializeStructToJson(NewRowData, RowStruct);
				
				// 保存到修改缓存
				ModifiedTableData.FindOrAdd(TablePath).Add(RowName, NewRowData);
			}
		}
	}

	ModificationHistory.Add(Record);
	CleanupOldHistory();
	
	UE_LOG(LogTemp, Log, TEXT("UniversalDataTableModifier: 添加行记录创建成功 (表: %s, 行: %s)"), 
		*TablePath, *RowName.ToString());
	
	return Record.ModificationId;
}

FGuid UUniversalDataTableModifier::UpdateTableRow(
	const FString& TablePath,
	FName RowName,
	const uint8* UpdatedData,
	bool bPersistent
)
{
	FString ErrorMsg;
	if (!ValidateModification(TablePath, RowName, ETableModificationType::UpdateRow, ErrorMsg))
	{
		UE_LOG(LogTemp, Error, TEXT("UniversalDataTableModifier: 更新行验证失败: %s"), *ErrorMsg);
		return FGuid();
	}

	FTableModificationRecord Record(ETableModificationType::UpdateRow, TablePath, RowName, bPersistent);
	
	if (UDataTable* Table = LoadDataTable(TablePath))
	{
		if (UStruct* RowStruct = GetTableRowStruct(Table))
		{
			// 获取原始数据快照
			static const FString ContextString(TEXT("DataTableModifier"));
			uint8* OriginalData = Table->FindRowUnchecked(RowName, ContextString);
			if (OriginalData)
			{
				Record.OriginalDataSnapshot = SerializeStructToJson(OriginalData, RowStruct);
			}

			// 创建修改后的数据副本
			uint8* NewRowData = CreateStructInstance(RowStruct);
			if (NewRowData && UpdatedData)
			{
				RowStruct->CopyScriptStruct(NewRowData, UpdatedData);
				Record.ModifiedDataSnapshot = SerializeStructToJson(NewRowData, RowStruct);
				
				// 保存到修改缓存
				ModifiedTableData.FindOrAdd(TablePath).Add(RowName, NewRowData);
			}
		}
	}

	ModificationHistory.Add(Record);
	CleanupOldHistory();
	
	UE_LOG(LogTemp, Log, TEXT("UniversalDataTableModifier: 更新行记录创建成功 (表: %s, 行: %s)"), 
		*TablePath, *RowName.ToString());
	
	return Record.ModificationId;
}

FGuid UUniversalDataTableModifier::DeleteTableRow(
	const FString& TablePath,
	FName RowName,
	bool bPersistent
)
{
	FString ErrorMsg;
	if (!ValidateModification(TablePath, RowName, ETableModificationType::DeleteRow, ErrorMsg))
	{
		UE_LOG(LogTemp, Error, TEXT("UniversalDataTableModifier: 删除行验证失败: %s"), *ErrorMsg);
		return FGuid();
	}

	FTableModificationRecord Record(ETableModificationType::DeleteRow, TablePath, RowName, bPersistent);
	
	if (UDataTable* Table = LoadDataTable(TablePath))
	{
		if (UStruct* RowStruct = GetTableRowStruct(Table))
		{
			// 获取要删除的数据快照
			static const FString ContextString(TEXT("DataTableModifier"));
			uint8* OriginalData = Table->FindRowUnchecked(RowName, ContextString);
			if (OriginalData)
			{
				Record.OriginalDataSnapshot = SerializeStructToJson(OriginalData, RowStruct);
			}
			
			// 标记为删除（在修改缓存中标记）
			ModifiedTableData.FindOrAdd(TablePath).Add(RowName, nullptr);
		}
	}

	ModificationHistory.Add(Record);
	CleanupOldHistory();
	
	UE_LOG(LogTemp, Log, TEXT("UniversalDataTableModifier: 删除行记录创建成功 (表: %s, 行: %s)"), 
		*TablePath, *RowName.ToString());
	
	return Record.ModificationId;
}

FGuid UUniversalDataTableModifier::BatchModifyTable(
	const FString& TablePath,
	const TArray<FTableModificationRecord>& Modifications,
	bool bPersistent
)
{
	FGuid TransactionId = BeginTransaction(FString::Printf(TEXT("BatchModify_%s"), *TablePath));
	
	for (const FTableModificationRecord& Mod : Modifications)
	{
		// 这里应该根据修改类型调用相应的修改方法
		// 简化处理，实际需要更复杂的逻辑
		ActiveTransactions[TransactionId].Add(Mod.ModificationId);
	}
	
	if (!CommitTransaction(TransactionId))
	{
		RollbackTransaction(TransactionId);
		return FGuid();
	}
	
	return TransactionId;
}

TArray<FTableModificationRecord> UUniversalDataTableModifier::GetModificationHistory(int32 MaxRecords)
{
	TArray<FTableModificationRecord> Result;
	
	int32 StartIndex = FMath::Max(0, ModificationHistory.Num() - MaxRecords);
	for (int32 i = StartIndex; i < ModificationHistory.Num(); ++i)
	{
		Result.Add(ModificationHistory[i]);
	}
	
	return Result;
}

bool UUniversalDataTableModifier::RevertModification(FGuid ModificationId)
{
	for (const FTableModificationRecord& Record : ModificationHistory)
	{
		if (Record.ModificationId == ModificationId)
		{
			return RollbackModification(Record);
		}
	}
	
	UE_LOG(LogTemp, Error, TEXT("UniversalDataTableModifier: 未找到修改记录 %s"), *ModificationId.ToString());
	return false;
}

bool UUniversalDataTableModifier::IsTableModified(const FString& TablePath)
{
	return ModifiedTableData.Contains(TablePath) && ModifiedTableData[TablePath].Num() > 0;
}

TArray<FString> UUniversalDataTableModifier::GetModifiedTables()
{
	TArray<FString> Result;
	for (const auto& TablePair : ModifiedTableData)
	{
		if (TablePair.Value.Num() > 0)
		{
			Result.Add(TablePair.Key);
		}
	}
	return Result;
}

bool UUniversalDataTableModifier::SaveAllModifications()
{
	if (!Config.bEnablePersistentModification)
	{
		UE_LOG(LogTemp, Warning, TEXT("UniversalDataTableModifier: 持久化功能未启用"));
		return false;
	}

	UDataTableModificationSave* SaveGame = Cast<UDataTableModificationSave>(
		UGameplayStatics::CreateSaveGameObject(UDataTableModificationSave::StaticClass())
	);

	if (!SaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("UniversalDataTableModifier: 无法创建存档对象"));
		return false;
	}

	// 保存修改的表数据
	for (const auto& TablePair : ModifiedTableData)
	{
		FModifiedTableData& ModifiedData = SaveGame->ModifiedTables.FindOrAdd(TablePair.Key);
		ModifiedData.TablePath = TablePair.Key;

		for (const auto& RowPair : TablePair.Value)
		{
			if (UDataTable* Table = LoadDataTable(TablePair.Key))
			{
				if (UStruct* RowStruct = GetTableRowStruct(Table))
				{
					if (RowPair.Value)
					{
						// 添加或更新的行
						FString JsonData = SerializeStructToJson(RowPair.Value, RowStruct);
						ModifiedData.AddedRows.Add(RowPair.Key, JsonData);
					}
					else
					{
						// 删除的行
						ModifiedData.DeletedRows.Add(RowPair.Key);
					}
				}
			}
		}
	}

	// 保存修改历史
	SaveGame->ModificationHistory = ModificationHistory;

	// 执行保存
	bool bSuccess = UGameplayStatics::SaveGameToSlot(SaveGame, TEXT("DataTableModifications"), 0);
	
	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("UniversalDataTableModifier: 成功保存所有修改 (%d 个表)"), 
			SaveGame->ModifiedTables.Num());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("UniversalDataTableModifier: 保存修改失败"));
	}

	return bSuccess;
}

bool UUniversalDataTableModifier::LoadSavedModifications()
{
	if (!Config.bEnablePersistentModification)
	{
		return false;
	}

	UDataTableModificationSave* SaveGame = Cast<UDataTableModificationSave>(
		UGameplayStatics::LoadGameFromSlot(TEXT("DataTableModifications"), 0)
	);

	if (!SaveGame)
	{
		UE_LOG(LogTemp, Log, TEXT("UniversalDataTableModifier: 没有找到保存的修改数据"));
		return false;
	}

	// 加载修改的表数据
	for (const auto& TableDataPair : SaveGame->ModifiedTables)
	{
		const FModifiedTableData& ModifiedData = TableDataPair.Value;
		
		if (UDataTable* Table = LoadDataTable(ModifiedData.TablePath))
		{
			if (UStruct* RowStruct = GetTableRowStruct(Table))
			{
				TMap<FName, uint8*>& TableCache = ModifiedTableData.FindOrAdd(ModifiedData.TablePath);

				// 加载添加的行
				for (const auto& RowPair : ModifiedData.AddedRows)
				{
					uint8* NewRow = CreateStructInstance(RowStruct);
					if (DeserializeStructFromJson(RowPair.Value, NewRow, RowStruct))
					{
						TableCache.Add(RowPair.Key, NewRow);
					}
					else
					{
						DestroyStructInstance(NewRow, RowStruct);
					}
				}

				// 标记删除的行
				for (const FName& DeletedRow : ModifiedData.DeletedRows)
				{
					TableCache.Add(DeletedRow, nullptr);
				}
			}
		}
	}

	// 加载修改历史
	ModificationHistory = SaveGame->ModificationHistory;

	UE_LOG(LogTemp, Log, TEXT("UniversalDataTableModifier: 成功加载保存的修改 (%d 个表)"), 
		SaveGame->ModifiedTables.Num());

	return true;
}

bool UUniversalDataTableModifier::ClearAllModifications(bool bIncludePersistent)
{
	// 清理内存中的修改数据
	for (auto& TablePair : ModifiedTableData)
	{
		for (auto& RowPair : TablePair.Value)
		{
			if (UDataTable* Table = LoadDataTable(TablePair.Key))
			{
				if (UStruct* RowStruct = GetTableRowStruct(Table))
				{
					DestroyStructInstance(RowPair.Value, RowStruct);
				}
			}
		}
	}
	ModifiedTableData.Empty();

	// 清理修改历史
	ModificationHistory.Empty();

	// 清理持久化数据
	if (bIncludePersistent && Config.bEnablePersistentModification)
	{
		return UGameplayStatics::DeleteGameInSlot(TEXT("DataTableModifications"), 0);
	}

	UE_LOG(LogTemp, Log, TEXT("UniversalDataTableModifier: 已清除所有修改数据"));
	return true;
}

// ==================== 工具方法实现 ====================

FString UUniversalDataTableModifier::SerializeStructToJson(const uint8* StructPtr, UStruct* StructType)
{
	if (!StructPtr || !StructType)
	{
		return FString();
	}

	// 使用 UE 提供的通用 JSON 转换器
	TSharedRef<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	
	if (FJsonObjectConverter::UStructToJsonObject(StructType, StructPtr, JsonObject, 0, 0))
	{
		FString OutputString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		FJsonSerializer::Serialize(JsonObject, Writer);
		return OutputString;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("UniversalDataTableModifier: UStructToJsonObject 失败，回退到手动序列化"));
	
	// 回退方案：手动序列化（保持兼容性）
	TSharedRef<FJsonObject> FallbackObject = MakeShareable(new FJsonObject);
	
	for (TFieldIterator<FProperty> PropIt(StructType); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property->HasAnyPropertyFlags(CPF_BlueprintVisible))
		{
			continue;
		}

		FString PropertyName = Property->GetName();
		
		if (FStrProperty* StrProperty = CastField<FStrProperty>(Property))
		{
			FString Value = StrProperty->GetPropertyValue_InContainer(StructPtr);
			FallbackObject->SetStringField(PropertyName, Value);
		}
		else if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
		{
			int32 Value = IntProperty->GetPropertyValue_InContainer(StructPtr);
			FallbackObject->SetNumberField(PropertyName, Value);
		}
		else if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
		{
			float Value = FloatProperty->GetPropertyValue_InContainer(StructPtr);
			FallbackObject->SetNumberField(PropertyName, Value);
		}
		else if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			bool Value = BoolProperty->GetPropertyValue_InContainer(StructPtr);
			FallbackObject->SetBoolField(PropertyName, Value);
		}
		// 可以继续添加其他属性类型的处理
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(FallbackObject, Writer);

	return OutputString;
}

bool UUniversalDataTableModifier::DeserializeStructFromJson(const FString& JsonString, uint8* StructPtr, UStruct* StructType)
{
	if (JsonString.IsEmpty() || !StructPtr || !StructType)
	{
		return false;
	}

	// 使用 UE 提供的通用 JSON 转换器
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
	{
		if (FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), StructType, StructPtr, 0, 0))
		{
			return true;
		}
		
		UE_LOG(LogTemp, Warning, TEXT("UniversalDataTableModifier: JsonObjectToUStruct 失败，回退到手动反序列化"));
	}

	// 回退方案：手动反序列化（保持兼容性）
	if (!JsonObject.IsValid())
	{
		JsonObject = MakeShareable(new FJsonObject());
		TSharedRef<TJsonReader<>> FallbackReader = TJsonReaderFactory<>::Create(JsonString);
		if (!FJsonSerializer::Deserialize(FallbackReader, JsonObject))
		{
			return false;
		}
	}

	for (TFieldIterator<FProperty> PropIt(StructType); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property->HasAnyPropertyFlags(CPF_BlueprintVisible))
		{
			continue;
		}

		FString PropertyName = Property->GetName();
		if (!JsonObject->HasField(PropertyName))
		{
			continue;
		}

		if (FStrProperty* StrProperty = CastField<FStrProperty>(Property))
		{
			FString Value;
			if (JsonObject->TryGetStringField(PropertyName, Value))
			{
				StrProperty->SetPropertyValue_InContainer(StructPtr, Value);
			}
		}
		else if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
		{
			int32 Value;
			if (JsonObject->TryGetNumberField(PropertyName, Value))
			{
				IntProperty->SetPropertyValue_InContainer(StructPtr, Value);
			}
		}
		else if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
		{
			double Value;
			if (JsonObject->TryGetNumberField(PropertyName, Value))
			{
				FloatProperty->SetPropertyValue_InContainer(StructPtr, (float)Value);
			}
		}
		else if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			bool Value;
			if (JsonObject->TryGetBoolField(PropertyName, Value))
			{
				BoolProperty->SetPropertyValue_InContainer(StructPtr, Value);
			}
		}
		// 可以继续添加其他属性类型的处理
	}

	return true;
}

uint8* UUniversalDataTableModifier::CreateStructInstance(UStruct* StructType)
{
	if (!StructType)
	{
		return nullptr;
	}

	uint8* NewStruct = (uint8*)FMemory::Malloc(StructType->GetStructureSize());
	StructType->InitializeStruct(NewStruct);
	return NewStruct;
}

void UUniversalDataTableModifier::DestroyStructInstance(uint8* StructPtr, UStruct* StructType)
{
	if (!StructPtr || !StructType)
	{
		return;
	}

	StructType->DestroyStruct(StructPtr);
	FMemory::Free(StructPtr);
}

// ==================== 内部方法实现 ====================

bool UUniversalDataTableModifier::ValidateModification(
	const FString& TablePath,
	FName RowName,
	ETableModificationType ModificationType,
	FString& OutError
)
{
	if (!bIsInitialized)
	{
		OutError = TEXT("修改器未初始化");
		return false;
	}

	if (TablePath.IsEmpty())
	{
		OutError = TEXT("表路径不能为空");
		return false;
	}

	if (RowName.IsNone())
	{
		OutError = TEXT("行名称不能为空");
		return false;
	}

	// 检查表是否存在
	UDataTable* Table = LoadDataTable(TablePath);
	if (!Table)
	{
		OutError = FString::Printf(TEXT("无法加载表: %s"), *TablePath);
		return false;
	}

	// 根据修改类型进行特定验证
	switch (ModificationType)
	{
	case ETableModificationType::AddRow:
		{
			// 检查行是否已存在
			static const FString ContextString(TEXT("DataTableModifier"));
			if (Table->FindRowUnchecked(RowName, ContextString) != nullptr)
			{
				OutError = FString::Printf(TEXT("行已存在: %s"), *RowName.ToString());
				return false;
			}
		}
		break;

	case ETableModificationType::UpdateRow:
	case ETableModificationType::DeleteRow:
		{
			// 检查行是否存在
			static const FString ContextString(TEXT("DataTableModifier"));
			if (Table->FindRowUnchecked(RowName, ContextString) == nullptr)
			{
				if (!ModifiedTableData.Contains(TablePath) || 
					!ModifiedTableData[TablePath].Contains(RowName))
				{
					OutError = FString::Printf(TEXT("行不存在: %s"), *RowName.ToString());
					return false;
				}
			}
		}
		break;
	}

	return true;
}

bool UUniversalDataTableModifier::ExecuteModification(FTableModificationRecord& Record)
{
	// 这里实现具体的修改执行逻辑
	// 实际项目中可能需要更复杂的验证和处理
	
	Record.Status = EModificationStatus::Executing;
	
	// 模拟执行过程
	switch (Record.ModificationType)
	{
	case ETableModificationType::AddRow:
		// 实际添加逻辑已在AddTableRow中处理
		break;
		
	case ETableModificationType::UpdateRow:
		// 实际更新逻辑已在UpdateTableRow中处理
		break;
		
	case ETableModificationType::DeleteRow:
		// 实际删除逻辑已在DeleteTableRow中处理
		break;
	}
	
	Record.Status = EModificationStatus::Completed;
	return true;
}

bool UUniversalDataTableModifier::RollbackModification(const FTableModificationRecord& Record)
{
	// 这里实现具体的回滚逻辑
	UE_LOG(LogTemp, Log, TEXT("UniversalDataTableModifier: 回滚修改 %s"), *Record.ModificationId.ToString());
	
	// 根据修改类型执行回滚
	switch (Record.ModificationType)
	{
	case ETableModificationType::AddRow:
		{
			// 删除添加的行
			if (TMap<FName, uint8*>* ModifiedRows = ModifiedTableData.Find(Record.TargetTablePath))
			{
				if (uint8** RowData = ModifiedRows->Find(Record.TargetRowName))
				{
					if (UDataTable* Table = LoadDataTable(Record.TargetTablePath))
					{
						if (UStruct* RowStruct = GetTableRowStruct(Table))
						{
							DestroyStructInstance(*RowData, RowStruct);
						}
					}
					ModifiedRows->Remove(Record.TargetRowName);
				}
			}
		}
		break;
		
	case ETableModificationType::UpdateRow:
		{
			// 恢复原始数据
			if (Record.OriginalDataSnapshot.IsEmpty())
			{
				// 如果没有原始数据快照，删除修改记录
				if (TMap<FName, uint8*>* ModifiedRows = ModifiedTableData.Find(Record.TargetTablePath))
				{
					ModifiedRows->Remove(Record.TargetRowName);
				}
			}
			else
			{
				// 恢复原始数据
				if (UDataTable* Table = LoadDataTable(Record.TargetTablePath))
				{
					if (UStruct* RowStruct = GetTableRowStruct(Table))
					{
						uint8* OriginalData = CreateStructInstance(RowStruct);
						if (DeserializeStructFromJson(Record.OriginalDataSnapshot, OriginalData, RowStruct))
						{
							ModifiedTableData.FindOrAdd(Record.TargetTablePath).Add(Record.TargetRowName, OriginalData);
						}
						else
						{
							DestroyStructInstance(OriginalData, RowStruct);
						}
					}
				}
			}
		}
		break;
		
	case ETableModificationType::DeleteRow:
		{
			// 恢复删除的行
			if (!Record.OriginalDataSnapshot.IsEmpty())
			{
				if (UDataTable* Table = LoadDataTable(Record.TargetTablePath))
				{
					if (UStruct* RowStruct = GetTableRowStruct(Table))
					{
						uint8* RestoredData = CreateStructInstance(RowStruct);
						if (DeserializeStructFromJson(Record.OriginalDataSnapshot, RestoredData, RowStruct))
						{
							ModifiedTableData.FindOrAdd(Record.TargetTablePath).Add(Record.TargetRowName, RestoredData);
						}
						else
						{
							DestroyStructInstance(RestoredData, RowStruct);
						}
					}
				}
			}
		}
		break;
	}
	
	return true;
}

UStruct* UUniversalDataTableModifier::GetTableRowStruct(UDataTable* Table) const
{
	if (!Table)
	{
		return nullptr;
	}

	return Table->RowStruct;
}

void UUniversalDataTableModifier::OnAutoSaveTimer()
{
	if (Config.bAutoSaveChanges && IsTableModified(""))
	{
		SaveAllModifications();
	}
}

void UUniversalDataTableModifier::CleanupOldHistory()
{
	if (ModificationHistory.Num() > Config.MaxHistoryRecords)
	{
		int32 RemoveCount = ModificationHistory.Num() - Config.MaxHistoryRecords;
		ModificationHistory.RemoveAt(0, RemoveCount);
		UE_LOG(LogTemp, Log, TEXT("UniversalDataTableModifier: 清理了 %d 条旧的历史记录"), RemoveCount);
	}
}