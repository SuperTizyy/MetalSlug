#include "UI/Activity/Core/RedDotManager.h"
#include "UI/Activity/Core/ActivitySubsystem.h"
#include "Kismet/GameplayStatics.h"

void URedDotManager::InitializeManager(UActivitySubsystem* InSubsystem)
{
	ActivitySubsystem = InSubsystem;
	LastRefreshTime = FPlatformTime::Seconds();
	RefreshAllRedDots();
}

void URedDotManager::RefreshAllRedDots()
{
	if (!ActivitySubsystem.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("RedDotManager: ActivitySubsystem 未初始化"));
		return;
	}

	RedDotCache.Empty();
	
	// 获取所有导航项配置
	TArray<const FActivityInfoRow*> NavItems = ActivitySubsystem->GetAllNavItems();
	
	for (const FActivityInfoRow* Config : NavItems)
	{
		if (Config)
		{
			FRedDotData RedDotData = CalculateRedDotForActivity(Config);
			RedDotCache.Add(Config->ActivityID, RedDotData);
		}
	}

	LastRefreshTime = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Log, TEXT("RedDotManager: 刷新了 %d 个导航项的红点状态"), NavItems.Num());
}

FRedDotData URedDotManager::GetRedDotData(int32 ActivityId) const
{
	if (const FRedDotData* Data = RedDotCache.Find(ActivityId))
	{
		return *Data;
	}
	
	// 返回默认的无红点状态
	return FRedDotData(ERedDotType::None, 0, false, 0, FName(*FString::Printf(TEXT("%d"), ActivityId)));
}

bool URedDotManager::HasAnyRedDots() const
{
	for (const auto& Pair : RedDotCache)
	{
		if (Pair.Value.bShouldShow)
		{
			return true;
		}
	}
	return false;
}

int32 URedDotManager::GetTotalRedDotCount() const
{
	int32 TotalCount = 0;
	for (const auto& Pair : RedDotCache)
	{
		if (Pair.Value.bShouldShow && 
			(Pair.Value.DotType == ERedDotType::NumberBadge || 
			 Pair.Value.DotType == ERedDotType::ProgressBadge))
		{
			TotalCount += Pair.Value.DotValue;
		}
	}
	return TotalCount;
}

void URedDotManager::SetRedDotManually(int32 ActivityId, bool bShow, int32 Value)
{
	FRedDotData ManualData(
		bShow ? ERedDotType::SimpleDot : ERedDotType::None,
		Value,
		bShow,
		100, // 手动设置给予高优先级
		FName(*FString::Printf(TEXT("%d"), ActivityId))
	);
	
	RedDotCache.Add(ActivityId, ManualData);
	UE_LOG(LogTemp, Log, TEXT("RedDotManager: 手动设置活动 %d 的红点状态为 %s, 数值: %d"), 
		   ActivityId, bShow ? TEXT("显示") : TEXT("隐藏"), Value);
}

FRedDotData URedDotManager::CalculateRedDotForActivity(const FActivityInfoRow* Config)
{
	if (!Config)
	{
		return FRedDotData();
	}

	// 如果配置了条件函数，优先执行动态计算
	if (!Config->RedDotConditionFunction.IsNone())
	{
		int32 CalculatedValue = 0;
		if (ExecuteRedDotCondition(Config->RedDotConditionFunction, CalculatedValue))
		{
			bool bShouldShow = CalculatedValue > 0;
			return FRedDotData(
				bShouldShow ? Config->RedDotType : ERedDotType::None,
				CalculatedValue,
				bShouldShow,
				Config->RedDotPriority,
				FName(*FString::Printf(TEXT("%d"), Config->ActivityID))
			);
		}
	}

	// 使用默认计算逻辑
	return CalculateDefaultRedDot(Config);
}

bool URedDotManager::ExecuteRedDotCondition(const FName& FunctionName, int32& OutValue)
{
	// 这里可以实现通过反射调用指定函数名的逻辑
	// 或者通过委托系统来执行自定义的红点计算函数
	
	// 示例：简单的模拟实现
	if (FunctionName == FName("CheckDailyLoginRedDot"))
	{
		// 模拟每日登录红点逻辑
		OutValue = 1; // 假设有可领取的奖励
		return true;
	}
	else if (FunctionName == FName("CheckFirstMatchRedDot"))
	{
		// 模拟首胜奖励红点逻辑
		OutValue = 0; // 假设没有可领取的奖励
		return true;
	}
	
	// 如果没有找到对应的函数，返回false使用默认逻辑
	UE_LOG(LogTemp, Warning, TEXT("RedDotManager: 未找到红点条件函数 %s"), *FunctionName.ToString());
	return false;
}

FRedDotData URedDotManager::CalculateDefaultRedDot(const FActivityInfoRow* Config)
{
	if (!Config)
	{
		return FRedDotData();
	}

	// 默认逻辑：如果有静态数值且大于0，则显示红点
	bool bShouldShow = Config->StaticRedDotValue > 0;
	
	return FRedDotData(
		bShouldShow ? Config->RedDotType : ERedDotType::None,
		Config->StaticRedDotValue,
		bShouldShow,
		Config->RedDotPriority,
		FName(*FString::Printf(TEXT("%d"), Config->ActivityID))
	);
}