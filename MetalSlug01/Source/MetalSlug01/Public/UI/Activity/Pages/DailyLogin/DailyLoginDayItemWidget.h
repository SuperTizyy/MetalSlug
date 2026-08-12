// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/Tables/DailyLoginTableRow.h"
#include "DailyLoginDayItemWidget.generated.h"


/**
 * @delegate FOnRewardClicked
 * @brief 奖励点击事件
 * @param DayIndex 天数索引
 * @param RewardType 奖励类型
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRewardClicked, int32, DayIndex, ELoginRewardType, RewardType);


/**
 * @class UDailyLoginDayItemWidget
 * @brief 每日登录单天条目 Widget
 *
 * 职责说明:
 * - 每日登录列表的单一格子
 * - 显示天数 + 奖励图标 + 领取按钮 + 状态颜色
 * - 第 8 天特殊样式（可选, 角色立绘 + 背景）
 *
 * 架构理念:
 * 1. 单一职责: 一个条目管自己
 * 2. 数据驱动: Init 设置所有数据, 内部按状态显示
 * 3. 异步加载: 软引用 TSoftObjectPtr 异步加载贴图
 * 4. 防御性: 颜色配置可空（提供 EditAnywhere 默认）
 * 5. 可选控件: BindWidgetOptional 兼容 1-7 天和 8 天
 *
 * 关联:
 * - 上级: UDailyLoginPage（创建/管理）
 * - 数据: UActivitySubsystem
 */
UCLASS()
class METALSLUG01_API UDailyLoginDayItemWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 公共接口 - 奖励图标
	// ==========================================

	/**
	 * 添加奖励图标（通过 RewardItemID 查询 FItemDetailRow）
	 * @param RewardID 物品 ID
	 * @param RewardCount 物品数量
	 */
	UFUNCTION(BlueprintCallable, Category = "DailyLogin")
	void AddRewardIcon(int32 RewardID, int32 RewardCount);

	/**
	 * 直接通过配置行添加奖励图标
	 * @param ConfigRow FDailyLoginConfigRow 配置行
	 */
	UFUNCTION(BlueprintCallable, Category = "DailyLogin")
	void AddRewardIconFromConfig(const FDailyLoginConfigRow& ConfigRow);

	/**
	 * 专门处理礼包/宝箱类型的图标（使用 FDailyLoginConfigRow.BoxImage）
	 * @param BoxImage 宝箱软引用
	 * @param RewardCount 物品数量
	 */
	UFUNCTION(BlueprintCallable, Category = "DailyLogin")
	void AddRewardIconFromBoxImage(const TSoftObjectPtr<UTexture2D>& BoxImage, int32 RewardCount);

	/**
	 * 从 TreasureBoxItemRow 表查找宝箱图标
	 * @param BoxID 宝箱 ID
	 * @param RewardCount 物品数量
	 */
	UFUNCTION(BlueprintCallable, Category = "DailyLogin")
	void AddRewardIconFromTreasureBox(int32 BoxID, int32 RewardCount);

	/**
	 * 清空奖励图标
	 */
	UFUNCTION(BlueprintCallable, Category = "DailyLogin")
	void ClearRewardIcons();

	/**
	 * 初始化条目
	 * @param InDayIndex 天数
	 * @param InCurrentProgress 玩家当前进度
	 * @param RewardState 状态（Claimed/Claimable/Incomplete）
	 * @param bInIsSpecialReward 是否特殊奖励
	 * @param ActivitySubsystem 数据源（用于查表）
	 */
	void Init(int32 InDayIndex, int32 InCurrentProgress, ERewardState RewardState = ERewardState::Incomplete, bool bInIsSpecialReward = false, class UActivitySubsystem* ActivitySubsystem = nullptr);

	// ==========================================
	// 2. 公共接口 - 控件（暴露给 C++ 拦截）
	// ==========================================

	/**
	 * 暴露给 C++ 绑定的按钮
	 * 用途: 主页面需要通过它拦截第 8 天的点击
	 */
	UPROPERTY(meta = (BindWidget), BlueprintReadWrite, Category = "DailyLogin")
	class UButton* ClaimButton;

	/**
	 * 蓝图事件: 处理第 8 天特殊样式（如发光、立绘切换）
	 * 实现交给蓝图
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DailyLogin")
	void OnSetupBigRewardStyle();

	/**
	 * 奖励点击事件
	 * 上级订阅
	 */
	UPROPERTY(BlueprintAssignable, Category = "DailyLogin|Events")
	FOnRewardClicked OnRewardClicked;

protected:
	// ==========================================
	// 3. UI 组件
	// ==========================================

	/** 奖励图标 Image (直接引用蓝图中的控件) */
	UPROPERTY(meta = (BindWidget))
	class UImage* RewardIconImage;

	/** 奖励数量 Text (直接引用蓝图中的控件) */
	UPROPERTY(meta = (BindWidget))
	class UTextBlock* RewardCountText;

	/**
	 * 组件构造完成回调
	 */
	virtual void NativeConstruct() override;

	/** 按钮文本（如"领取"/"已领取"） */
	UPROPERTY(meta = (BindWidget))
	class UTextBlock* ButtonText;

	// ==========================================
	// 4. 第 8 天可选控件
	// ==========================================

	/**
	 * 第 8 天特有: 角色立绘
	 * 设为 Optional 兼容 1-7 天
	 */
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadWrite, Category = "DailyLogin")
	class UImage* CharacterIcon;

	/**
	 * 第 8 天特有: 大奖背景
	 * 设为 Optional 兼容 1-7 天
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	class UImage* BigRewardBackground;

	/** 成功提示框类 */
	UPROPERTY(EditAnywhere, Category = "DailyLogin|UI")
	TSubclassOf<class UUserWidget> SuccessPopClass;

	// ==========================================
	// 5. 内部回调
	// ==========================================

	/**
	 * 领取按钮点击事件
	 * 1. 校验状态 Claimable
	 * 2. 广播 OnRewardClicked(DayIndex, RewardType)
	 */
	UFUNCTION()
	void OnClaimButtonClicked();

	/**
	 * 根据 CurrentState / bIsSpecialReward 更新视觉
	 * 1. 按钮颜色
	 * 2. 文字
	 * 3. 锁定图标
	 */
	void UpdateVisualState();

	// ==========================================
	// 6. 私有成员
	// ==========================================

	/**
	 * 缓存当前进度
	 * 用途: UpdateVisualState 中判定明日可领
	 */
	UPROPERTY()
	int32 CurrentProgress;

private:
	/** 天数 */
	int32 DayIndex;

	/** 当前状态（Claimed/Claimable/Incomplete） */
	UPROPERTY()
	ERewardState CurrentState;

	/** 是否特殊奖励 */
	UPROPERTY()
	bool bIsSpecialReward;

	// ==========================================
	// 7. 颜色配置（蓝图可改）
	// ==========================================

	/** 领取状态 - 黄色 */
	UPROPERTY(EditAnywhere, Category = "Button Colors")
	FLinearColor ClaimedColor;

	/** 明日可领状态 - 青色 */
	UPROPERTY(EditAnywhere, Category = "Button Colors")
	FLinearColor TomorrowColor;

	/** 已领取状态 - 深灰色 */
	UPROPERTY(EditAnywhere, Category = "Button Colors")
	FLinearColor ClaimedDisabledColor;

	/** 其他未达成状态 - 浅灰色 */
	UPROPERTY(EditAnywhere, Category = "Button Colors")
	FLinearColor IncompleteDisabledColor;
};
