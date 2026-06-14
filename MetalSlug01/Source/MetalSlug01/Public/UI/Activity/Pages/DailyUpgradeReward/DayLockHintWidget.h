// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Activity/Core/UpgradeActivitySubsystem.h"
#include "DayLockHintWidget.generated.h"


/**
 * @class UDayLockHintWidget
 * @brief 天数锁提示 Widget
 *
 * 职责说明:
 * - 展示当前天数锁定状态下的奖励预览
 * - 包含限时奖励图标 + 任务奖励图标
 * - 当天未解锁时, 显示"明天可领"等提示
 *
 * 架构理念:
 * 1. 数据驱动: 通过 DayIdentifier 从 UUpgradeActivitySubsystem 取数据
 * 2. 双容器: 限时奖励 + 任务奖励 分离显示
 * 3. 复用: 每个天一份, 或者每日一份
 * 4. 依赖: 强依赖 UUpgradeActivitySubsystem（必须存在）
 *
 * 关联:
 * - 上级: UDailyUpgradeRewardPage
 * - 数据: UUpgradeActivitySubsystem
 */
UCLASS()
class METALSLUG01_API UDayLockHintWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. UI 组件
	// ==========================================

	/** 限时奖励图标容器（HorizontalBox） */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UHorizontalBox* LimitedTimeRewardIconsContainer;

	/** 任务奖励图标容器（HorizontalBox） */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UHorizontalBox* TaskRewardIconsContainer;

	/** 文字提示（如"明天可领"） */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UTextBlock* HintText;

	// ==========================================
	// 2. 公共接口
	// ==========================================

	/**
	 * 初始化锁定提示 Widget
	 * @param DayIdentifier 天数标识符（如"day1", "day2"等）
	 * @details 根据天数标识符动态生成奖励图标
	 */
	UFUNCTION(BlueprintCallable, Category = "DayLockHint")
	void InitializeWidget(const FString& DayIdentifier);

	// ==========================================
	// 3. 私有辅助
	// ==========================================

	/**
	 * 设置任务奖励图标
	 * @param DayIdentifier 天数标识符
	 */
	void SetupTaskRewardIcons(const FString& DayIdentifier);

	/**
	 * 设置限时奖励图标
	 * @param DayIdentifier 天数标识符
	 */
	void SetupLimitedTimeRewardIcons(const FString& DayIdentifier);

	// ==========================================
	// 4. 配置
	// ==========================================

	/** WBP_RewardIcon 蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayLockHint|UI")
	TSubclassOf<class UUserWidget> RewardIconClass;
};
