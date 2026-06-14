// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RoomLabelWidget.generated.h"

// 前向声明
class UButton;
class UTextBlock;


/**
 * @delegate FOnRoomSelectedSignature
 * @brief 房间被选中的事件（大喇叭）
 * @param SelectedRoomName 选中的房间名
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoomSelectedSignature, FString, SelectedRoomName);


/**
 * @class URoomLabelWidget
 * @brief 房间列表中的单个房间条目 UI
 *
 * 职责说明:
 * - 显示房间名 + 玩家数 + 状态
 * - 高亮显示（被选中时）
 * - 点击后通过 OnRoomSelected 通知外层大厅
 *
 * 架构理念:
 * 1. 委托反通知: 不依赖外部主动查询, 点击时主动广播
 * 2. 状态机: SetHighlight / SetRoomState 都是单一职责方法
 * 3. 复用: 每个房间一条 URoomLabelWidget
 * 4. 防御性: GetRoomName 时做空指针保护
 */
UCLASS()
class METALSLUG01_API URoomLabelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 生命周期
	// ==========================================

	/**
	 * 初始化: 绑定 Btn_Background 点击事件
	 */
	virtual bool Initialize() override;

	// ==========================================
	// 2. 公共接口
	// ==========================================

	/**
	 * 动态设置这个条目显示的房间名称
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomLabel")
	void SetRoomName(const FString& InRoomName);

	/**
	 * 获取当前条目的房间名
	 * 用途: 大厅获取你选了哪个房间
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomLabel")
	FString GetRoomName() const;

	/**
	 * 【新增】供外层（大厅）绑定的事件分发器
	 * 触发时机: 玩家点击该房间条目时
	 */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnRoomSelectedSignature OnRoomSelected;

	/**
	 * 【新增】设置人数显示
	 * @param CurrentPlayers 当前人数
	 * @param MaxPlayers 最大人数
	 * 显示格式: (Current/Max) e.g. (3/10)
	 */
	void SetPlayerCount(int32 CurrentPlayers, int32 MaxPlayers);

	/**
	 * 【新增】设置高亮状态
	 * @param bIsHighlight true=亮起, false=熄灭
	 */
	void SetHighlight(bool bIsHighlight);

	/**
	 * 【新增】设置房间状态
	 * @param bIsPlaying true=游戏中（暗红）, false=等待中（白）
	 */
	void SetRoomState(bool bIsPlaying);

protected:
	// ==========================================
	// 3. UI 组件绑定
	// ==========================================

	/**
	 * 【修改】将原本的 Border 换成 Button
	 * 用途: 让整条条目可点击
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_Background;

	/** 房间名称文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_RoomName;

	/**
	 * 【新增】房间人数展示控件
	 * 蓝图命名必须严格叫 Text_PlayerCount
	 */
	UPROPERTY(meta = (BindWidget))
	class UTextBlock* Text_PlayerCount;

	/**
	 * 【新增】高亮框背景图
	 * 选中时显示, 未选中隐藏
	 */
	UPROPERTY(meta = (BindWidget))
	class UImage* Image_Highlight;

	/**
	 * 【新增】房间状态文本
	 * 等待中 / 游戏中
	 */
	UPROPERTY(meta = (BindWidget))
	class UTextBlock* Text_RoomStatus;

private:
	// ==========================================
	// 4. 内部回调
	// ==========================================

	/**
	 * 玩家点击这个房间条目时触发
	 * 1. 广播 OnRoomSelected 事件
	 * 2. 屏幕调试信息
	 */
	UFUNCTION()
	void OnRoomItemClicked();
};
