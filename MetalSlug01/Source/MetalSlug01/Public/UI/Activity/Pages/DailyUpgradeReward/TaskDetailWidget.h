// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TaskDetailWidget.generated.h"


/**
 * @class UTaskDetailWidget
 * @brief 任务明细 Widget
 *
 * 职责说明:
 * - 显示单个任务的详细信息
 * - 包含: 任务需求说明 + 奖励展示 + 领取状态 + 领取按钮 + 提示
 *
 * 架构理念:
 * 1. 数据驱动: 通过 DayIdentifier + TaskIndex 路由
 * 2. 无参包装: HandleClaimButtonClickWrapper 用于委托绑定
 * 3. 双按钮: ClaimButton + ClaimHintText 状态联动
 * 4. 复用: 每个任务一个 UTaskDetailWidget 实例
 *
 * 关联:
 * - 上级: UDailyUpgradeRewardPage
 * - 数据: UUpgradeActivitySubsystem
 */
UCLASS()
class METALSLUG01_API UTaskDetailWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. UI 组件
	// ==========================================

	/** 任务需求说明文本 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UTextBlock* TaskRequirementText;

	/** 奖励展示容器（HorizontalBox） */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UHorizontalBox* RewardsContainer;

	/** 领取成功图标 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UImage* ClaimSuccessImage;

	/** 领取按钮 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UButton* ClaimButton;

	/** 领取提示文本 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UTextBlock* ClaimHintText;

	// ==========================================
	// 2. 公共接口
	// ==========================================

	/**
	 * 设置领取按钮状态和绑定点击事件
	 * @param DayIdentifier 天数标识（如"day1", "day2"）
	 * @param TaskIndex 任务索引（在 TaskDescriptions 数组中的索引）
	 * @param CompleteCount 当前完成次数（TaskCompleteCounts[i]）
	 * @param RequiredCount 需要完成的次数（TaskRelatedValues[i]）
	 */
	void SetupClaimButton(const FString& DayIdentifier, int32 TaskIndex, int32 CompleteCount, int32 RequiredCount);

	/**
	 * 设置奖励展示容器内容
	 * @param DayIdentifier 天数标识
	 * @param TaskIndex 任务索引
	 */
	void SetupRewardsContainer(const FString& DayIdentifier, int32 TaskIndex);

	// ==========================================
	// 3. 内部回调
	// ==========================================

	/**
	 * 处理领取按钮点击事件
	 * @param DayIdentifier 天数标识
	 * @param TaskIndex 任务索引
	 */
	UFUNCTION()
	void HandleClaimButtonClicked(const FString& DayIdentifier, int32 TaskIndex);

	/**
	 * 处理领取按钮点击事件的无参包装器
	 * 用途: 用于委托绑定 (因为按钮点击事件无参)
	 */
	UFUNCTION()
	void HandleClaimButtonClickWrapper();

	/**
	 * 处理奖励存储到背包事件
	 * @param TaskIndex 任务索引
	 */
	UFUNCTION()
	void HandleRewardStore(int32 TaskIndex);

	/**
	 * 【v216 新增】应用"已领取"UI 状态
	 * 职责: 单一职责函数 - 集中管理 ClaimButton/ClaimHintText/ClaimSuccessImage 的已领取视觉
	 * 大厂原则:
	 *   - 按钮: SetIsEnabled(false) + 灰色 + Visible (保留布局)
	 *   - 提示: "已领取" 文本 + Visible
	 *   - 成功图标: Visible
	 * 调用方:
	 *   - SetupClaimButton 在 TaskClaimStatus[i]==1 分支
	 *   - HandleRewardStore 领取成功后立即调用
	 */
	void ApplyClaimedUI();

	// ==========================================
	// 4. 配置
	// ==========================================

	/** WBP_RewardIcon 蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TaskDetail|UI")
	TSubclassOf<class UUserWidget> RewardIconClass;

	/** RewardOptionWidget 蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TaskDetail|UI")
	TSubclassOf<class URewardOptionWidget> RewardOptionWidgetClass;

private:
	// ==========================================
	// 5. 私有成员
	// ==========================================

	/** 当前绑定的天数标识（用于无参委托） */
	FString CurrentDayIdentifier;

	/** 当前绑定的任务索引（用于无参委托） */
	int32 CurrentTaskIndex;

	/** 当前任务索引（用于奖励事件处理） */
	int32 CurrentTaskIndexForReward;
};
