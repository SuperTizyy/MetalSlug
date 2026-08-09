// ==========================================
// FActivityDataTableService 实现
// ==========================================
#include "Data/FActivityDataTableService.h"
#include "Engine/DataTable.h"
#include "Logs/MetalSlugLogChannels.h"

TMap<FName, TObjectPtr<UDataTable>>& FActivityDataTableService::GetCache()
{
	// 静态局部变量, 首次访问时构造, 进程结束时析构
	// 注意: 这里使用 TObjectPtr 仅为了 GC 友好, 实际访问仍走 TMap
	static TMap<FName, TObjectPtr<UDataTable>> Cache;
	return Cache;
}

FString FActivityDataTableService::BuildAssetPath(FName TableID)
{
	// 资产路径格式: /Game/UI/Activity/Data/<TableID>.<TableID>
	// UE 规范: 引用 UDataTable 时, 路径必须带 .AssetName 后缀
	return FString::Printf(TEXT("/Game/UI/Activity/Data/%s.%s"), *TableID.ToString(), *TableID.ToString());
}

UDataTable* FActivityDataTableService::LoadTableInternal(FName TableID)
{
	const FString AssetPath = BuildAssetPath(TableID);

	UDataTable* Loaded = LoadObject<UDataTable>(nullptr, *AssetPath);
	if (!Loaded)
	{
		UE_LOG(LogTemp, Error, TEXT("[ActivityDT] 加载失败: %s (路径: %s)"), *TableID.ToString(), *AssetPath);
		return nullptr;
	}

	UE_LOG(LogTemp, Log, TEXT("[ActivityDT] 已加载: %s (Rows: %d)"), *TableID.ToString(), Loaded->GetRowMap().Num());
	return Loaded;
}

UDataTable* FActivityDataTableService::Get(FName TableID)
{
	// 1. 命中缓存: 校验是否仍有效 (UPROPERTY 的 TObjectPtr 在 GC 时会自动 null)
	//    但缓存的 TMap 不在 UPROPERTY 体系, GC 不会自动清理 entry
	//    → 必须手动校验 + IsValid 检测, 失效时自动重载
	if (TObjectPtr<UDataTable>* Cached = GetCache().Find(TableID))
	{
		UDataTable* RawPtr = Cached->Get();
		if (IsValid(RawPtr))
		{
			// 额外防御: RowStruct 也不能失效 (DataTable 资产重载/热刷新后)
			if (IsValid(RawPtr->GetRowStruct()))
			{
				return RawPtr;
			}
			UE_LOG(LogTemp, Warning,
				TEXT("[ActivityDT] Get: '%s' DataTable.RowStruct 失效, 强制重新加载"),
				*TableID.ToString());
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[ActivityDT] Get: '%s' 缓存中的 DataTable 已失效 (GC/unload), 强制重新加载"),
				*TableID.ToString());
		}
		// 失效: 清理 entry, 重新走 LoadTableInternal
		GetCache().Remove(TableID);
	}

	// 2. 首次访问 / 失效重载: 同步加载并写入缓存
	UDataTable* Loaded = LoadTableInternal(TableID);
	if (Loaded)
	{
		GetCache().Add(TableID, Loaded);
	}
	return Loaded;
}

void FActivityDataTableService::ReloadAll()
{
	UE_LOG(LogTemp, Warning, TEXT("[ActivityDT] ReloadAll: 清空缓存并重新加载 %d 张表"), GetCache().Num());
	for (auto& Pair : GetCache())
	{
		Pair.Value = nullptr; // 解除引用, 等待 GC
	}
	GetCache().Empty();

	// 按已知 ID 重新加载一次
	static const FName KnownTables[] = {
		ActivityDataTable::ActivityInfo,
		ActivityDataTable::DailyLoginConfig,
		ActivityDataTable::ItemDetail,
		ActivityDataTable::TreasureBoxItem,
		ActivityDataTable::DailyUpgradeReward,
	};
	for (const FName& ID : KnownTables)
	{
		Get(ID);
	}
}

TArray<FName> FActivityDataTableService::GetMissingTables()
{
	TArray<FName> Missing;
	static const FName KnownTables[] = {
		ActivityDataTable::ActivityInfo,
		ActivityDataTable::DailyLoginConfig,
		ActivityDataTable::ItemDetail,
		ActivityDataTable::TreasureBoxItem,
		ActivityDataTable::DailyUpgradeReward,
	};
	for (const FName& ID : KnownTables)
	{
		if (GetCache().Find(ID) == nullptr)
		{
			Missing.Add(ID);
		}
	}
	return Missing;
}

void FActivityDataTableService::Shutdown()
{
	UE_LOG(LogTemp, Log, TEXT("[ActivityDT] Shutdown: 清理 %d 张缓存表"), GetCache().Num());
	GetCache().Empty();
}

// 注意: 模板方法 GetRowsSafe / FindRowByIdSafe 的实现已搬到 .h 末尾 (因模板必须在头文件中可见)
