#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PlayerLabelWidget.generated.h"

class UBorder;
class UTextBlock;
class UButton;

/**
 * 房间内的单个玩家条目 UI（用于显示在红蓝队伍列表中）
 */
UCLASS()
class METALSLUG01_API UPlayerLabelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 初始化函数，用于绑定按钮事件
	virtual bool Initialize() override;

	// ==========================================
	// 供外部调用的公共接口
	// ==========================================
	
	// 设置显示的玩家名称
	UFUNCTION(BlueprintCallable, Category = "PlayerLabel")
	void SetPlayerName(const FString& InPlayerName);

	// 获取当前条目的玩家名
	UFUNCTION(BlueprintCallable, Category = "PlayerLabel")
	FString GetPlayerName() const;

	// 控制“踢人”按钮的显示与隐藏（比如：只有房主才能看到这个按钮）
	UFUNCTION(BlueprintCallable, Category = "PlayerLabel")
	void SetRemoveButtonVisibility(bool bIsVisible);

protected:
	// ==========================================
	// UI 组件绑定区域
	// ==========================================

	// 标签底色 (你要求的 Border 控件)
	UPROPERTY(meta = (BindWidget))
	UBorder* Border_Background;

	// 玩家名称文本
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_PlayerName;

	// 移除玩家（踢人）按钮
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_RemovePlayer;
	
	// 【新增】：是否准备文本控件 
	// (蓝图里务必命名为 Text_IsReady)
	UPROPERTY(meta = (BindWidget))
	class UTextBlock* Text_IsReady;
	
public:
	// 【新增】：对外开放的设置准备状态的接口
	void SetReadyState(bool bIsReady);
	
	// 【新增】：设置该标签是否属于房主（处理特殊样式）
	void SetAsHost(bool bIsHost);
	
	// 【新增】：设置该标签属于 AI 玩家
	void SetAsAI();

private:
	// ==========================================
	// 按钮点击响应函数
	// ==========================================
	
	// 点击移除玩家时触发
	UFUNCTION()
	void OnRemoveButtonClicked();
};