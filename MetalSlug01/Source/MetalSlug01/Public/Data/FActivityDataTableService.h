// ==========================================
// 活动 DataTable 集中加载服务
// ==========================================
// 目的: 消除散落在 7+ 个 .cpp 中的硬编码 /Game/UI/Activity/Data/DT_xxx 路径
//
// 优势:
//   1. 单一职责: 所有活动表加载/缓存逻辑集中
//   2. 路径集中: 资产路径改一次, 全局生效
//   3. 错误聚合: 启动期一次性检查所有表是否加载成功
//   4. 性能: 首次访问 LoadSynchronous, 后续直接返回缓存
//   5. 可测试: 提供 ReloadAll() 用于调试期强制重新加载
//
// 注意事项:
//   - 当前实现: 同步加载。后续可改为 FStreamableManager 异步
//   - 当前实现: 全局 static 缓存。如未来支持多 GameInstance 需改为 WorldSubsystem
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "UObject/NameTypes.h"
#include "Templates/SubclassOf.h"
#include "Engine/DataTable.h"  // ⚠️ 模板实现需要 UDataTable 完整定义 (模板必须在 .h 中可见)
#include "Logging/LogMacros.h"  // ⚠️ 模板函数中用到 UE_LOG, 需要 LogMacros.h

class UObject;

// 活动 DataTable 标识符 (避免各处散落 FName 字面量)
namespace ActivityDataTable
{
	static const FName ActivityInfo         = TEXT("DT_ActivityInfoRow");
	static const FName DailyLoginConfig     = TEXT("DT_DailyLoginConfigRow");
	static const FName ItemDetail           = TEXT("DT_ItemDetailRow");
	static const FName TreasureBoxItem      = TEXT("DT_TreasureBoxItemRow");
	static const FName DailyUpgradeReward   = TEXT("DT_DailyUpgradeRewardConfigRow");
}

/**
 * @struct FActivityDataTableService
 * @brief 活动 DataTable 集中加载器 (静态工具类)
 * @details 内部以 TMap<FName, UDataTable*> 缓存加载结果
 */
struct METALSLUG01_API FActivityDataTableService
{
	public:
	FActivityDataTableService() = default;
	~FActivityDataTableService() = default;

	/**
	 * @brief 内部缓存访问 (供 cpp 实现使用)
	 */
	static TMap<FName, TObjectPtr<UDataTable>>& GetCache();

	/**
	 * @brief 根据表 ID 获取 DataTable (首次访问同步加载, 后续命中缓存)
	 * @param TableID 活动 DataTable 标识 (见 ActivityDataTable 命名空间)
	 * @return 加载成功返回 UDataTable*; 失败返回 nullptr
	 */
	static UDataTable* Get(FName TableID);

	/**
	 * @brief 强制重新加载所有缓存的 DataTable (调试用, 修改 .uasset 后想立即生效)
	 */
	static void ReloadAll();

	/**
	 * @brief 检查所有已知表是否都已成功加载 (启动期完整性检查)
	 * @return 缺失的表 ID 列表 (空表示全部加载成功)
	 */
	static TArray<FName> GetMissingTables();

	/**
	 * @brief 关闭子系统时清理缓存 (GC 友好)
	 */
	static void Shutdown();

/**
 * @brief 防御性遍历 DataTable 所有行 (使用 GetRowNames + FindRow 安全路径)
 *
 * ⚠️ 为什么不直接用 DataTable->GetAllRows<T>()?
 *   原因 (2026-08-10 v4 崩溃): 在 DataTable 资产热刷新/编辑器重载/GC 之后,
 *   `RowMap` 里的 `uint8*` 数据指针会变成 dangling,GetAllRows 内部的
 *   `OutRowArray.Reserve(...)` 调用会访问到已被 GC 释放的内存,导致
 *   EXCEPTION_ACCESS_VIOLATION (崩溃地址 0x0 或 0xffffffffffffffff)。
 *   修复策略: 用 GetRowNames + FindRow 单条查询路径,避开 GetAllRows
 *   内部潜在的 dangling RowMap 问题。
 *
 * @tparam T 行 struct 类型 (如 FActivityInfoRow)
 * @tparam Predicate 谓词回调: bool(const T& Row) -> 是否收集该行
 * @param TableID 活动 DataTable 标识
 * @param Predicate 用于过滤/收集行的回调
 * @return 匹配谓词的行指针数组 (永远不包含 nullptr)
 */
template <typename T, typename Predicate>
static TArray<const T*> GetRowsSafe(FName TableID, Predicate Pred);

/**
 * @brief 防御性查找单个行 (按 ID 谓词)
 *
 * @tparam T 行 struct 类型
 * @param TableID 活动 DataTable 标识
 * @param IdExtractor 从行结构提取 ID 的 lambda
 * @param TargetId 要查找的 ID
 * @return 匹配的行指针,未找到返回 nullptr
 */
template <typename T, typename IdExtractor>
static const T* FindRowByIdSafe(FName TableID, IdExtractor Extractor, int32 TargetId);

private:
	/**
	 * @brief 单个表的实际加载实现
	 * @details 集中处理路径拼接 + LoadObject + 错误日志
	 */
	static UDataTable* LoadTableInternal(FName TableID);

	/**
	 * @brief 构建资源的绝对路径 (注意 UE 资源路径必须带 .AssetName 后缀)
	 */
	static FString BuildAssetPath(FName TableID);
};

// ============================================================
// 防御性遍历/查找 Helper 实现 (v4 崩溃防御 — 2026-08-10)
// ⚠️ 必须放在 .h 末尾 (因为是 template, 调用方需要看见实现代码)
//
// 目的: 用 GetRowNames + FindRow<T> 单条路径代替 GetAllRows<T>,
//       避开 RowMap 中 dangling uint8* 指针导致的访问冲突。
// ============================================================

template <typename T, typename Predicate>
TArray<const T*> FActivityDataTableService::GetRowsSafe(FName TableID, Predicate Pred)
{
	TArray<const T*> Result;

	// 1. Get() 内部已带 IsValid 防御 (ConfigTable + RowStruct 双重校验)
	UDataTable* Table = Get(TableID);

	// 2. 兜底再校验一次 (Get 已保证, 双保险)
	if (!IsValid(Table))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ActivityDT] GetRowsSafe: '%s' DataTable 失效, 返回空数组"), *TableID.ToString());
		return Result;
	}

	const UScriptStruct* RowStruct = Table->GetRowStruct();
	if (!IsValid(RowStruct))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ActivityDT] GetRowsSafe: '%s' RowStruct 失效, 返回空数组"), *TableID.ToString());
		return Result;
	}

	// 3. 用 GetRowNames + FindRow 单条路径 (避开 GetAllRows 内部潜在 dangling)
	static const FString ContextString(TEXT("FActivityDataTableService::GetRowsSafe"));
	const TArray<FName> RowNames = Table->GetRowNames();
	Result.Reserve(RowNames.Num());

	for (const FName& RowName : RowNames)
	{
		const T* Row = Table->FindRow<T>(RowName, ContextString, /*bWarnIfRowMissing=*/false);
		if (Row && Pred(*Row))
		{
			Result.Add(Row);
		}
	}

	return Result;
}

template <typename T, typename IdExtractor>
const T* FActivityDataTableService::FindRowByIdSafe(FName TableID, IdExtractor Extractor, int32 TargetId)
{
	// 1. Get() 内部已带 IsValid 防御
	UDataTable* Table = Get(TableID);

	// 2. 兜底再校验
	if (!IsValid(Table))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ActivityDT] FindRowByIdSafe: '%s' DataTable 失效, 返回 nullptr"), *TableID.ToString());
		return nullptr;
	}

	const UScriptStruct* RowStruct = Table->GetRowStruct();
	if (!IsValid(RowStruct))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ActivityDT] FindRowByIdSafe: '%s' RowStruct 失效, 返回 nullptr"), *TableID.ToString());
		return nullptr;
	}

	// 3. GetRowNames + FindRow 单条路径
	static const FString ContextString(TEXT("FActivityDataTableService::FindRowByIdSafe"));
	const TArray<FName> RowNames = Table->GetRowNames();

	for (const FName& RowName : RowNames)
	{
		const T* Row = Table->FindRow<T>(RowName, ContextString, /*bWarnIfRowMissing=*/false);
		if (Row && Extractor(*Row) == TargetId)
		{
			return Row;
		}
	}

	return nullptr;
}
