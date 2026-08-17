// ==========================================
// FActivityDataTableService 实现 (v231 — 大厂架构)
// ==========================================
// v231 重构要点:
//   1. 静态 TMap → 实例 TMap (由 UActivityDataTableService 持有)
//   2. TObjectPtr → TStrongObjectPtr (AddToRoot 保证 GC 永不回收)
//   3. 移除所有"先校验再重载"的兜底逻辑 — 强引用保证指针永远有效
//   4. 加载失败 = Log Error + return nullptr, 严禁静默
// ==========================================
#include "Data/FActivityDataTableService.h"
#include "Engine/DataTable.h"
#include "Logs/MetalSlugLogChannels.h"

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
		UE_LOG(LogTemp, Error,
			TEXT("[ActivityDT] 加载失败: %s (路径: %s). 请检查资产是否存在, 路径前缀是否正确"),
			*TableID.ToString(), *AssetPath);
		return nullptr;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[ActivityDT] 已加载: %s (Rows: %d)"), *TableID.ToString(), Loaded->GetRowMap().Num());
	return Loaded;
}

void FActivityDataTableService::ReleaseTable(FName TableID)
{
	// TStrongObjectPtr 析构时自动调 RemoveFromRoot, 这里只需让 TMap 移除 entry
	if (TStrongObjectPtr<UDataTable>* Found = Cache.Find(TableID))
	{
		Found->Reset(); // 强制析构, 触发 RemoveFromRoot
		Cache.Remove(TableID);
	}
}

UDataTable* FActivityDataTableService::Get(FName TableID)
{
	// v231 强引用保证: 缓存命中即返回, 指针永远有效
	// 加载失败是唯一返回 nullptr 的路径, 不再有"假阳性"兜底
	if (TStrongObjectPtr<UDataTable>* Cached = Cache.Find(TableID))
	{
		UDataTable* RawPtr = Cached->Get();
		// 强引用保证 RawPtr 永远有效, 不再加 IsValid 校验
		// 校验 = 反模式, 强引用 = 单一真理源
		return RawPtr;
	}

	// 首次访问: 同步加载 + 强引用缓存
	UDataTable* Loaded = LoadTableInternal(TableID);
	if (!Loaded)
	{
		// 加载失败: Log Error 在 LoadTableInternal 内部已做, 这里直接返回
		return nullptr;
	}

	// TStrongObjectPtr 构造函数内部 AddToRoot, GC 永远不会回收
	Cache.Add(TableID, TStrongObjectPtr<UDataTable>(Loaded));
	return Loaded;
}

void FActivityDataTableService::ReloadAll()
{
	UE_LOG(LogTemp, Warning, TEXT("[ActivityDT] ReloadAll: 释放 %d 张表的强引用并重新加载"), Cache.Num());

	// 1. 释放所有强引用 (TStrongObjectPtr 析构会 RemoveFromRoot)
	for (auto& Pair : Cache)
	{
		Pair.Value.Reset();
	}
	Cache.Empty();

	// 2. 按已知 ID 重新加载 (Get 内部会建立新的强引用)
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

TArray<FName> FActivityDataTableService::GetMissingTables() const
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
		if (!Cache.Contains(ID))
		{
			Missing.Add(ID);
		}
	}
	return Missing;
}
