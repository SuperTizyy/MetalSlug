// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Activity/Data/DailyLoginConfig.h"
// 移除导航相关包含，保持每日登录功能纯净
#include "DailyLoginPage.generated.h"


/**
 * @class UDailyLoginPage
 * @brief 每日登录活动页面
 *
 * 职责说明:
 * - 显示每日登录奖励（第 1-7 天普通 + 第 8 天大奖）
 * - 处理全部领取 / 单天领取
 * - 礼包/宝箱奖励弹出选项弹窗
 * - 普通奖励弹出成功提示
 * - 滚动到当前可领取天
 *
 * 架构理念:
 * 1. 数据驱动: UActivitySubsystem 提供 Record + Rewards
 * 2. 双入口: 普通点击 -> ShowClaimSuccessPopup / 宝箱点击 -> ShowRewardOptionPopup
 * 3. 防重入: static bool bAlreadyCalled 防止同一事件多次触发
 * 4. 解耦: 通过 OnActivityDataChanged.AddDynamic 监听 Subsystem 刷新
 * 5. 大项分离: 第 8 天大奖单独显示在 BigRewardItem 控件中（不在滚动列表）
 *
 * 关联:
 * - UActivitySubsystem（数据源）
 * - UDailyLoginDayItemWidget（单日条目）
 * - URewardOptionWidget（礼包选项弹窗）
 * - UActivityConfirmPopupWidget（宝箱选择弹窗）
 * - UClaimSuccessWidget（领取成功提示）
 */
UCLASS()
class METALSLUG01_API UDailyLoginPage : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 生命周期
	// ==========================================

	/**
	 * Native 构造
	 * 1. 缓存 ActivitySub 指针
	 * 2. 绑定 ActivitySub->OnActivityDataChanged -> RefreshRewardList
	 * 3. 绑定 ClaimAllButton
	 * 4. 检查并日志输出所有 TSubclassOf
	 * 5. 首次刷新 + 下一帧滚动到当前天
	 */
	virtual void NativeConstruct() override;

	/**
	 * Native 析构（清理）
	 */
	virtual void NativeDestruct() override;

	// ==========================================
	// 2. 公共接口
	// ==========================================

	/**
	 * 刷新奖励列表
	 * 1. 清空 DayListScroll + BigRewardItem
	 * 2. 获取 Record / Info
	 * 3. 遍历 TotalDays
	 *    - 计算 ERewardState（Claimed/Claimable/Incomplete）
	 *    - 创 UDailyLoginDayItemWidget
	 *    - 绑定 OnRewardClicked
	 *    - 特殊奖励 -> BigRewardItem / 普通 -> DayListScroll
	 * 4. 重新初始化 BigRewardItem（第 8 天）
	 */
	UFUNCTION()
	void RefreshRewardList();

	/**
	 * 调试: 设置登录天数并刷新
	 * 用途: 测试时手动跳到指定天数
	 */
	UFUNCTION(BlueprintCallable)
	void Cheat_SetDayAndRefresh(int32 NewDay);

	/**
	 * 显示奖励选项弹窗（用于礼包/宝箱类型奖励）
	 * 1. 校验领取资格
	 * 2. 转换指针数组 -> 值数组（UHT 限制）
	 * 3. CreateWidget<URewardOptionWidget>
	 * 4. 绑定 OnStoreToBag / OnOpenNow
	 */
	UFUNCTION(BlueprintCallable, Category = "DailyLogin")
	void ShowRewardOptionPopup(int32 DayIndex);

	/**
	 * 显示领取成功弹窗（用于普通奖励）
	 */
	UFUNCTION(BlueprintCallable, Category = "DailyLogin")
	void ShowClaimSuccessPopup();

	/**
	 * 处理奖励点击事件
	 * 根据 ELoginRewardType 路由:
	 * - Box -> ShowRewardOptionPopup
	 * - 默认 -> ShowClaimSuccessPopup
	 */
	UFUNCTION()
	void HandleRewardClick(int32 DayIndex, ELoginRewardType RewardType);

protected:
	// ==========================================
	// 3. 内部回调
	// ==========================================

	/**
	 * 大奖励点击事件（备用入口, 当前主要靠 OnRewardClicked）
	 */
	UFUNCTION()
	void OnBigRewardClicked();

	/**
	 * 处理"放入背包"逻辑
	 * 调用 ActivitySub->TryClaimReward(101, DayIndex) + RefreshRewardList
	 */
	UFUNCTION()
	void HandleStoreLogic(int32 DayIndex);

	/**
	 * 处理"放入背包"逻辑（来自 RewardOptionWidget）
	 * 与 HandleStoreLogic 区别: 用于特殊奖励的"先选后领"
	 */
	UFUNCTION()
	void HandleRewardOptionStore(int32 DayIndex);

	/**
	 * 打开宝箱
	 * 1. 优先用参数 DayIndex, 其次用 CurrentProcessingDay, 最后默认 3
	 * 2. 获取奖励配置 + 转值数组
	 * 3. CreateWidget<UActivityConfirmPopupWidget>
	 * 4. InitializePopup + AddToViewport
	 */
	UFUNCTION()
	void OpenTreasureBox(int32 DayIndex = -1);

	/**
	 * 最终领取完成回调
	 * 用途: TryClaimReward 用 CurrentClaimCount 作为天索引
	 */
	UFUNCTION()
	void OnFinalClaimComplete();

	/**
	 * 滚动到当前可领取天
	 * 1. Clamp Progress 到 1~8
	 * 2. 大于等于 8 时 TargetIndex = 6（第 7 天, 滚动列表最大索引）
	 * 3. ScrollWidgetIntoView + EDescendantScrollDestination::TopOrLeft
	 */
	UFUNCTION()
	void ScrollToCurrentDay();

	/**
	 * 全部领取按钮点击
	 * 1. 收集可领取天数列表
	 * 2. TryClaimMultipleRewards 批量领取
	 * 3. ShowClaimSuccessPopup
	 */
	UFUNCTION()
	void OnClaimAllButtonClicked();

	// ==========================================
	// 4. UI 组件
	// ==========================================

	/** 奖励滚动列表 */
	UPROPERTY(meta = (BindWidget))
	class UScrollBox* DayListScroll;

	/** 全部领取按钮 */
	UPROPERTY(meta = (BindWidget))
	class UButton* ClaimAllButton;

	/** 单天条目 Widget 类 */
	UPROPERTY(EditAnywhere, Category = "DailyLogin|Classes")
	TSubclassOf<class UDailyLoginDayItemWidget> ItemClass;

	/** 奖励选项弹窗类 */
	UPROPERTY(EditAnywhere, Category = "DailyLogin|Classes")
	TSubclassOf<class UUserWidget> RewardOptionClass;

	/** 活动确认弹窗类（宝箱选择） */
	UPROPERTY(EditAnywhere, Category = "DailyLogin|Classes")
	TSubclassOf<class UActivityConfirmPopupWidget> ActivityConfirmPopupClass;

	/** 奖励选项卡片类（多选卡片） */
	UPROPERTY(EditAnywhere, Category = "DailyLogin|Classes")
	TSubclassOf<class URewardOptionCardWidget> RewardOptionCardClass;

	/** 领取成功弹窗类 */
	UPROPERTY(EditAnywhere, Category = "DailyLogin|Classes")
	TSubclassOf<class UUserWidget> ClaimSuccessPopClass;

	/**
	 * 主布局控件（用于 GetWidgetFromName 查找 BigRewardItem）
	 * meta=(BindWidget) 自动绑定蓝图同名控件
	 */
	UPROPERTY(meta = (BindWidget))
	class UWidget* MainLayout;

private:
	// ==========================================
	// 5. 私有成员
	// ==========================================

	/** Subsystem 指针 */
	UPROPERTY()
	class UActivitySubsystem* ActivitySub;

	/** 临时存储当前处理的天数（用于 OpenTreasureBox fallback） */
	UPROPERTY()
	int32 CurrentProcessingDay = -1;
};
