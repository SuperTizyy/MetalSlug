//13

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TreasureBoxWidget.generated.h"

UCLASS()
class METALSLUG01_API UTreasureBoxWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 暴露给蓝图，用于刷新 UI 表现（比如勾选框的显隐）
	UPROPERTY(BlueprintReadOnly, Category = "Reward")
	int32 SelectedIndex = 1; // 默认选中中间 (0, 1, 2)
	
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnClaimFinished); 
	UPROPERTY(BlueprintAssignable) FOnClaimFinished OnClaimFinished;

protected:
	virtual void NativeConstruct() override;

	// 蓝图事件：当选择改变时调用，用于在蓝图中播放音效或动画
	UFUNCTION(BlueprintImplementableEvent, Category = "Reward")
	void OnSelectionChanged(int32 NewIndex);

private:
	// 三个奖励选项按钮
	UPROPERTY(meta = (BindWidget))
	class UButton* RewardOption_0;

	UPROPERTY(meta = (BindWidget))
	class UButton* RewardOption_1;

	UPROPERTY(meta = (BindWidget))
	class UButton* RewardOption_2;

	// 底部确认按钮
	UPROPERTY(meta = (BindWidget))
	class UButton* ConfirmButton;

	// 点击回调
	UFUNCTION() void OnOption0Clicked();
	UFUNCTION() void OnOption1Clicked();
	UFUNCTION() void OnOption2Clicked();
	UFUNCTION() void OnConfirmClicked();
};