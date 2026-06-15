// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "Systems/Activity/RedDotManager.h"
#include "Systems/Activity/ActivitySubsystem.h"
#include "Kismet/GameplayStatics.h"


// ==========================================
// 1. 初始化
// ==========================================

/**
 * URedDotManager::InitializeManager
 *
 * 1. 缓存 ActivitySubsystem 弱引用
 * 2. 记录 LastRefreshTime
 * 3. 立即 RefreshAllRedDots
 */
void URedDotManager::InitializeManager(UActivitySubsystem* InSubsystem)
{
	ActivitySubsystem = InSubsystem;
	LastRefreshTime = FPlatformTime::Seconds();
	RefreshAllRedDots();
}


// ==========================================
// 2. 公共接口
// ==========================================

/**
 * URedDotManager::RefreshAllRedDots
 *
 * 1. 防御: ActivitySubsystem 无效时 Log 警告并返回
 * 2. 清空 RedDotCache
 * 3. 从子系统获取所有 NavItems
 * 4. 逐个 CalculateRedDotForActivity -> 写入缓存
 * 5. 更新 LastRefreshTime + 日志
 */
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


/**
 * URedDotManager::GetRedDotData
 *
 * 1. 查 RedDotCache
 * 2. 找不到时返回默认 (ERedDotType::None, 0, false, 0)
 */
FRedDotData URedDotManager::GetRedDotData(int32 ActivityId) const
{
	if (const FRedDotData* Data = RedDotCache.Find(ActivityId))
	{
		return *Data;
	}

	// 返回默认的无红点状态
	return FRedDotData(ERedDotType::None, 0, false, 0, FName(*FString::Printf(TEXT("%d"), ActivityId)));
}


/**
 * URedDotManager::HasAnyRedDots
 *
 * 遍历 RedDotCache, 找到任一 bShouldShow = true 即返回 true
 */
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


/**
 * URedDotManager::GetTotalRedDotCount
 *
 * 汇总所有 NumberBadge/ProgressBadge 的数值
 * 用途: 顶部 TabBar 的汇总红点
 */
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


/**
 * URedDotManager::SetRedDotManually
 *
 * 1. 构造 FRedDotData(SimpleDot/None, Value, bShow, 100, FName)
 * 2. 写入 RedDotCache
 * 注意: 手动设置优先级 = 100, 覆盖条件函数
 */
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


// ==========================================
// 3. 内部计算
// ==========================================

/**
 * URedDotManager::CalculateRedDotForActivity
 *
 * 1. Config 空时返回默认 FRedDotData()
 * 2. 若配置了 RedDotConditionFunction, 优先调用
 *    - 成功: 根据 CalculatedValue > 0 决定 bShouldShow
 *    - 失败: fallback CalculateDefaultRedDot
 */
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


/**
 * URedDotManager::ExecuteRedDotCondition
 *
 * 通过 FName 反射调用指定函数
 * 已知:
 *   - CheckDailyLoginRedDot: 假设 OutValue=1
 *   - CheckFirstMatchRedDot: 假设 OutValue=0
 * 未来可扩展: 用反射 + TArray<UFunction*> 实现
 */
bool URedDotManager::ExecuteRedDotCondition(const FName& FunctionName, int32& OutValue)
{
	// 这里可以实现通过反射调用指定函数名的逻辑
	// 或者通过委托系统来执行自定义的红点计算函数

	// 示例: 简单的模拟实现
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

	// 如果没有找到对应的函数，返回 false 使用默认逻辑
	UE_LOG(LogTemp, Warning, TEXT("RedDotManager: 未找到红点条件函数 %s"), *FunctionName.ToString());
	return false;
}


/**
 * URedDotManager::CalculateDefaultRedDot
 *
 * 规则: Config->StaticRedDotValue > 0 时显示
 * 用途: 静态红点（不需要条件函数）
 */
FRedDotData URedDotManager::CalculateDefaultRedDot(const FActivityInfoRow* Config)
{
	if (!Config)
	{
		return FRedDotData();
	}

	// 默认逻辑: 如果有静态数值且大于 0，则显示红点
	bool bShouldShow = Config->StaticRedDotValue > 0;

	return FRedDotData(
		bShouldShow ? Config->RedDotType : ERedDotType::None,
		Config->StaticRedDotValue,
		bShouldShow,
		Config->RedDotPriority,
		FName(*FString::Printf(TEXT("%d"), Config->ActivityID))
	);
}
