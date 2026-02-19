/**
 * @file DataTableModifierAdvancedExample.cpp
 * @brief 动态表修改器高级使用示例实现
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 * 
 * @details 实现使用FJsonObjectConverter后的增强功能演示
 */

#include "Tools/DataTableModifierAdvancedExample.h"
#include "Engine/Engine.h"
#include "HAL/PlatformTime.h"

void UDataTableModifierAdvancedExample::DemoComplexSerialization(UObject* WorldContext)
{
	// 获取修改器实例
	UUniversalDataTableModifier* Modifier = UDataTableModifierExample::GetModifierInstance(WorldContext);
	if (!Modifier)
	{
		UE_LOG(LogTemp, Error, TEXT("AdvancedExample: 无法获取修改器实例"));
		return;
	}

	// 创建复杂数据结构
	FAdvancedExampleData ComplexData;
	ComplexData.ID = 1001;
	ComplexData.Name = TEXT("Complex Test Data");
	ComplexData.Value = 3.14159f;
	ComplexData.IsActive = true;
	
	// 数组数据已在构造函数中初始化
	
	// 序列化为JSON
	FString JsonString = UUniversalDataTableModifier::SerializeStructToJson(
		reinterpret_cast<const uint8*>(&ComplexData),
		FAdvancedExampleData::StaticStruct()
	);

	UE_LOG(LogTemp, Log, TEXT("=== 复杂数据序列化结果 ==="));
	UE_LOG(LogTemp, Log, TEXT("JSON输出: %s"), *JsonString);
	
	// 验证数组和嵌套结构是否正确序列化
	if (JsonString.Contains(TEXT("NumberArray")) && JsonString.Contains(TEXT("StringArray")))
	{
		UE_LOG(LogTemp, Log, TEXT("✅ 数组字段已正确序列化"));
	}
	
	if (JsonString.Contains(TEXT("NestedStruct")) && JsonString.Contains(TEXT("NestedName")))
	{
		UE_LOG(LogTemp, Log, TEXT("✅ 嵌套结构体已正确序列化"));
	}
}

void UDataTableModifierAdvancedExample::DemoArrayAndNestedStructs(UObject* WorldContext)
{
	UUniversalDataTableModifier* Modifier = UDataTableModifierExample::GetModifierInstance(WorldContext);
	if (!Modifier)
	{
		return;
	}

	// 创建包含复杂数据的示例
	FAdvancedExampleData OriginalData;
	OriginalData.ID = 2001;
	OriginalData.Name = TEXT("Array Test");
	OriginalData.NumberArray.Reset(); // 清空原有数据
	OriginalData.NumberArray.Add(10);
	OriginalData.NumberArray.Add(20);
	OriginalData.NumberArray.Add(30);
	
	OriginalData.StringArray.Reset();
	OriginalData.StringArray.Add(TEXT("First"));
	OriginalData.StringArray.Add(TEXT("Second"));
	OriginalData.StringArray.Add(TEXT("Third"));
	
	OriginalData.NestedStruct.NestedName = TEXT("Deep Nested");
	OriginalData.NestedStruct.NestedValue = 999;

	// 序列化
	FString Serialized = UUniversalDataTableModifier::SerializeStructToJson(
		reinterpret_cast<const uint8*>(&OriginalData),
		FAdvancedExampleData::StaticStruct()
	);

	UE_LOG(LogTemp, Log, TEXT("=== 数组和嵌套结构测试 ==="));
	UE_LOG(LogTemp, Log, TEXT("原始数据序列化: %s"), *Serialized);

	// 反序列化验证
	FAdvancedExampleData DeserializedData;
	if (UUniversalDataTableModifier::DeserializeStructFromJson(
		Serialized,
		reinterpret_cast<uint8*>(&DeserializedData),
		FAdvancedExampleData::StaticStruct()))
	{
		UE_LOG(LogTemp, Log, TEXT("✅ 反序列化成功"));
		UE_LOG(LogTemp, Log, TEXT("验证数据:"));
		UE_LOG(LogTemp, Log, TEXT("  ID: %d"), DeserializedData.ID);
		UE_LOG(LogTemp, Log, TEXT("  Name: %s"), *DeserializedData.Name);
		UE_LOG(LogTemp, Log, TEXT("  数组长度: %d"), DeserializedData.NumberArray.Num());
		UE_LOG(LogTemp, Log, TEXT("  嵌套值: %d"), DeserializedData.NestedStruct.NestedValue);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("❌ 反序列化失败"));
	}
}

void UDataTableModifierAdvancedExample::DemoPerformanceComparison(UObject* WorldContext)
{
	UUniversalDataTableModifier* Modifier = UDataTableModifierExample::GetModifierInstance(WorldContext);
	if (!Modifier)
	{
		return;
	}

	// 准备测试数据
	FAdvancedExampleData TestData;
	TestData.ID = 3001;
	TestData.Name = TEXT("Performance Test Data");
	TestData.Value = 2.71828f;
	TestData.IsActive = true;

	// 扩展数组数据以增加复杂度
	for (int32 i = 0; i < 100; ++i)
	{
		TestData.NumberArray.Add(i);
		TestData.StringArray.Add(FString::Printf(TEXT("String_%d"), i));
	}

	UE_LOG(LogTemp, Log, TEXT("=== 性能对比测试 ==="));

	// 测试多次序列化性能
	const int32 TestIterations = 1000;
	double StartTime = FPlatformTime::Seconds();

	for (int32 i = 0; i < TestIterations; ++i)
	{
		FString JsonResult = UUniversalDataTableModifier::SerializeStructToJson(
			reinterpret_cast<const uint8*>(&TestData),
			FAdvancedExampleData::StaticStruct()
		);
		
		// 简单验证避免编译器优化
		if (JsonResult.Len() < 10)
		{
			UE_LOG(LogTemp, Warning, TEXT("Unexpected short JSON"));
		}
	}

	double EndTime = FPlatformTime::Seconds();
	double DurationMs = (EndTime - StartTime) * 1000.0;

	UE_LOG(LogTemp, Log, TEXT("FJsonObjectConverter 性能测试:"));
	UE_LOG(LogTemp, Log, TEXT("  迭代次数: %d"), TestIterations);
	UE_LOG(LogTemp, Log, TEXT("  总耗时: %.2f ms"), DurationMs);
	UE_LOG(LogTemp, Log, TEXT("  平均每次: %.4f ms"), DurationMs / TestIterations);
	UE_LOG(LogTemp, Log, TEXT("  每秒处理: %.0f 次"), TestIterations / (DurationMs / 1000.0));

	// 内存使用估算
	FString SampleJson = UUniversalDataTableModifier::SerializeStructToJson(
		reinterpret_cast<const uint8*>(&TestData),
		FAdvancedExampleData::StaticStruct()
	);
	
	UE_LOG(LogTemp, Log, TEXT("  单次JSON大小: %d 字节"), SampleJson.Len());
	UE_LOG(LogTemp, Log, TEXT("  预估内存使用: %d KB"), 
		static_cast<int32>((SampleJson.Len() * TestIterations) / 1024));
}

void UDataTableModifierAdvancedExample::DemoErrorHandling(UObject* WorldContext)
{
	UUniversalDataTableModifier* Modifier = UDataTableModifierExample::GetModifierInstance(WorldContext);
	if (!Modifier)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("=== 错误处理和回退机制演示 ==="));

	// 测试1: 空指针处理
	FString NullResult = UUniversalDataTableModifier::SerializeStructToJson(nullptr, nullptr);
	if (NullResult.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("✅ 空指针正确处理"));
	}

	// 测试2: 无效JSON反序列化
	FAdvancedExampleData InvalidData;
	FString InvalidJson = TEXT("{\"Invalid\": \"JSON\"");
	
	if (!UUniversalDataTableModifier::DeserializeStructFromJson(
		InvalidJson,
		reinterpret_cast<uint8*>(&InvalidData),
		FAdvancedExampleData::StaticStruct()))
	{
		UE_LOG(LogTemp, Log, TEXT("✅ 无效JSON正确拒绝"));
	}

	// 测试3: 缺失字段的JSON
	FAdvancedExampleData PartialData;
	FString PartialJson = TEXT("{\"ID\": 9999, \"Name\": \"Partial Data\"}");
	
	if (UUniversalDataTableModifier::DeserializeStructFromJson(
		PartialJson,
		reinterpret_cast<uint8*>(&PartialData),
		FAdvancedExampleData::StaticStruct()))
	{
		UE_LOG(LogTemp, Log, TEXT("✅ 部分字段JSON正确处理"));
		UE_LOG(LogTemp, Log, TEXT("  ID设置为: %d"), PartialData.ID);
		UE_LOG(LogTemp, Log, TEXT("  Name设置为: %s"), *PartialData.Name);
		// 未设置的字段应该保持默认值
	}

	// 测试4: 类型不匹配
	FAdvancedExampleData TypeMismatchData;
	FString TypeMismatchJson = TEXT("{\"ID\": \"This should be integer\", \"Value\": true}");
	
	if (UUniversalDataTableModifier::DeserializeStructFromJson(
		TypeMismatchJson,
		reinterpret_cast<uint8*>(&TypeMismatchData),
		FAdvancedExampleData::StaticStruct()))
	{
		UE_LOG(LogTemp, Log, TEXT("✅ 类型不匹配时的容错处理"));
	}

	UE_LOG(LogTemp, Log, TEXT("=== 错误处理测试完成 ==="));
}

FDataTableModificationConfig UDataTableModifierAdvancedExample::CreateAdvancedConfig()
{
	FDataTableModificationConfig Config;
	Config.bEnableMemoryModification = true;
	Config.bEnablePersistentModification = true;
	Config.MaxHistoryRecords = 1000;
	Config.bAutoSaveChanges = true;
	Config.AutoSaveInterval = 30.0f;
	Config.bEnableTransactionSupport = true;
	Config.TransactionTimeout = 60.0f;
	return Config;
}