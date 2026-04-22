#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Login/Data/StaticTable.h"
#include "ScoreboardWidget.generated.h"

class UVerticalBox;
class UScoreboardEntryWidget;
class ARoomPlayerState;
class UTextBlock;

/**
 * 刀战模式计分板控件
 * 对战中按 Tab 显示计分排行（VB_AttackerTeam / VB_DefenderTeam）
 * 结算状态时保持显示，并在其基础上叠加显示：
 *   Text_Settlement_AttackerKills（当局攻方击杀数）
 *   Text_Settlement_DefenderKills（当局守方击杀数）
 *   Text_AttackerWinResult（攻方胜利/平局文字）
 *   Text_DefenderWinResult（守方胜利/平局文字）
 * 返回大厅按钮已迁移至 GameHUDWidget 统一管理
 */
UCLASS()
class METALSLUG01_API UScoreboardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 供外部调用的公共接口
	// ==========================================

	// 刷新所有队伍的玩家排行信息（从GameState获取最新数据）
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	void RefreshScoreboard();

	// 清空所有玩家数据
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	void ClearScoreboard();

	// 监听计分板数据变化事件（由RoomPlayerState广播）
	UFUNCTION()
	void OnPlayerScoreChanged(ARoomPlayerState* ChangedPlayerState);

	// ==========================================
	// 结算阶段接口
	// ==========================================

	// 进入结算状态：显示当局比分（由 GameHUD 在倒计时归零时调用）
	UFUNCTION(BlueprintCallable, Category = "Scoreboard|Settlement")
	void ShowRoundSettlement(int32 AttackerKills, int32 DefenderKills);

	// 显示最终胜负结果（由 GameHUD 延迟3秒后调用）
	UFUNCTION(BlueprintCallable, Category = "Scoreboard|Settlement")
	void ShowFinalResult(int32 AttackerWins, int32 DefenderWins);

	// 隐藏结算控件，返回纯计分板状态
	UFUNCTION(BlueprintCallable, Category = "Scoreboard|Settlement")
	void HideSettlementOverlay();

protected:
	// 初始化函数
	virtual bool Initialize() override;

	// Widget 构造完毕并加入视口后调用
	virtual void NativeConstruct() override;

	// ==========================================
	// UI 组件绑定区域
	// ==========================================

	// 攻方玩家列表容器
	UPROPERTY(meta = (BindWidget))
	UVerticalBox* VB_AttackerTeam;

	// 守方玩家列表容器
	UPROPERTY(meta = (BindWidget))
	UVerticalBox* VB_DefenderTeam;

	// 结算覆盖层 - 当局攻方击杀数
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_Settlement_AttackerKills;

	// 结算覆盖层 - 当局守方击杀数
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_Settlement_DefenderKills;

	// 结算覆盖层 - 攻方胜利/平局文字
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_AttackerWinResult;

	// 结算覆盖层 - 守方胜利/平局文字
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_DefenderWinResult;

protected:
	// 标记当前是否为结算状态
	bool bIsInSettlementState = false;

	// ==========================================
	// 蓝图可配置的子Widget类
	// ==========================================

	// 计分板条目Widget类（在蓝图中配置 WBP_ScoreboardEntryWidget）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoreboard|Config")
	TSubclassOf<UScoreboardEntryWidget> ScoreboardEntryWidgetClass;

private:
	// ==========================================
	// 私有辅助函数
	// ==========================================

	// 从GameState获取所有玩家数据并刷新UI
	void RefreshFromGameState();

	// 更新单个玩家的条目数据
	void UpdateOrCreateEntry(ARoomPlayerState* PlayerState);

	// 创建单个玩家条目Widget
	UScoreboardEntryWidget* CreateEntryWidget(ARoomPlayerState* PlayerState, bool bIsAttacker);

	// 对指定VerticalBox中的玩家按得分排序
	void SortEntriesByScore(UVerticalBox* VerticalBox);

	// 更新所有玩家的排名显示
	void UpdateAllRanks(UVerticalBox* VerticalBox);

	// 查找当前玩家
	FString GetCurrentPlayerName() const;

	// 检查当前玩家是否属于攻方
	bool IsCurrentPlayerAttacker() const;
};
