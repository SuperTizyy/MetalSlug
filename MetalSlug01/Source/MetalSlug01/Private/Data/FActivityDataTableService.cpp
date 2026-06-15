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
		UE_LOG(LogAssetLoad, Error, TEXT("[ActivityDT] 加载失败: %s (路径: %s)"), *TableID.ToString(), *AssetPath);
		return nullptr;
	}

	UE_LOG(LogAssetLoad, Log, TEXT("[ActivityDT] 已加载: %s (Rows: %d)"), *TableID.ToString(), Loaded->GetRowMap().Num());
	return Loaded;
}

UDataTable* FActivityDataTableService::Get(FName TableID)
{
	// 1. 命中缓存直接返回
	if (TObjectPtr<UDataTable>* Cached = GetCache().Find(TableID))
	{
		return Cached->Get();
	}

	// 2. 首次访问: 同步加载并写入缓存
	UDataTable* Loaded = LoadTableInternal(TableID);
	if (Loaded)
	{
		GetCache().Add(TableID, Loaded);
	}
	return Loaded;
}

void FActivityDataTableService::ReloadAll()
{
	UE_LOG(LogAssetLoad, Warning, TEXT("[ActivityDT] ReloadAll: 清空缓存并重新加载 %d 张表"), GetCache().Num());
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
	UE_LOG(LogAssetLoad, Log, TEXT("[ActivityDT] Shutdown: 清理 %d 张缓存表"), GetCache().Num());
	GetCache().Empty();
}
