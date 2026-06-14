// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PlayerLabelWidget.generated.h"

// 前向声明
class UBorder;
class UTextBlock;
class UButton;


/**
 * @class UPlayerLabelWidget
 * @brief 房间内的单个玩家条目 UI
 *
 * 职责说明:
 * - 用于显示在攻守方队伍列表中
 * - 显示玩家名、准备状态、是否房主/AI
 * - 房主可看到"踢人"按钮，点击调用 PC->Server_KickPlayer
 *
 * 架构理念:
 * 1. 单一职责: 只是一个 UI 列表项，所有数据由 RoomInsidePage 注入
 * 2. 状态机通过 SetReadyState/SetAsHost/SetAsAI 切换
 * 3. 绝对防御: SetAsHost 时无论权限如何都隐藏踢人按钮（防自踢）
 */
UCLASS()
class METALSLUG01_API UPlayerLabelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 生命周期
	// ==========================================

	/**
	 * 初始化函数
	 * 用途: 绑定踢人按钮事件 + 默认"未准备"状态
	 */
	virtual bool Initialize() override;

	// ==========================================
	// 2. 公共接口（供外部调用）
	// ==========================================

	/**
	 * 设置显示的玩家名称
	 * @param InPlayerName 玩家名
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayerLabel")
	void SetPlayerName(const FString& InPlayerName);

	/**
	 * 获取当前条目的玩家名
	 * @return 玩家名
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayerLabel")
	FString GetPlayerName() const;

	/**
	 * 控制"踢人"按钮的显示与隐藏
	 * 用途: 只有房主能看到这个按钮
	 * @param bIsVisible 是否可见
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayerLabel")
	void SetRemoveButtonVisibility(bool bIsVisible);

protected:
	// ==========================================
	// 3. UI 组件绑定
	// ==========================================

	// // 标签底色 (你要求的 Border 控件)
	// UPROPERTY(meta = (BindWidget))
	// UBorder* Border_Background;

	/**
	 * 玩家名称文本
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_PlayerName;

	/**
	 * 移除玩家（踢人）按钮
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_RemovePlayer;

	/**
	 * 是否准备文本控件
	 * 注意: 蓝图里务必命名为 Text_IsReady
	 */
	UPROPERTY(meta = (BindWidget))
	class UTextBlock* Text_IsReady;

public:
	// ==========================================
	// 4. 状态机设置接口
	// ==========================================

	/**
	 * 设置准备状态
	 * @param bIsReady 是否已准备
	 * 用途: 控制文字"已准备/未准备"和颜色
	 */
	void SetReadyState(bool bIsReady);

	/**
	 * 设置该标签是否属于房主
	 * 副作用: 自动追加"（房主）"后缀，隐藏准备文本，强制隐藏踢人按钮
	 * @param bIsHost 是否为房主
	 */
	void SetAsHost(bool bIsHost);

	/**
	 * 设置该标签属于 AI 玩家
	 * 副作用: 隐藏准备文本
	 */
	void SetAsAI();

private:
	// ==========================================
	// 5. 按钮响应
	// ==========================================

	/**
	 * 点击移除玩家时触发
	 * 流程: 拿到本条目玩家名 -> 调用 PC->Server_KickPlayer
	 */
	UFUNCTION()
	void OnRemoveButtonClicked();
};
