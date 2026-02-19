/**
 * @file DataTableModifierAdvancedExample.h
 * @brief 动态表修改器高级使用示例
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 * 
 * @details 展示使用FJsonObjectConverter后的增强功能示例
 */

#pragma once

#include "CoreMinimal.h"
#include "Tools/UniversalDataTableModifier.h"
#include "DataTableModifierAdvancedExample.generated.h"

/**
 * @brief 高级数据结构示例
 * @details 演示复杂数据类型的序列化/反序列化
 */
USTRUCT(BlueprintType)
struct FAdvancedExampleData : public FTableRowBase
{
	GENERATED_BODY()

public:
	/** 基础字段 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Basic")
	int32 ID;

	/** 字符串字段 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Basic")
	FString Name;

	/** 浮点数字段 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Basic")
	float Value;

	/** 布尔字段 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Basic")
	bool IsActive;

	/** 数组字段 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collections")
	TArray<int32> NumberArray;

	/** 字符串数组 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collections")
	TArray<FString> StringArray;

	/** 嵌套结构体 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nested")
	struct FNestedData
	{
		UPROPERTY(EditAnywhere, BlueprintReadWrite)
		FString NestedName;

		UPROPERTY(EditAnywhere, BlueprintReadWrite)
		int32 NestedValue;

		FNestedData() : NestedValue(0) {}
	} NestedStruct;

	/** 构造函数 */
	FAdvancedExampleData() 
		: ID(0), Value(0.0f), IsActive(false)
	{
		// 初始化示例数据
		NumberArray.Add(1);
		NumberArray.Add(2);
		NumberArray.Add(3);
		
		StringArray.Add(TEXT("Hello"));
		StringArray.Add(TEXT("World"));
		
		NestedStruct.NestedName = TEXT("Nested Example");
		NestedStruct.NestedValue = 42;
	}
};

/**
 * @brief 动态表修改器高级示例类
 * @details 展示FJsonObjectConverter的强大功能
 */
UCLASS()
class METALSLUG01_API UDataTableModifierAdvancedExample : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * @brief 演示复杂数据类型的序列化
	 * @param WorldContext 世界上下文
	 */
	UFUNCTION(BlueprintCallable, Category = "Advanced Examples")
	static void DemoComplexSerialization(UObject* WorldContext);

	/**
	 * @brief 演示数组和嵌套结构体处理
	 * @param WorldContext 世界上下文
	 */
	UFUNCTION(BlueprintCallable, Category = "Advanced Examples")
	static void DemoArrayAndNestedStructs(UObject* WorldContext);

	/**
	 * @brief 性能对比测试
	 * @param WorldContext 世界上下文
	 */
	UFUNCTION(BlueprintCallable, Category = "Advanced Examples")
	static void DemoPerformanceComparison(UObject* WorldContext);

	/**
	 * @brief 错误处理和回退机制演示
	 * @param WorldContext 世界上下文
	 */
	UFUNCTION(BlueprintCallable, Category = "Advanced Examples")
	static void DemoErrorHandling(UObject* WorldContext);

private:
	/**
	 * @brief 创建高级示例配置
	 * @return 配置结构
	 */
	static FDataTableModificationConfig CreateAdvancedConfig();
};