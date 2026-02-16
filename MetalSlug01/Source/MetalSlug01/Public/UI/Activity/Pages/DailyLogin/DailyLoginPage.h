#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Activity/Data/DailyLoginConfig.h"
// 移除导航相关包含，保持每日登录功能纯净
#include "DailyLoginPage.generated.h"

UCLASS()
class METALSLUG01_API UDailyLoginPage : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void RefreshRewardList();

	UFUNCTION(BlueprintCallable)
	void Cheat_SetDayAndRefresh(int32 NewDay);
	
	// 新增：显示奖励选项弹窗（用于礼包/宝箱类型奖励）
	UFUNCTION(BlueprintCallable, Category = "DailyLogin")
	void ShowRewardOptionPopup(int32 DayIndex);
	
	// 新增：显示领取成功弹窗（用于普通奖励）
	UFUNCTION(BlueprintCallable, Category = "DailyLogin")
	void ShowClaimSuccessPopup();
	
	// 新增：处理奖励点击事件
	UFUNCTION()
	void HandleRewardClick(int32 DayIndex, ELoginRewardType RewardType);
	
protected:
	// --- 确保以下函数和变量都已声明 ---
	UFUNCTION()
	void OnBigRewardClicked();

	UFUNCTION()
	void HandleStoreLogic(int32 DayIndex);
	
	// 新增：用于处理奖励选项弹窗的存储逻辑
	UFUNCTION()
	void HandleRewardOptionStore(int32 DayIndex);

	UFUNCTION()
	void OpenTreasureBox(int32 DayIndex = -1);

	UFUNCTION()
	void OnFinalClaimComplete();

	UFUNCTION()
	void ScrollToCurrentDay();

	// 新增：全部领取按钮点击事件
	UFUNCTION()
	void OnClaimAllButtonClicked();

	// UI 组件绑定
	UPROPERTY(meta = (BindWidget))
	class UScrollBox* DayListScroll;

	// 新增：全部领取按钮
	UPROPERTY(meta = (BindWidget))
	class UButton* ClaimAllButton;

	UPROPERTY(EditAnywhere, Category = "DailyLogin|Classes")
	TSubclassOf<class UDailyLoginDayItemWidget> ItemClass;

	UPROPERTY(EditAnywhere, Category = "DailyLogin|Classes")
	TSubclassOf<class UUserWidget> RewardOptionClass; // 弹窗类

	// 新增：ActivityConfirmPopupWidget类
	UPROPERTY(EditAnywhere, Category = "DailyLogin|Classes")
	TSubclassOf<class UActivityConfirmPopupWidget> ActivityConfirmPopupClass;
	
	// 新增：领取成功弹窗类
	UPROPERTY(EditAnywhere, Category = "DailyLogin|Classes")
	TSubclassOf<class UUserWidget> ClaimSuccessPopClass;

	// meta = (BindWidget) 会自动将此变量与蓝图里同名的控件绑定
	// 假设你的蓝图根节点或主要容器叫 MainLayout
	UPROPERTY(meta = (BindWidget))
	class UWidget* MainLayout;
	
private:
	ERewardState CalculateState(int32 DayIndex);
	// 成员变量（ActivitySubsystem 指针）
	UPROPERTY()
	class UActivitySubsystem* ActivitySub;
	
	// 临时存储当前处理的天数
	UPROPERTY()
	int32 CurrentProcessingDay = -1;
};