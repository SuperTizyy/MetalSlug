#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Activity/Data/DailyLoginConfig.h"
#include "DailyLoginDayItemWidget.generated.h"

// 声明事件委托
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRewardClicked, int32, DayIndex, ELoginRewardType, RewardType);

UCLASS()
class METALSLUG01_API UDailyLoginDayItemWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 新增：添加奖励图标的函数
	// 注意：这里的 RewardID 和 Count 必须和你表里的字段名对应
	UFUNCTION(BlueprintCallable, Category = "DailyLogin")
	void AddRewardIcon(int32 RewardID, int32 RewardCount);
	
	// 新增：直接通过配置行添加奖励图标
	UFUNCTION(BlueprintCallable, Category = "DailyLogin")
	void AddRewardIconFromConfig(const FDailyLoginConfigRow& ConfigRow);
	
	/**
	 * @brief 纹理异步加载完成回调
	 */
	UFUNCTION()
	void OnTextureLoaded(const FSoftObjectPath& Path, UObject* LoadedObject, UImage* RewardImage, int32 RewardID);
	
	// 新增：清空奖励容器的函数
	UFUNCTION(BlueprintCallable, Category = "DailyLogin")
	void ClearRewardIcons();
	
	// 修改 Init 声明：添加特殊奖励参数
	void Init(int32 InDayIndex, int32 InCurrentProgress, ERewardState RewardState = ERewardState::Incomplete, bool bInIsSpecialReward = false);
    
	// 暴露给 C++ 绑定的按钮（主页面需要通过它拦截第8天的点击）
	UPROPERTY(meta = (BindWidget), BlueprintReadWrite, Category = "DailyLogin")
	class UButton* ClaimButton; 

	// 蓝图事件：处理第 8 天特殊样式（如发光、立绘切换）
	UFUNCTION(BlueprintImplementableEvent, Category = "DailyLogin")
	void OnSetupBigRewardStyle();
	
	// 新增：奖励点击事件
	UPROPERTY(BlueprintAssignable, Category = "DailyLogin|Events")
	FOnRewardClicked OnRewardClicked;

protected:
	// 对应蓝图里的容器（HorizontalBox 或 WrapBox）
	UPROPERTY(meta = (BindWidget))
	class UPanelWidget* RewardContainer; 

	// 需要在编辑器里赋值的图标蓝图类
	UPROPERTY(EditAnywhere, Category = "DailyLogin")
	TSubclassOf<class UUserWidget> RewardIconClass;
	
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget))
	class UTextBlock* ButtonText;

	// 以下为第 8 天特有控件，设为 Optional 兼容 1-7 天
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadWrite, Category = "DailyLogin")
	class UImage* CharacterIcon;

	UPROPERTY(meta = (BindWidgetOptional))
	class UImage* BigRewardBackground;

	// 成功提示框类
	UPROPERTY(EditAnywhere, Category = "DailyLogin|UI")
	TSubclassOf<class UUserWidget> SuccessPopClass;

	UFUNCTION()
	void OnClaimButtonClicked();

	void UpdateVisualState();
	
	UPROPERTY()
	int32 CurrentProgress; // 新增成员变量来保存进度

private:
	int32 DayIndex;
	
	UPROPERTY()
	ERewardState CurrentState;
	
	UPROPERTY()
	bool bIsSpecialReward; // 新增：是否是特殊奖励
	
	// 按钮状态颜色配置
	UPROPERTY(EditAnywhere, Category = "Button Colors")
	FLinearColor ClaimedColor; // 领取状态 - 黄色
	
	UPROPERTY(EditAnywhere, Category = "Button Colors")
	FLinearColor TomorrowColor; // 明日可领状态 - 青色
	
	UPROPERTY(EditAnywhere, Category = "Button Colors")
	FLinearColor ClaimedDisabledColor; // 已领取状态 - 深灰色
	
	UPROPERTY(EditAnywhere, Category = "Button Colors")
	FLinearColor IncompleteDisabledColor; // 其他未达成状态 - 浅灰色
};