/**
 * @file ActivityDataTableModifierBPLibrary.h
 * @brief 活动系统动态表修改器蓝图函数库
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 * 
 * @details 提供ActivitySubsystem动态表修改功能的蓝图接口
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UI/Activity/Core/ActivitySubsystem.h"
#include "ActivityDataTableModifierBPLibrary.generated.h"

/**
 * @brief 活动系统动态表修改器蓝图函数库
 * @details 提供在蓝图中操作动态表修改器的便捷接口
 */
UCLASS()
class METALSLUG01_API UActivityDataTableModifierBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * @brief 获取活动子系统的动态表修改器
	 * @param WorldContext 世界上下文
	 * @return 动态表修改器实例
	 */
	UFUNCTION(BlueprintCallable, Category = "Activity DataTable Modifier", meta = (WorldContext = "WorldContext"))
	static UUniversalDataTableModifier* GetActivityDataTableModifier(UObject* WorldContext);

	/**
	 * @brief 修改每日登录配置
	 * @param WorldContext 世界上下文
	 * @param ActivityID 活动ID
	 * @param DayIndex 天数索引
	 * @param NewRewardCount 新奖励数量
	 * @param bPersistent 是否持久化
	 * @return 是否修改成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Activity DataTable Modifier", meta = (WorldContext = "WorldContext"))
	static bool ModifyDailyLoginReward(UObject* WorldContext, int32 ActivityID, int32 DayIndex, int32 NewRewardCount, bool bPersistent = true);

	/**
	 * @brief 修改物品详情
	 * @param WorldContext 世界上下文
	 * @param ItemID 物品ID
	 * @param NewItemName 新物品名称
	 * @param bPersistent 是否持久化
	 * @return 是否修改成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Activity DataTable Modifier", meta = (WorldContext = "WorldContext"))
	static bool ModifyItemDetail(UObject* WorldContext, int32 ItemID, const FString& NewItemName, bool bPersistent = true);

	/**
	 * @brief 获取修改后的每日登录配置
	 * @param WorldContext 世界上下文
	 * @param ActivityID 活动ID
	 * @return 配置数组
	 */
	UFUNCTION(BlueprintCallable, Category = "Activity DataTable Modifier", meta = (WorldContext = "WorldContext"))
	static TArray<FDailyLoginConfigRow*> GetModifiedDailyLoginConfigs(UObject* WorldContext, int32 ActivityID);

	/**
	 * @brief 获取修改后的指定天数奖励
	 * @param WorldContext 世界上下文
	 * @param ActivityID 活动ID
	 * @param Day 天数
	 * @return 奖励配置数组
	 */
	UFUNCTION(BlueprintCallable, Category = "Activity DataTable Modifier", meta = (WorldContext = "WorldContext"))
	static TArray<FDailyLoginConfigRow*> GetModifiedRewardsByDay(UObject* WorldContext, int32 ActivityID, int32 Day);

	/**
	 * @brief 获取修改后的物品详情
	 * @param WorldContext 世界上下文
	 * @param ItemID 物品ID
	 * @return 物品详情
	 */
	UFUNCTION(BlueprintCallable, Category = "Activity DataTable Modifier", meta = (WorldContext = "WorldContext"))
	static const FItemDetailRow* GetModifiedItemDetail(UObject* WorldContext, int32 ItemID);

	/**
	 * @brief 保存所有修改
	 * @param WorldContext 世界上下文
	 * @return 是否保存成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Activity DataTable Modifier", meta = (WorldContext = "WorldContext"))
	static bool SaveAllDataTableModifications(UObject* WorldContext);

	/**
	 * @brief 加载已保存的修改
	 * @param WorldContext 世界上下文
	 * @return 是否加载成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Activity DataTable Modifier", meta = (WorldContext = "WorldContext"))
	static bool LoadSavedDataTableModifications(UObject* WorldContext);
};