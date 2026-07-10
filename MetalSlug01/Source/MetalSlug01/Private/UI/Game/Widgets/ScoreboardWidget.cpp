#include "UI/Game/Widgets/ScoreboardWidget.h"
#include "UI/Game/Widgets/SubWidgets/ScoreboardEntryWidget.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Systems/RoomGameState.h"
#include "Systems/RoomPlayerController.h"
#include "Systems/Core/RoomPlayerState.h"
// 【2026.07.10 P0 重构】阵营集中定义 (FGameplayTag 替代 ERoomTeam)
#include "Data/Faction/FactionTags.h"

bool UScoreboardWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	return true;
}

void UScoreboardWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 初始化结算文本为隐藏状态（等待 ShowRoundSettlement / ShowFinalResult 时才显示）
	if (Text_Settlement_AttackerKills)
	{
		Text_Settlement_AttackerKills->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (Text_Settlement_DefenderKills)
	{
		Text_Settlement_DefenderKills->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (Text_AttackerWinResult)
	{
		Text_AttackerWinResult->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (Text_DefenderWinResult)
	{
		Text_DefenderWinResult->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 延迟刷新一次数据，等待服务器数据同步完成
	// 如果直接调用 RefreshScoreboard()，此时 RoomPlayerState->CurrentTeam 还未从服务器复制
	FTimerHandle RefreshTimer;
	GetWorld()->GetTimerManager().SetTimer(RefreshTimer, this, &UScoreboardWidget::RefreshScoreboard, 0.5f, false);
}

void UScoreboardWidget::RefreshScoreboard()
{
	// 清空现有数据
	ClearScoreboard();

	// 从GameState获取最新数据
	RefreshFromGameState();
}

void UScoreboardWidget::RefreshFromGameState()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>();
	if (!RoomGS)
	{
		return;
	}

	// 遍历所有玩家
	for (APlayerState* PS : RoomGS->PlayerArray)
	{
		ARoomPlayerState* RoomPS = Cast<ARoomPlayerState>(PS);
		if (RoomPS)
		{
			UpdateOrCreateEntry(RoomPS);
		}
	}

	// 排序并更新排名
	SortEntriesByScore(VB_AttackerTeam);
	SortEntriesByScore(VB_DefenderTeam);
	UpdateAllRanks(VB_AttackerTeam);
	UpdateAllRanks(VB_DefenderTeam);
}

void UScoreboardWidget::UpdateOrCreateEntry(ARoomPlayerState* PlayerState)
{
	if (!PlayerState)
	{
		return;
	}

	// 【2026.07.10 P0 重构】判断玩家阵营 (FGameplayTag 替代 ERoomTeam)
	bool bIsAttacker = (PlayerState->CurrentFactionTag == FFactionTags::Offense());

	// 获取对应的VerticalBox
	UVerticalBox* TargetBox = bIsAttacker ? VB_AttackerTeam : VB_DefenderTeam;
	UVerticalBox* WrongBox = bIsAttacker ? VB_DefenderTeam : VB_AttackerTeam;
	if (!TargetBox)
	{
		return;
	}

	FString PlayerName = PlayerState->GetPlayerName();

	// 先在错误容器中查找并移除（防止玩家被错误添加到对面容器）
	if (WrongBox)
	{
		for (int32 i = WrongBox->GetChildrenCount() - 1; i >= 0; i--)
		{
			UScoreboardEntryWidget* Entry = Cast<UScoreboardEntryWidget>(WrongBox->GetChildAt(i));
			if (Entry && Entry->GetPlayerName() == PlayerName)
			{
				Entry->RemoveFromParent();
				break;
			}
		}
	}

	// 在正确容器中查找是否已存在该玩家的条目
	UScoreboardEntryWidget* ExistingEntry = nullptr;
	for (int32 i = 0; i < TargetBox->GetChildrenCount(); i++)
	{
		UScoreboardEntryWidget* Entry = Cast<UScoreboardEntryWidget>(TargetBox->GetChildAt(i));
		if (Entry && Entry->GetPlayerName() == PlayerName)
		{
			ExistingEntry = Entry;
			break;
		}
	}

	if (ExistingEntry)
	{
		// 更新现有条目
		ExistingEntry->SetScore(PlayerState->GetScore());
		ExistingEntry->SetKDA(PlayerState->GetKills(), PlayerState->GetDeaths(), PlayerState->GetAssists());
		ExistingEntry->SetIsCurrentPlayer(PlayerName == GetCurrentPlayerName());
	}
	else
	{
		// 创建新条目
		CreateEntryWidget(PlayerState, bIsAttacker);
	}
}

UScoreboardEntryWidget* UScoreboardWidget::CreateEntryWidget(ARoomPlayerState* PlayerState, bool bIsAttacker)
{
	if (!PlayerState)
	{
		return nullptr;
	}

	// 实时校验阵营归属,防止因服务器数据未同步导致玩家被添加到错误容器
	// 【2026.07.10 P0 重构】FGameplayTag 比对
	bool bActualIsAttacker = (PlayerState->CurrentFactionTag == FFactionTags::Offense());
	if (bActualIsAttacker != bIsAttacker)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScoreboardWidget] CreateEntryWidget: 玩家 %s 队伍不一致，参数=%d，实际=%d，已修正"),
			*PlayerState->GetPlayerName(), bIsAttacker, bActualIsAttacker);
		bIsAttacker = bActualIsAttacker;
	}

	// 获取目标容器
	UVerticalBox* TargetBox = bIsAttacker ? VB_AttackerTeam : VB_DefenderTeam;
	if (!TargetBox)
	{
		return nullptr;
	}

	// 检查是否配置了条目Widget类
	if (!ScoreboardEntryWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScoreboardWidget] ScoreboardEntryWidgetClass 未配置！请在蓝图中设置。"));
		return nullptr;
	}

	// 创建条目Widget
	UScoreboardEntryWidget* EntryWidget = CreateWidget<UScoreboardEntryWidget>(this, ScoreboardEntryWidgetClass);
	if (!EntryWidget)
	{
		return nullptr;
	}

	// 设置玩家数据
	FString PlayerName = PlayerState->GetPlayerName();
	EntryWidget->SetPlayerName(PlayerName);
	EntryWidget->SetScore(PlayerState->GetScore());
	EntryWidget->SetKDA(PlayerState->GetKills(), PlayerState->GetDeaths(), PlayerState->GetAssists());
	EntryWidget->SetIsCurrentPlayer(PlayerName == GetCurrentPlayerName());

	// 添加到容器
	TargetBox->AddChild(EntryWidget);

	return EntryWidget;
}

void UScoreboardWidget::OnPlayerScoreChanged(ARoomPlayerState* ChangedPlayerState)
{
	if (!ChangedPlayerState)
	{
		return;
	}

	// 更新对应玩家的条目
	UpdateOrCreateEntry(ChangedPlayerState);

	// 重新排序和更新排名
	SortEntriesByScore(VB_AttackerTeam);
	SortEntriesByScore(VB_DefenderTeam);
	UpdateAllRanks(VB_AttackerTeam);
	UpdateAllRanks(VB_DefenderTeam);
}

void UScoreboardWidget::ClearScoreboard()
{
	if (VB_AttackerTeam)
	{
		VB_AttackerTeam->ClearChildren();
	}

	if (VB_DefenderTeam)
	{
		VB_DefenderTeam->ClearChildren();
	}
}

void UScoreboardWidget::SortEntriesByScore(UVerticalBox* VerticalBox)
{
	if (!VerticalBox)
	{
		return;
	}

	// 获取所有子控件
	TArray<UWidget*> Children = VerticalBox->GetAllChildren();
	if (Children.Num() == 0)
	{
		return;
	}

	// 收集所有条目数据和引用
	struct FPlayerScoreData
	{
		FString PlayerName;
		int32 Score;
		int32 Kills;
		int32 Deaths;
		int32 Assists;
		bool bBelongsToThisBox;
		bool bIsCurrentPlayer;
		UScoreboardEntryWidget* Widget;
	};

	TArray<FPlayerScoreData> PlayerDataList;

	// 遍历收集时，同步检查队伍归属（防止玩家被错误添加后乱跳）
	for (UWidget* Child : Children)
	{
		UScoreboardEntryWidget* EntryWidget = Cast<UScoreboardEntryWidget>(Child);
		if (!EntryWidget)
		{
			continue;
		}

		FPlayerScoreData Data;
		Data.PlayerName = EntryWidget->GetPlayerName();
		Data.Widget = EntryWidget;
		Data.bIsCurrentPlayer = (Data.PlayerName == GetCurrentPlayerName());

		// 从 GameState 查找该玩家的最新数据和队伍信息
		Data.bBelongsToThisBox = false; // 默认不属于
		UWorld* World = GetWorld();
		if (World)
		{
			ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>();
			if (RoomGS)
			{
				for (APlayerState* PS : RoomGS->PlayerArray)
				{
					ARoomPlayerState* RoomPS = Cast<ARoomPlayerState>(PS);
					if (RoomPS && RoomPS->GetPlayerName() == Data.PlayerName)
					{
						Data.Score = RoomPS->GetScore();
						Data.Kills = RoomPS->GetKills();
						Data.Deaths = RoomPS->GetDeaths();
						Data.Assists = RoomPS->GetAssists();
						// 【2026.07.10 P0 重构】校验该玩家是否真正属于当前容器对应的阵营
						bool bPlayerIsAttacker = (RoomPS->CurrentFactionTag == FFactionTags::Offense());
						Data.bBelongsToThisBox = (VerticalBox == VB_AttackerTeam) ? bPlayerIsAttacker : !bPlayerIsAttacker;
						break;
					}
				}
			}
		}

		PlayerDataList.Add(Data);
	}

	// 清空当前容器
	VerticalBox->ClearChildren();

	// 按得分降序排序（只排序属于当前容器的条目）
	TArray<FPlayerScoreData> BelongsList;
	TArray<FPlayerScoreData> NotBelongsList;
	for (const FPlayerScoreData& Data : PlayerDataList)
	{
		if (Data.bBelongsToThisBox)
		{
			BelongsList.Add(Data);
		}
		else
		{
			NotBelongsList.Add(Data);
		}
	}

	BelongsList.Sort([](const FPlayerScoreData& A, const FPlayerScoreData& B)
	{
		return A.Score > B.Score;
	});

	// 重新添加属于当前容器的条目
	for (const FPlayerScoreData& Data : BelongsList)
	{
		VerticalBox->AddChild(Data.Widget);
	}

	// 将不属于当前容器的条目移动到正确容器
	for (const FPlayerScoreData& Data : NotBelongsList)
	{
		UVerticalBox* CorrectBox = (VerticalBox == VB_AttackerTeam) ? VB_DefenderTeam : VB_AttackerTeam;
		if (CorrectBox && Data.Widget)
		{
			CorrectBox->AddChild(Data.Widget);
		}
	}
}

void UScoreboardWidget::UpdateAllRanks(UVerticalBox* VerticalBox)
{
	if (!VerticalBox)
	{
		return;
	}

	int32 CurrentRank = 1;
	TArray<UWidget*> Children = VerticalBox->GetAllChildren();

	for (UWidget* Child : Children)
	{
		UScoreboardEntryWidget* EntryWidget = Cast<UScoreboardEntryWidget>(Child);
		if (EntryWidget)
		{
			EntryWidget->SetRank(CurrentRank);
			CurrentRank++;
		}
	}
}

FString UScoreboardWidget::GetCurrentPlayerName() const
{
	// 获取当前玩家控制器
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		// 获取玩家状态
		if (APlayerState* PS = PC->GetPlayerState<APlayerState>())
		{
			return PS->GetPlayerName();
		}
	}

	return FString();
}

bool UScoreboardWidget::IsCurrentPlayerAttacker() const
{
	FString CurrentName = GetCurrentPlayerName();
	if (CurrentName.IsEmpty())
	{
		return true; // 默认返回攻方
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}

	ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>();
	if (!RoomGS)
	{
		return true;
	}

	for (APlayerState* PS : RoomGS->PlayerArray)
	{
		ARoomPlayerState* RoomPS = Cast<ARoomPlayerState>(PS);
		if (RoomPS && RoomPS->GetPlayerName() == CurrentName)
		{
			return RoomPS->CurrentFactionTag == FFactionTags::Offense();
		}
	}

	return true;
}

void UScoreboardWidget::ShowRoundSettlement(int32 AttackerKills, int32 DefenderKills)
{
	bIsInSettlementState = true;

	// 强制从 GameState 获取最新数据（避免参数传递链路中数据丢失或同步延迟）
	UWorld* World = GetWorld();
	if (World)
	{
		if (ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>())
		{
			// 直接使用 GameState 的复制属性，覆盖传入参数（确保显示的是服务器认可的数据）
			AttackerKills = RoomGS->AttackerTotalKills;
			DefenderKills = RoomGS->DefenderTotalKills;
			UE_LOG(LogTemp, Log, TEXT("[ScoreboardWidget] ShowRoundSettlement: 从 GameState 读取击杀数，攻方=%d, 守方=%d"),
				AttackerKills, DefenderKills);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[ScoreboardWidget] ShowRoundSettlement: GameState 未就绪，使用传入参数：攻方=%d, 守方=%d"),
				AttackerKills, DefenderKills);
		}
	}

	// 结算时强制刷新一遍玩家列表的队伍归属（防止 CurrentTeam 同步延迟导致玩家被错分到对面容器）
	RefreshScoreboard();

	// 刷新当局击杀数显示
	if (Text_Settlement_AttackerKills)
	{
		Text_Settlement_AttackerKills->SetText(FText::FromString(FString::Printf(TEXT("攻方击杀总数：%d"), AttackerKills)));
		Text_Settlement_AttackerKills->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] ShowRoundSettlement: Text_Settlement_AttackerKills 未绑定（BindWidget 为 nullptr）！请检查蓝图中是否正确绑定了该控件。"));
	}

	if (Text_Settlement_DefenderKills)
	{
		Text_Settlement_DefenderKills->SetText(FText::FromString(FString::Printf(TEXT("守方击杀总数：%d"), DefenderKills)));
		Text_Settlement_DefenderKills->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] ShowRoundSettlement: Text_Settlement_DefenderKills 未绑定（BindWidget 为 nullptr）！请检查蓝图中是否正确绑定了该控件。"));
	}

	// 胜负文字暂时隐藏，等最终结果广播后再显示
	if (Text_AttackerWinResult)
	{
		Text_AttackerWinResult->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] ShowRoundSettlement: Text_AttackerWinResult 未绑定！"));
	}

	if (Text_DefenderWinResult)
	{
		Text_DefenderWinResult->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] ShowRoundSettlement: Text_DefenderWinResult 未绑定！"));
	}

	// 强制显示计分板（结算状态下按 Tab 隐藏后，再按 Tab 应该能重新显示）
	SetVisibility(ESlateVisibility::Visible);

	UE_LOG(LogTemp, Log, TEXT("[ScoreboardWidget] ShowRoundSettlement: 攻方=%d, 守方=%d"), AttackerKills, DefenderKills);
}

void UScoreboardWidget::ShowFinalResult(int32 AttackerWins, int32 DefenderWins)
{
	// 从 GameState 获取最新胜局数（避免参数传递链路中的同步问题）
	UWorld* World = GetWorld();
	if (World)
	{
		if (ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>())
		{
			AttackerWins = RoomGS->AttackerWins;
			DefenderWins = RoomGS->DefenderWins;
			UE_LOG(LogTemp, Log, TEXT("[ScoreboardWidget] ShowFinalResult: 从 GameState 读取胜局数，攻方=%d, 守方=%d"),
				AttackerWins, DefenderWins);
		}
	}

	// 显示攻方胜利/平局文字
	if (Text_AttackerWinResult)
	{
		if (AttackerWins > DefenderWins)
		{
			Text_AttackerWinResult->SetText(FText::FromString(TEXT("攻方胜利")));
			Text_AttackerWinResult->SetColorAndOpacity(FSlateColor(FLinearColor::Red));
		}
		else if (AttackerWins == DefenderWins)
		{
			Text_AttackerWinResult->SetText(FText::FromString(TEXT("平局")));
			Text_AttackerWinResult->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		}
		Text_AttackerWinResult->SetVisibility(AttackerWins >= DefenderWins ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] ShowFinalResult: Text_AttackerWinResult 未绑定！"));
	}

	// 显示守方胜利/平局文字
	if (Text_DefenderWinResult)
	{
		if (DefenderWins > AttackerWins)
		{
			Text_DefenderWinResult->SetText(FText::FromString(TEXT("守方胜利")));
			Text_DefenderWinResult->SetColorAndOpacity(FSlateColor(FLinearColor::Blue));
		}
		else if (AttackerWins == DefenderWins)
		{
			Text_DefenderWinResult->SetText(FText::FromString(TEXT("平局")));
			Text_DefenderWinResult->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		}
		Text_DefenderWinResult->SetVisibility(DefenderWins >= AttackerWins ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] ShowFinalResult: Text_DefenderWinResult 未绑定！"));
	}

	UE_LOG(LogTemp, Log, TEXT("[ScoreboardWidget] ShowFinalResult: 攻方胜%d局, 守方胜%d局"), AttackerWins, DefenderWins);
}

void UScoreboardWidget::HideSettlementOverlay()
{
	bIsInSettlementState = false;

	// 隐藏所有结算控件
	if (Text_Settlement_AttackerKills)
	{
		Text_Settlement_AttackerKills->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (Text_Settlement_DefenderKills)
	{
		Text_Settlement_DefenderKills->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (Text_AttackerWinResult)
	{
		Text_AttackerWinResult->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (Text_DefenderWinResult)
	{
		Text_DefenderWinResult->SetVisibility(ESlateVisibility::Collapsed);
	}
}
