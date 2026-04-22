#include "UI/Game/Widgets/ScoreboardWidget.h"
#include "UI/Game/Widgets/SubWidgets/ScoreboardEntryWidget.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Systems/RoomGameState.h"
#include "Systems/RoomPlayerController.h"
#include "UI/Login/Core/RoomPlayerState.h"

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

	// 初始刷新一次数据
	RefreshScoreboard();
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

	// 判断玩家队伍
	bool bIsAttacker = (PlayerState->CurrentTeam == ERoomTeam::Attack);

	// 获取对应的VerticalBox
	UVerticalBox* TargetBox = bIsAttacker ? VB_AttackerTeam : VB_DefenderTeam;
	if (!TargetBox)
	{
		return;
	}

	// 查找是否已存在该玩家的条目
	FString PlayerName = PlayerState->GetPlayerName();
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
	if (Children.Num() <= 1)
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
		bool bIsCurrentPlayer;
		UScoreboardEntryWidget* Widget;
	};

	TArray<FPlayerScoreData> PlayerDataList;

	for (UWidget* Child : Children)
	{
		UScoreboardEntryWidget* EntryWidget = Cast<UScoreboardEntryWidget>(Child);
		if (EntryWidget)
		{
			FPlayerScoreData Data;
			Data.PlayerName = EntryWidget->GetPlayerName();
			Data.Widget = EntryWidget;
			Data.bIsCurrentPlayer = (Data.PlayerName == GetCurrentPlayerName());

			// 从GameState获取最新得分
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
							break;
						}
					}
				}
			}

			PlayerDataList.Add(Data);
		}
	}

	// 按得分降序排序
	PlayerDataList.Sort([](const FPlayerScoreData& A, const FPlayerScoreData& B)
	{
		return A.Score > B.Score;
	});

	// 重新添加子控件（按排序顺序）
	VerticalBox->ClearChildren();

	for (const FPlayerScoreData& Data : PlayerDataList)
	{
		if (Data.Widget)
		{
			VerticalBox->AddChild(Data.Widget);
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
			return RoomPS->CurrentTeam == ERoomTeam::Attack;
		}
	}

	return true;
}

void UScoreboardWidget::ShowRoundSettlement(int32 AttackerKills, int32 DefenderKills)
{
	bIsInSettlementState = true;

	// 刷新当局击杀数显示
	if (Text_Settlement_AttackerKills)
	{
		Text_Settlement_AttackerKills->SetText(FText::FromString(FString::Printf(TEXT("攻方击杀总数：%d"), AttackerKills)));
		Text_Settlement_AttackerKills->SetVisibility(ESlateVisibility::Visible);
	}

	if (Text_Settlement_DefenderKills)
	{
		Text_Settlement_DefenderKills->SetText(FText::FromString(FString::Printf(TEXT("守方击杀总数：%d"), DefenderKills)));
		Text_Settlement_DefenderKills->SetVisibility(ESlateVisibility::Visible);
	}

	// 胜负文字暂时隐藏，等最终结果广播后再显示
	if (Text_AttackerWinResult)
	{
		Text_AttackerWinResult->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (Text_DefenderWinResult)
	{
		Text_DefenderWinResult->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 强制显示计分板（结算状态下按 Tab 隐藏后，再按 Tab 应该能重新显示）
	SetVisibility(ESlateVisibility::Visible);

	UE_LOG(LogTemp, Log, TEXT("[ScoreboardWidget] ShowRoundSettlement: 攻方=%d, 守方=%d"), AttackerKills, DefenderKills);
}

void UScoreboardWidget::ShowFinalResult(int32 AttackerWins, int32 DefenderWins)
{
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
		// 如果攻方输了则不显示文字（保持 Collapsed）
		Text_AttackerWinResult->SetVisibility(AttackerWins >= DefenderWins ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
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
		// 如果守方输了则不显示文字（保持 Collapsed）
		Text_DefenderWinResult->SetVisibility(DefenderWins >= AttackerWins ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
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
