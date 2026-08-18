// ==========================================
// 活动 DataTable 集中加载服务 - 头文件声明 (v231 重构 — 大厂架构)
// ==========================================
// 目的: 消除散落的硬编码 /Game/UI/Activity/Data/... 路径
//
// 文件功能总览:
//   - 定义 ActivityDataTable 命名空间,集中管理 DataTable 资产标识符
//   - 声明 FActivityDataTableService 结构体(继承自 v231 重构,实例服务)
//   - 提供 Get / ReloadAll / GetMissingTables / GetRowsSafe / FindRowByIdSafe 等模板 API
//
// 大厂架构角色:
//   - 单一真理源: 任何 DataTable 访问必须走本 Service,不允许子系统再持有 CachedConfigTable
//   - 实例服务: 由 UActivityDataTableService(UObject 子对象)持有生命周期
//   - 强引用缓存: TStrongObjectPtr 内部 AddToRoot,GC 永不回收,野指针问题彻底消失
// ==========================================
//
// v231 重构要点(根治静态 TMap 野指针崩溃):
//   1. 静态服务 → 实例服务,由 UActivityDataTableService(UObject 子对象)持有生命周期
//   2. TMap<FName, TObjectPtr<UDataTable>> → TMap<FName, TStrongObjectPtr<UDataTable>>
//      - TStrongObjectPtr 内部调 AddToRoot,GC 永远不会回收,野指针问题彻底消失
//   3. 移除一切"防御性 IsValid"—— 强引用保证指针永远有效,再校验是反模式
//   4. 模板实现仍然在 .h(GetRowsSafe / FindRowByIdSafe),避免编译期耦合
//   5. 单一真理源:任何 DataTable 访问必须走本 Service,不允许子系统再持有 CachedConfigTable
//
// 调用约定:
//   - 调用方通过 UActivitySubsystem::GetDataTableService() 获取本服务实例
//   - 调用方拿到指针后无需判空(Subsystem 由 UGameInstance 持有,生命周期 = GameInstance)
//
// 未来可扩展:
//   - 异步加载(FStreamableManager 替代 LoadObject)
//   - 进度回报(OnAllTablesLoaded 委托,UI 可订阅启动期进度)
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "UObject/NameTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "Engine/DataTable.h" // 模板实现需要 UDataTable 完整定义
#include "Logging/LogMacros.h"

class UDataTableService;

// 活动 DataTable 标识符 (避免各处散落 FName 字面量)
namespace ActivityDataTable
{
	static const FName ActivityInfo         = TEXT("DT_ActivityInfoRow");
	static const FName DailyLoginConfig     = TEXT("DT_DailyLoginConfigRow");
	static const FName ItemDetail           = TEXT("DT_ItemDetailRow");
	/** 宝箱道具表 — 母体变异的奖励道具 ItemID + 数量 */
	static const FName TreasureBoxItem      = TEXT("DT_TreasureBoxItemRow");
	/** 每日升级奖励表 — 宝箱 ID + 可获得道具列表 */
	static const FName DailyUpgradeReward   = TEXT("DT_DailyUpgradeRewardConfigRow");
}

/**
 * @struct FActivityDataTableService
 * @brief 活动 DataTable 集中加载器 (实例服务, 由调用方持有生命周期)
 * @details 内部以 TMap<FName, TStrongObjectPtr<UDataTable>> 缓存加载结果
 *
 * 设计原则 (v231):
 *   - 强引用保证 GC 安全: TStrongObjectPtr 内部 AddToRoot,GC 永不回收
 *   - 零 IsValid 防御: 强引用保证 RawPtr 永远有效,再校验是反模式
 *   - 零兜底: 失败立即 Log Error + return nullptr,无静默默认
 *   - 单一真理源: 任何 DataTable 访问必须走本 Service
 *
 * 生命周期 (v231):
 *   - 由 UActivitySubsystem::DataTableService (UObject 子对象) 持有
 *   - UObject 子对象随 UActivitySubsystem 一同 GC, 内部 TStrongObjectPtr 自动析构
 *   - 不需要手动 Shutdown
 */
struct METALSLUG01_API FActivityDataTableService
{
public:
	FActivityDataTableService() = default;
	~FActivityDataTableService() = default;

	// 禁止拷贝/移动: TStrongObjectPtr 与 UObject GC 体系绑定, 拷贝会让多个 Service 持有同一对象的引用
	FActivityDataTableService(const FActivityDataTableService&) = delete;
	FActivityDataTableService& operator=(const FActivityDataTableService&) = delete;

	/**
	 * @brief 根据表 ID 获取 DataTable (首次访问同步加载, 后续命中强引用缓存)
	 * @param TableID 活动 DataTable 标识 (见 ActivityDataTable 命名空间)
	 * @return 加载成功返回 UDataTable*; 失败返回 nullptr
	 *
	 * 行为合约 (v231):
	 *   - 永远不返回失效指针(强引用保证)
	 *   - 加载失败时 Log Error + return nullptr,绝不静默
	 *   - 路径: /Game/UI/Activity/Data/<TableID>.<TableID>
	 */
	UDataTable* Get(FName TableID);

	/**
	 * @brief 强制重新加载所有缓存的 DataTable (调试用, 修改 .uasset 后想立即生效)
	 * @details Release 旧引用 → DropRoot → 重新 LoadObject + AddToRoot
	 */
	void ReloadAll();

	/**
	 * @brief 检查所有已知表是否都已成功加载
	 * @return 缺失的表 ID 列表 (空表示全部加载成功)
	 */
	TArray<FName> GetMissingTables() const;

	/**
	 * @brief 防御性遍历 DataTable 所有行 (使用 GetRowNames + FindRow 安全路径)
	 * @tparam T 行 struct 类型
	 * @tparam Predicate 谓词回调: bool(const T& Row) -> 是否收集该行
	 *
	 * 单一真理源: 外部调用方(包括 ActivitySubsystem / UpgradeActivitySubsystem)
	 *            必须通过 Service 实例调用本方法, 严禁子系统自己再持有 DataTable 缓存
	 */
	template <typename T, typename Predicate>
	TArray<const T*> GetRowsSafe(FName TableID, Predicate Pred);

	/**
	 * @brief 防御性查找单个行 (按 ID 谓词)
	 */
	template <typename T, typename IdExtractor>
	const T* FindRowByIdSafe(FName TableID, IdExtractor Extractor, int32 TargetId);

private:
	/**
	 * @brief 强引用缓存容器
	 * @details TStrongObjectPtr 内部调 AddToRoot, GC 永不回收这些 DataTable
	 *          即使 DataTable 资产被 UE 视为"未引用", GC 也不会回收
	 */
	TMap<FName, TStrongObjectPtr<UDataTable>> Cache;

	/**
	 * @brief 释放单个 DataTable 的强引用
	 */
	void ReleaseTable(FName TableID);

	/**
	 * @brief 单个表的实际加载实现
	 * @details 集中处理路径拼接 + LoadObject + 错误日志
	 */
	UDataTable* LoadTableInternal(FName TableID);

	/**
	 * @brief 构建资源的绝对路径 (UE 资源路径必须带 .AssetName 后缀)
	 */
	static FString BuildAssetPath(FName TableID);
};

// ============================================================
// 模板实现 (v231)
// ⚠️ 必须放在 .h 末尾 (因为是 template, 调用方需要看见实现代码)
//
// v231 改进:
//   - 委托给 Service.Get(TableID) — 强引用保证返回值必然非空或 nullptr
//   - 失败行为: Log Error + return 空容器,无兜底
// ============================================================

template <typename T, typename Predicate>
TArray<const T*> FActivityDataTableService::GetRowsSafe(FName TableID, Predicate Pred)
{
	TArray<const T*> Result;

	UDataTable* Table = Get(TableID);
	if (!Table)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ActivityDT] GetRowsSafe: '%s' DataTable 加载失败, 返回空数组"), *TableID.ToString());
		return Result;
	}

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
	UDataTable* Table = Get(TableID);
	if (!Table)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ActivityDT] FindRowByIdSafe: '%s' DataTable 加载失败, 返回 nullptr"), *TableID.ToString());
		return nullptr;
	}

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

	UE_LOG(LogTemp, Warning,
		TEXT("[ActivityDT] FindRowByIdSafe: '%s' 遍历 %d 行未找到 TargetId=%d"),
		*TableID.ToString(), RowNames.Num(), TargetId);
	return nullptr;
}
