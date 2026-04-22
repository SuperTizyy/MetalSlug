#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Login/Data/StaticTable.h"
#include "ScoreboardWidget.generated.h"

class UVerticalBox;
class UScoreboardEntryWidget;
class ARoomPlayerState;

/**
 * 计分板主控件
 * 用于在游戏中按Tab键显示对战排行信息
 * 分为攻方和守方两个队伍显示区域
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
	// 蓝图可配置的子Widget类
	// ==========================================

	// 计分板条目Widget类（在蓝图中配置 WBP_ScoreboardEntryWidget）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoreboard|Config")
	TSubclassOf<UScoreboardEntryWidget> ScoreboardEntryWidgetClass;

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
