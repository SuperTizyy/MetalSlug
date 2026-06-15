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

class UDataTable;
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

	/** 全局缓存: TableID -> 已加载的 UDataTable* */
	static TMap<FName, TObjectPtr<UDataTable>>& GetCache();
};
