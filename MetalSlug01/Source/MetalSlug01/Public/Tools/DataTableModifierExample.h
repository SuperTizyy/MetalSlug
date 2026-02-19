/**
 * @file DataTableModifierExample.h
 * @brief 动态表修改器使用示例和工具类
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 * 
 * @details 提供动态表修改器的实际使用示例和便捷工具方法
 */

#pragma once

#include "CoreMinimal.h"
#include "Tools/UniversalDataTableModifier.h"
#include "DataTableModifierExample.generated.h"

/**
 * @brief 动态表修改器使用示例类
 * @details 展示如何在实际项目中使用万能动态表修改器
 */
UCLASS()
class METALSLUG01_API UDataTableModifierExample : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * @brief 演示基本的表修改操作
	 * @param WorldContext 世界上下文
	 */
	UFUNCTION(BlueprintCallable, Category = "DataTable Modifier Examples")
	static void DemoBasicModification(UObject* WorldContext);

	/**
	 * @brief 演示事务性操作
	 * @param WorldContext 世界上下文
	 */
	UFUNCTION(BlueprintCallable, Category = "DataTable Modifier Examples")
	static void DemoTransactionalModification(UObject* WorldContext);

	/**
	 * @brief 演示持久化修改
	 * @param WorldContext 世界上下文
	 */
	UFUNCTION(BlueprintCallable, Category = "DataTable Modifier Examples")
	static void DemoPersistentModification(UObject* WorldContext);

	/**
	 * @brief 演示批量修改操作
	 * @param WorldContext 世界上下文
	 */
	UFUNCTION(BlueprintCallable, Category = "DataTable Modifier Examples")
	static void DemoBatchModification(UObject* WorldContext);

	/**
	 * @brief 演示修改历史和回滚
	 * @param WorldContext 世界上下文
	 */
	UFUNCTION(BlueprintCallable, Category = "DataTable Modifier Examples")
	static void DemoHistoryAndRollback(UObject* WorldContext);

	/**
	 * @brief 获取修改器实例
	 * @param WorldContext 世界上下文
	 * @return 修改器实例
	 */
	static UUniversalDataTableModifier* GetModifierInstance(UObject* WorldContext);

private:
	/**
	 * @brief 创建示例配置
	 * @return 配置结构
	 */
	static FDataTableModificationConfig CreateExampleConfig();
};