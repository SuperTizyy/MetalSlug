// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Game/Widgets/MatchInfoWidget.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Kismet/GameplayStatics.h"
#include "Systems/RoomGameState.h"


// ==========================================
// 1. 初始化
// ==========================================

/**
 * UMatchInfoWidget::Initialize
 *
 * 防御性检查: 防止 BindWidget 绑定失败时访问 nullptr
 * 1. 攻/守方数文本 -> 0
 * 2. 倒计时 -> "00:00" 白色
 * 3. 剩余局数 -> 0
 */
bool UMatchInfoWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// 初始化默认值: 防御性检查，防止 BindWidget 绑定失败时访问 nullptr
	if (Text_AttackerCount)
	{
		Text_AttackerCount->SetText(FText::AsNumber(0));
		UE_LOG(LogTemp, Log, TEXT("[MatchInfoWidget] Initialize: Text_AttackerCount 绑定成功，初始值=0"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MatchInfoWidget] Initialize: Text_AttackerCount 为空! 请检查 Blueprint 中是否有名为 Text_AttackerCount 的 TextBlock 控件，并确认其绑定了 meta=(BindWidget)"));
	}

	if (Text_DefenderCount)
	{
		Text_DefenderCount->SetText(FText::AsNumber(0));
		UE_LOG(LogTemp, Log, TEXT("[MatchInfoWidget] Initialize: Text_DefenderCount 绑定成功，初始值=0"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MatchInfoWidget] Initialize: Text_DefenderCount 为空! 请检查 Blueprint 中是否有名为 Text_DefenderCount 的 TextBlock 控件，并确认其绑定了 meta=(BindWidget)"));
	}

	if (Text_RoundCountdown)
	{
		Text_RoundCountdown->SetText(FText::FromString(TEXT("00:00")));
		Text_RoundCountdown->SetColorAndOpacity(NormalTimeColor);
	}

	if (Text_RemainingRounds)
	{
		Text_RemainingRounds->SetText(FText::AsNumber(0));
	}

	return true;
}


// ==========================================
// 2. Tick 循环
// ==========================================

/**
 * UMatchInfoWidget::NativeTick
 *
 * 1. 延迟绑定 GameState（首次 Tick 时）
 * 2. 立即从 GameState 读取最新值（防错过 OnRep）
 * 3. 每帧检查倒计时
 * 4. Dirty Flag: 仅在秒数变化时才刷新文本
 * 5. 根据阈值切换颜色
 */
void UMatchInfoWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 尝试绑定 GameState: 延迟到 Tick 中执行可以确保 GameState 已经生成
	// 仅当尚未绑定成功时才执行（bIsBoundToGameState 作为 Guard）
	if (!bIsBoundToGameState)
	{
		CachedGameState = Cast<ARoomGameState>(UGameplayStatics::GetGameState(this));
		if (CachedGameState)
		{
			CachedGameState->OnTeamKillCountUpdated.AddDynamic(this, &UMatchInfoWidget::OnTeamKillCountChanged);

			// 【修复】: 绑定后立即读取 GameState 上的最新值（而非使用本地的旧值）
			// 解决 OnRep_TeamKillCount 在绑定之前就触发、绑定时值为旧数据的问题
			UpdateAttackerCount(CachedGameState->AttackerTotalKills);
			UpdateDefenderCount(CachedGameState->DefenderTotalKills);

			bIsBoundToGameState = true;
			UE_LOG(LogTemp, Log, TEXT("[MatchInfoWidget] 已成功绑定 GameState，AttackerKills=%d, DefenderKills=%d"),
				CachedGameState->AttackerTotalKills, CachedGameState->DefenderTotalKills);
		}
	}

	// 倒计时更新: 安全拦截（不依赖上面的绑定逻辑）
	if (!CachedGameState || !Text_RoundCountdown)
	{
		return;
	}

	// 步骤 3: 获取由客户端运算预测的最新剩余时间
	int32 CurrentSeconds = CachedGameState->GetMatchRemainingSeconds();

	// 步骤 4: 性能优化防御编程（Dirty Flag 机制）
	// 避免每一帧 (如 60FPS 下每秒60次) 都去重设文本导致底层的 Slate 重绘 (InvalidateLayout)
	if (CurrentSeconds != LastRenderedSeconds)
	{
		LastRenderedSeconds = CurrentSeconds;

		// 步骤 5: 利用底层的 FTimespan 标准结构处理时间，避免手工 % 60 的隐式陷阱
		FTimespan TimeLeft = FTimespan::FromSeconds(CurrentSeconds);

		// 采用更安全规范的 FText::Format (如果未来需要做更复杂的本地化占位符替换，这是唯一规范)
		// 也可以保持格式为 mm:ss，但避免将可变数字暴露在硬编码宏中
		FString TimeString = FString::Printf(TEXT("%02d:%02d"), TimeLeft.GetMinutes(), TimeLeft.GetSeconds());
		Text_RoundCountdown->SetText(FText::FromString(TimeString));

		// 步骤 6: 使用暴露给设计人员的属性判断阈值，而不再使用魔法硬编码
		if (CurrentSeconds <= WarningTimeThreshold)
		{
			Text_RoundCountdown->SetColorAndOpacity(WarningTimeColor);
		}
		else
		{
			Text_RoundCountdown->SetColorAndOpacity(NormalTimeColor);
		}
	}
}


// ==========================================
// 3. 事件回调
// ==========================================

/**
 * UMatchInfoWidget::OnTeamKillCountChanged
 *
 * 双方击杀人数变化回调
 * 核心修复: 直接读取 CachedGameState 上的最新值
 * 原因: OnRep_TeamKillCount 回调触发时，属性值可能尚未同步完成
 * 解决: Widget 延迟绑定时，错过 OnRep 触发导致 UI 永远显示旧数据的问题
 */
void UMatchInfoWidget::OnTeamKillCountChanged(int32 AttackerKills, int32 DefenderKills)
{
	// 【核心修复】: 直接读取 CachedGameState 上的最新值
	// 不依赖传入参数，因为 OnRep_TeamKillCount 回调触发时，属性值可能尚未同步完成
	// 解决 Widget 延迟绑定时，错过 OnRep 触发导致 UI 永远显示旧数据的问题
	if (CachedGameState)
	{
		UpdateAttackerCount(CachedGameState->AttackerTotalKills);
		UpdateDefenderCount(CachedGameState->DefenderTotalKills);
		UE_LOG(LogTemp, Log, TEXT("[MatchInfoWidget] OnTeamKillCountChanged: 直接读取 GameState -> 攻方=%d, 守方=%d"),
			CachedGameState->AttackerTotalKills, CachedGameState->DefenderTotalKills);
	}
}


// ==========================================
// 4. 公共接口
// ==========================================

/**
 * UMatchInfoWidget::UpdateAttackerCount
 *
 * @param Count 攻方击杀数
 * 防御性检查: Text_AttackerCount 是否绑定
 */
void UMatchInfoWidget::UpdateAttackerCount(int32 Count)
{
	if (!Text_AttackerCount)
	{
		UE_LOG(LogTemp, Error, TEXT("[MatchInfoWidget] UpdateAttackerCount: Text_AttackerCount 为空! 请检查 Blueprint 中是否有名为 Text_AttackerCount 的 TextBlock 控件，并确认其绑定了 meta=(BindWidget)"));
		return;
	}

	Text_AttackerCount->SetText(FText::AsNumber(Count));
	UE_LOG(LogTemp, Log, TEXT("[MatchInfoWidget] 刷新攻方击杀数: %d"), Count);
}


/**
 * UMatchInfoWidget::UpdateDefenderCount
 *
 * @param Count 守方击杀数
 */
void UMatchInfoWidget::UpdateDefenderCount(int32 Count)
{
	if (!Text_DefenderCount)
	{
		UE_LOG(LogTemp, Error, TEXT("[MatchInfoWidget] UpdateDefenderCount: Text_DefenderCount 为空! 请检查 Blueprint 中是否有名为 Text_DefenderCount 的 TextBlock 控件，并确认其绑定了 meta=(BindWidget)"));
		return;
	}

	Text_DefenderCount->SetText(FText::AsNumber(Count));
	UE_LOG(LogTemp, Log, TEXT("[MatchInfoWidget] 刷新守方击杀数: %d"), Count);
}


/**
 * UMatchInfoWidget::UpdateRemainingRounds
 *
 * @param Rounds 剩余局数
 * 使用 FText::Format 进行本地化占位符替换
 */
void UMatchInfoWidget::UpdateRemainingRounds(int32 Rounds)
{
	if (Text_RemainingRounds)
	{
		Text_RemainingRounds->SetText(FText::Format(
			NSLOCTEXT("MatchInfo", "RemainingRoundsFormat", "Rounds: {0}"),
			FText::AsNumber(Rounds)
		));
	}
}


/**
 * UMatchInfoWidget::SetVisibilityByMode
 *
 * 刀战模式隐藏剩余局数，生化模式显示
 */
void UMatchInfoWidget::SetVisibilityByMode(ERoomMatchMode Mode)
{
	if (Text_RemainingRounds)
	{
		// 刀战模式隐藏剩余局数，生化模式显示
		Text_RemainingRounds->SetVisibility(Mode == ERoomMatchMode::Zombie
			? ESlateVisibility::Visible
			: ESlateVisibility::Collapsed);
	}
}


// ==========================================
// 5. 人数图标
// ==========================================

/**
 * UMatchInfoWidget::AddAttackerIcon
 *
 * 添加攻方人数图标
 * 1. 检查容器和图标有效性
 * 2. 检查是否超过 MaxIconDisplayCount
 * 3. 创建 UImage, 设置 20x20 大小
 * 4. AddChild 到 HB_AttackerIcons
 */
void UMatchInfoWidget::AddAttackerIcon(UTexture2D* Icon)
{
	if (!HB_AttackerIcons || !Icon)
	{
		return;
	}

	// 检查是否超过最大显示数量
	if (HB_AttackerIcons->GetChildrenCount() >= MaxIconDisplayCount)
	{
		return;
	}

	// 创建图标控件
	UImage* NewIcon = NewObject<UImage>(HB_AttackerIcons);
	if (NewIcon)
	{
		NewIcon->SetBrushFromTexture(Icon);
		NewIcon->SetDesiredSizeOverride(FVector2D(20.0f, 20.0f));
		// NewIcon->SetBrushSize(FVector2D(20.0f, 20.0f)); // Deprecated, already set with SetDesiredSizeOverride
		HB_AttackerIcons->AddChild(NewIcon);
	}
}


/**
 * UMatchInfoWidget::AddDefenderIcon
 *
 * 添加守方人数图标
 */
void UMatchInfoWidget::AddDefenderIcon(UTexture2D* Icon)
{
	if (!HB_DefenderIcons || !Icon)
	{
		return;
	}

	// 检查是否超过最大显示数量
	if (HB_DefenderIcons->GetChildrenCount() >= MaxIconDisplayCount)
	{
		return;
	}

	// 创建图标控件
	UImage* NewIcon = NewObject<UImage>(HB_DefenderIcons);
	if (NewIcon)
	{
		NewIcon->SetBrushFromTexture(Icon);
		NewIcon->SetDesiredSizeOverride(FVector2D(20.0f, 20.0f));
		// NewIcon->SetBrushSize(FVector2D(20.0f, 20.0f)); // Deprecated, already set with SetDesiredSizeOverride
		HB_DefenderIcons->AddChild(NewIcon);
	}
}
