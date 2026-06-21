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
 * 1. 单一职责: 只是一个 UI 列表项,所有数据由 RoomInsidePage 注入
 * 2. 状态机通过 SetReadyState/SetAsHost/SetAsAI/ApplyIdentity 切换
 * 3. 绝对防御: SetAsHost 时无论权限如何都隐藏踢人按钮 (防自踢)
 * 4. Model-View 分离: CachedPlayerName 是数据源, 控件只是 View 表现
 * 5. 房主/AI 身份永久化: 内部 bIsHostEntry/bIsAIEntry 状态位阻止 SetReadyState
 *    把 Text_IsReady 反向恢复出来 (Bug1 修复)
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
 * 【大厂模式】: 与 RoomLabelWidget 一致, 直接读数据缓存, 不反读 TextBlock
 *               即使 Text_PlayerName 未绑定也能正确返回
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

	/**
	 * 【大厂数据缓存】: 玩家名缓存, 与 RoomLabelWidget 的 CachedRoomName 同源设计
	 * Model-View 分离: SetPlayerName 同时写缓存 + 写 UI, GetPlayerName 直接读缓存
	 * 原因: 旧实现反读 Text_PlayerName, 一旦控件被 SetAsHost 改了后缀就拿不到原始名
	 *        现在: GetPlayerName 返回原始名, OnRemoveButtonClicked 也据此拿被踢者
	 */
	UPROPERTY()
	FString CachedPlayerName;

public:
	// ==========================================
	// 4. 状态机设置接口
	// ==========================================

	/**
	 * 设置准备状态
	 * @param bIsReady 是否已准备
	 * 用途: 控制文字"已准备/未准备"和颜色
	 * 【大厂防御】房主/AI 不应该有准备状态, 该接口在内部被 IsHostEntryOrAI 拦截
	 */
	void SetReadyState(bool bIsReady);

	/**
	 * 设置该标签是否属于房主
	 * 副作用:
	 *   1. 自动追加"（房主）"后缀
	 *   2. 永久隐藏 Text_IsReady (房主无准备概念)
	 *   3. 强制隐藏踢人按钮 (防自踢)
	 * @param bIsHost 是否为房主
	 */
	void SetAsHost(bool bIsHost);

	/**
	 * 设置该标签属于 AI 玩家
	 * 副作用: 永久隐藏 Text_IsReady (AI 无准备概念)
	 */
	void SetAsAI();

	/**
	 * 【大厂 P0】视图查询接口: 该条目当前是否被标记为房主
	 * 用途: URoomInsidePage 可据此决定是否需要重新应用隐藏逻辑
	 */
	UFUNCTION(BlueprintPure, Category = "PlayerLabel")
	bool IsHostEntry() const { return bIsHostEntry; }

	/**
	 * 【大厂 P0】视图查询接口: 该条目当前是否被标记为 AI
	 */
	UFUNCTION(BlueprintPure, Category = "PlayerLabel")
	bool IsAIEntry() const { return bIsAIEntry; }

	/**
	 * 【大厂 P0】权威身份覆写: 由 URoomInsidePage 在收到 OnHostChanged 事件后
	 * 调用, 用于"创建时未识别为房主 → 后期 GameState 同步下来后"再补一次刷新。
	 *
	 * 流程:
	 *   1. RefreshRoomUI 创建玩家条目时, 用 RoomStateService 快照判定房主
	 *      → 如果本地 HostPlayerName 还没下发, 会暂时漏判为普通玩家
	 *   2. 之后 RoomStateService 同步完成, URoomInsidePage 收到 OnRoomServiceHostChanged(true)
	 *   3. URoomInsidePage 调用本接口, 强制把"自己那条"标为房主
	 *
	 * @param bIsHostEntry  是否为房主条目
	 * @param bIsAIEntry   是否为 AI 条目
	 */
	void ApplyIdentity(bool bIsHostEntry, bool bIsAIEntry);

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

	/**
	 * 【大厂 P0 内部状态机】: 玩家身份标记
	 * 用途:
	 *   - 房主条目永远没有"准备"概念 → Text_IsReady 永久隐藏
	 *   - AI 条目永远没有"准备"概念   → Text_IsReady 永久隐藏
	 *   - 房主条目永远不能被自己踢   → Btn_RemovePlayer 永久隐藏
	 * 这样 SetReadyState 被调用时, 不会反向把这俩类型的 Text_IsReady 恢复出来
	 */
	bool bIsHostEntry = false;
	bool bIsAIEntry = false;
};
