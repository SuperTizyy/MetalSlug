/*#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "GameRewardMainWidget.generated.h"

/**
 * 游戏奖励主控件 - 管理奖励界面的整体布局和交互
 * 包含奖励列表和全部领取按钮的功能
 #1#
UCLASS()
class METALSLUG01_API UGameRewardMainWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	// 生命周期：当 Widget 构造时调用
	// 初始化控件绑定和注册事件回调
	virtual void NativeConstruct() override;

	// 绑定主容器和按钮
	UPROPERTY(meta = (BindWidget))
	UHorizontalBox* RewardListContainer; // 放置奖励卡片的水平容器，用于动态生成奖励项

	UPROPERTY(meta = (BindWidget))
	UButton* Btn_ClaimAll;               // "全部领取"按钮，用于批量领取所有可领取奖励

	// 引用子项的蓝图类，用于动态生成
	UPROPERTY(EditAnywhere, Category = "UI Config")
	TSubclassOf<class UGameRewardItemWidget> RewardItemClass; // 奖励项Widget类，用于创建多个奖励项

private:
	// 点击全部领取的回调
	// 处理用户点击"全部领取"按钮的事件
	UFUNCTION()
	void OnClaimAllClicked();

	// 初始化奖励列表的方法
	// 创建并填充奖励项列表
	void RefreshRewardList();
};*/