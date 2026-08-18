// ==========================================
// FActivityDataTableService 实现文件 (v231 — 大厂架构)
// ==========================================
//
// 文件功能总览:
//   - BuildAssetPath: 拼接资产绝对路径(/Game/UI/Activity/Data/<TableID>.<TableID>)
//   - LoadTableInternal: 单个 DataTable 的实际加载实现,失败 Log Error + return nullptr
//   - ReleaseTable: 释放单张 DataTable 的强引用(触发 RemoveFromRoot)
//   - Get: 强引用缓存查询(命中即返回,未命中同步加载并缓存)
//   - ReloadAll: 调试用,强制重载所有缓存表
//   - GetMissingTables: 返回缺失表 ID 列表(启动期完整性检查)
//
// 大厂架构角色:
//   - 单一真理源: 任何活动 DataTable 访问必须走本 Service
//   - 强引用安全: TStrongObjectPtr 内部 AddToRoot,GC 永不回收这些 DataTable
//   - 零兜底: 加载失败立即 Log Error + return nullptr,严禁静默
//
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

/**
 * @brief 构造 DataTable 资产绝对路径
 * @param TableID 活动 DataTable 标识 (例如 "DT_ActivityInfoRow")
 * @return 资产路径字符串,格式 /Game/UI/Activity/Data/<TableID>.<TableID>
 * @note UE 规范: 引用 UDataTable 时,路径必须带 .AssetName 后缀(LoadObject 才能命中)
 */
FString FActivityDataTableService::BuildAssetPath(FName TableID)
{
	// 资产路径格式: /Game/UI/Activity/Data/<TableID>.<TableID>
	// UE 规范: 引用 UDataTable 时, 路径必须带 .AssetName 后缀
	return FString::Printf(TEXT("/Game/UI/Activity/Data/%s.%s"), *TableID.ToString(), *TableID.ToString());
}

/**
 * @brief 单个 DataTable 的实际加载实现
 * @param TableID 活动 DataTable 标识
 * @return 加载成功返回 UDataTable*,失败返回 nullptr + Log Error
 * @note 大厂原则: 加载失败立即报错,绝不静默默认
 */
UDataTable* FActivityDataTableService::LoadTableInternal(FName TableID)
{
	const FString AssetPath = BuildAssetPath(TableID);  // 拼接资产绝对路径

	// 同步加载 — LoadObject 失败立即返回 nullptr
	UDataTable* Loaded = LoadObject<UDataTable>(nullptr, *AssetPath);
	if (!Loaded)
	{
		// 加载失败: 显式报错,告诉调用方资产不存在或路径错
		UE_LOG(LogTemp, Error,
			TEXT("[ActivityDT] 加载失败: %s (路径: %s). 请检查资产是否存在, 路径前缀是否正确"),
			*TableID.ToString(), *AssetPath);
		return nullptr;
	}

	// 加载成功: 打印诊断日志(行数方便排查数据完整性)
	UE_LOG(LogTemp, Log,
		TEXT("[ActivityDT] 已加载: %s (Rows: %d)"), *TableID.ToString(), Loaded->GetRowMap().Num());
	return Loaded;
}

/**
 * @brief 释放单个 DataTable 的强引用
 * @param TableID 待释放的表标识
 * @note TStrongObjectPtr 析构时自动 RemoveFromRoot,这里只需从 TMap 移除
 */
void FActivityDataTableService::ReleaseTable(FName TableID)
{
	// TStrongObjectPtr 析构时自动调 RemoveFromRoot, 这里只需让 TMap 移除 entry
	if (TStrongObjectPtr<UDataTable>* Found = Cache.Find(TableID))
	{
		Found->Reset(); // 强制析构, 触发 RemoveFromRoot
		Cache.Remove(TableID);
	}
}

/**
 * @brief 根据表 ID 获取 DataTable (强引用缓存命中 / 同步加载)
 * @param TableID 活动 DataTable 标识
 * @return 强引用保证的 RawPtr,加载失败返回 nullptr
 * @note v231: 不再加 IsValid 校验 — 强引用 = 单一真理源
 */
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

/**
 * @brief 强制重新加载所有已知 DataTable (调试用, .uasset 改动后想立即生效)
 * @note 流程: 释放旧强引用 → DropRoot → 按已知 ID 列表重新 LoadObject + AddToRoot
 */
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
		ActivityDataTable::ActivityInfo,         // DT_ActivityInfoRow
		ActivityDataTable::DailyLoginConfig,     // DT_DailyLoginConfigRow
		ActivityDataTable::ItemDetail,           // DT_ItemDetailRow
		ActivityDataTable::TreasureBoxItem,      // DT_TreasureBoxItemRow
		ActivityDataTable::DailyUpgradeReward,   // DT_DailyUpgradeRewardConfigRow
	};
	// 遍历已知表 ID 列表,逐个 Get 触发同步加载
	for (const FName& ID : KnownTables)
	{
		Get(ID);
	}
}

/**
 * @brief 检查所有已知表是否都已成功加载
 * @return 缺失的表 ID 列表 (空数组表示全部加载成功)
 * @note 大厂原则: 启动期完整性检查,缺失立即暴露而非静默
 */
TArray<FName> FActivityDataTableService::GetMissingTables() const
{
	TArray<FName> Missing;  // 缺失表 ID 收集器
	// 已知表 ID 列表 (单一真理源,改这里 = 改完整性检查范围)
	static const FName KnownTables[] = {
		ActivityDataTable::ActivityInfo,         // DT_ActivityInfoRow
		ActivityDataTable::DailyLoginConfig,     // DT_DailyLoginConfigRow
		ActivityDataTable::ItemDetail,           // DT_ItemDetailRow
		ActivityDataTable::TreasureBoxItem,      // DT_TreasureBoxItemRow
		ActivityDataTable::DailyUpgradeReward,   // DT_DailyUpgradeRewardConfigRow
	};
	// 遍历所有已知 ID,Cache 不包含则视为缺失
	for (const FName& ID : KnownTables)
	{
		if (!Cache.Contains(ID))
		{
			Missing.Add(ID);
		}
	}
	return Missing;
}
