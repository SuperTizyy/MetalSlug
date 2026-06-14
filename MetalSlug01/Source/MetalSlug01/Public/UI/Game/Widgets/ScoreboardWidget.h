// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Login/Data/StaticTable.h" // 引入 ERoomMatchMode
#include "ScoreboardWidget.generated.h"

// 前向声明
class UVerticalBox;
class UScoreboardEntryWidget;
class ARoomPlayerState;
class UTextBlock;


/**
 * @class UScoreboardWidget
 * @brief 刀战模式计分板控件
 *
 * 使用方式:
 * - 对战中按 Tab 显示计分排行（VB_AttackerTeam / VB_DefenderTeam）
 * - 结算状态时保持显示，并在其基础上叠加显示
 * - 监听 RoomPlayerState 的 OnScoreboardDataChanged 事件自动刷新
 *
 * 显示内容:
 * - Text_Settlement_AttackerKills: 当局攻方击杀数
 * - Text_Settlement_DefenderKills: 当局守方击杀数
 * - Text_AttackerWinResult: 攻方胜利/平局文字
 * - Text_DefenderWinResult: 守方胜利/平局文字
 *
 * 注意: 返回大厅按钮已迁移至 GameHUDWidget 统一管理
 *
 * 架构理念:
 * 1. 自动订阅: OnPlayerScoreChanged 监听所有 PlayerState
 * 2. 数据源单一: RefreshFromGameState 从 GameState->PlayerArray 获取
 * 3. 排名计算: SortEntriesByScore + UpdateAllRanks
 */
UCLASS()
class METALSLUG01_API UScoreboardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 公共接口（供外部调用）
	// ==========================================

	/**
	 * 刷新所有队伍的玩家排行信息
	 * 数据源: GameState->PlayerArray
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	void RefreshScoreboard();

	/**
	 * 清空所有玩家数据
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	void ClearScoreboard();

	/**
	 * 监听计分板数据变化事件（由 RoomPlayerState 广播）
	 * @param ChangedPlayerState 发生变化的玩家状态
	 */
	UFUNCTION()
	void OnPlayerScoreChanged(ARoomPlayerState* ChangedPlayerState);

	// ==========================================
	// 2. 结算阶段接口
	// ==========================================

	/**
	 * 进入结算状态
	 * 时机: 由 GameHUD 在倒计时归零时调用
	 * @param AttackerKills 当局攻方击杀数
	 * @param DefenderKills 当局守方击杀数
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard|Settlement")
	void ShowRoundSettlement(int32 AttackerKills, int32 DefenderKills);

	/**
	 * 显示最终胜负结果
	 * 时机: 由 GameHUD 延迟 3 秒后调用
	 * @param AttackerWins 攻方胜局数
	 * @param DefenderWins 守方胜局数
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard|Settlement")
	void ShowFinalResult(int32 AttackerWins, int32 DefenderWins);

	/**
	 * 隐藏结算控件，返回纯计分板状态
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard|Settlement")
	void HideSettlementOverlay();

protected:
	// ==========================================
	// 3. 生命周期
	// ==========================================

	/**
	 * 初始化
	 */
	virtual bool Initialize() override;

	/**
	 * Widget 构造完毕并加入视口后调用
	 */
	virtual void NativeConstruct() override;

	// ==========================================
	// 4. UI 组件绑定
	// ==========================================

	/**
	 * 攻方玩家列表容器
	 */
	UPROPERTY(meta = (BindWidget))
	UVerticalBox* VB_AttackerTeam;

	/**
	 * 守方玩家列表容器
	 */
	UPROPERTY(meta = (BindWidget))
	UVerticalBox* VB_DefenderTeam;

	/**
	 * 结算覆盖层 - 当局攻方击杀数
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_Settlement_AttackerKills;

	/**
	 * 结算覆盖层 - 当局守方击杀数
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_Settlement_DefenderKills;

	/**
	 * 结算覆盖层 - 攻方胜利/平局文字
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_AttackerWinResult;

	/**
	 * 结算覆盖层 - 守方胜利/平局文字
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_DefenderWinResult;

	/**
	 * 标记当前是否为结算状态
	 */
	bool bIsInSettlementState = false;

	// ==========================================
	// 5. 子 Widget 配置
	// ==========================================

	/**
	 * 计分板条目 Widget 类
	 * 用途: 在蓝图中配置 WBP_ScoreboardEntryWidget
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoreboard|Config")
	TSubclassOf<UScoreboardEntryWidget> ScoreboardEntryWidgetClass;

private:
	// ==========================================
	// 6. 私有辅助
	// ==========================================

	/**
	 * 从 GameState 获取所有玩家数据并刷新 UI
	 */
	void RefreshFromGameState();

	/**
	 * 更新单个玩家的条目数据
	 * @param PlayerState 玩家状态
	 */
	void UpdateOrCreateEntry(ARoomPlayerState* PlayerState);

	/**
	 * 创建单个玩家条目 Widget
	 * @param PlayerState 玩家状态
	 * @param bIsAttacker 是否攻方（决定添加到哪个容器）
	 * @return 创建的 UScoreboardEntryWidget
	 */
	UScoreboardEntryWidget* CreateEntryWidget(ARoomPlayerState* PlayerState, bool bIsAttacker);

	/**
	 * 对指定 VerticalBox 中的玩家按得分排序
	 */
	void SortEntriesByScore(UVerticalBox* VerticalBox);

	/**
	 * 更新所有玩家的排名显示
	 */
	void UpdateAllRanks(UVerticalBox* VerticalBox);

	/**
	 * 查找当前玩家
	 * @return 当前玩家的 PlayerName
	 */
	FString GetCurrentPlayerName() const;

	/**
	 * 检查当前玩家是否属于攻方
	 */
	bool IsCurrentPlayerAttacker() const;
};
