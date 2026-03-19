#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RoomLabelWidget.generated.h"

// 前向声明需要的 UI 组件
class UButton;
class UTextBlock;

// 【新增】声明一个带有一个字符串参数的委托（大喇叭）
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoomSelectedSignature, FString, SelectedRoomName);

/**
 * 房间列表中的单个房间条目 UI
 */
UCLASS()
class METALSLUG01_API URoomLabelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 初始化函数，用于绑定按钮点击事件
	virtual bool Initialize() override;

	// 动态设置这个条目显示的房间名称
	UFUNCTION(BlueprintCallable, Category = "RoomLabel")
	void SetRoomName(const FString& InRoomName);

	// 获取当前条目的房间名（方便以后大厅获取你选了哪个房间）
	UFUNCTION(BlueprintCallable, Category = "RoomLabel")
	FString GetRoomName() const;
	
	// ==========================================
	// 【新增】供外层（大厅）绑定的事件分发器
	// ==========================================
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnRoomSelectedSignature OnRoomSelected;
	
	// 【新增】：供外部大厅调用的设置人数接口
	void SetPlayerCount(int32 CurrentPlayers, int32 MaxPlayers);
	
	// 【新增】：设置高亮状态
	void SetHighlight(bool bIsHighlight);

	// 【新增】：设置房间状态 (false=等待中, true=游戏中)
	void SetRoomState(bool bIsPlaying);

protected:
	// ==========================================
	// UI 组件绑定区域
	// ==========================================

	// 【修改】将原本的 Border 换成 Button
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_Background;

	// 房间名称文本
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_RoomName;
	
	// 【新增】：房间人数展示控件
	// 确保蓝图里这个 TextBlock 的名字严格叫 Text_PlayerCount
	UPROPERTY(meta = (BindWidget))
	class UTextBlock* Text_PlayerCount;
	
	// 【新增】：高亮框背景图
	UPROPERTY(meta = (BindWidget))
	class UImage* Image_Highlight;

	// 【新增】：房间状态文本
	UPROPERTY(meta = (BindWidget))
	class UTextBlock* Text_RoomStatus;

private:
	// ==========================================
	// 响应事件
	// ==========================================
	
	// 当玩家点击这个房间条目时触发
	UFUNCTION()
	void OnRoomItemClicked();
};
