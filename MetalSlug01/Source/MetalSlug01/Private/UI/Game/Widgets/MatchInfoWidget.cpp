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
 * 3. 总局数 -> 0 (【v92】替换旧的"剩余局数")
 * 4. 母体变异倒计时 -> 默认隐藏 (Collapsed)
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

	// 【v92 大厂架构重构】总局数 (替换"剩余局数")
	if (Text_RemainingRounds)
	{
		Text_RemainingRounds->SetText(FText::FromString(TEXT("总局数：0")));
		Text_RemainingRounds->SetVisibility(ESlateVisibility::Collapsed); // 默认隐藏, 由 SetVisibilityByMode 控制
	}

	// 【v92 大厂架构新增】母体变异倒计时初始化 — 默认隐藏, 仅生化模式每局开局 8s 内显示
	if (Text_MotherMutationCountdown)
	{
		Text_MotherMutationCountdown->SetVisibility(ESlateVisibility::Collapsed);
		Text_MotherMutationCountdown->SetText(FText::FromString(TEXT("生化变异倒计时：0秒")));
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MatchInfoWidget] Initialize: Text_MotherMutationCountdown 为空! 请检查 Blueprint 中是否有名为 Text_MotherMutationCountdown 的 TextBlock 控件 (生化模式母体变异倒计时, v92 新增)."));
	}

	// 【生化模式】空投降临倒计时初始化 — 默认隐藏, 由服务器事件驱动
	//   - 业务规则: 必须等当前小局母体变异倒计时结束才能显示
	//   - 任何一次 Reset / 模式切换, 控件都要 Collapsed
	if (Text_AirdropCountdown)
	{
		Text_AirdropCountdown->SetVisibility(ESlateVisibility::Collapsed);
		Text_AirdropCountdown->SetText(FText::FromString(TEXT("空投降临：0秒")));
		bIsAirdropCountdownActive = false;
		LastRenderedAirdropSeconds = -1;
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MatchInfoWidget] Initialize: Text_AirdropCountdown 为空! 请检查 Blueprint 中是否有名为 Text_AirdropCountdown 的 TextBlock 控件 (生化模式空投降临倒计时)."));
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

	// ==========================================
	// 【v92 大厂架构新增】母体变异倒计时更新 (镜像 MatchEndTime 模式)
	// ==========================================
	//
	// 大厂原则:
	//   - 单一真理源: 数据在 GameState.MotherMutationStartTime/Duration
	//   - 客户端本地用 GetMotherMutationRemainingSeconds() 计算剩余秒数
	//   - DirtyFlag + NativeTick, 与比赛倒计时复用同一 Tick 钩子
	//
	// 【v92 大厂架构】0秒后自动隐藏 (修复 bug):
	//   - 用户反馈: "0秒后要隐藏显示"
	//   - 旧版只依赖服务器 Reset RPC 隐藏, 但服务器只在 HandleZombieRoundEnd 才 Reset
	//     → 倒计时到期 → 服务器不立即 Reset → UI 永远显示 "0秒"
	//   - 新版: Widget 检测到 RemainingSeconds == 0 时主动 Collapsed, 不依赖服务器
	if (Text_MotherMutationCountdown && bIsMotherMutationCountdownActive)
	{
		const int32 MotherMutationSeconds = CachedGameState->GetMotherMutationRemainingSeconds();

		// 大厂原则 — 0秒后自动隐藏 (不依赖服务器 Reset RPC):
		//   - 倒计时归零 → Widget 自己 Collapsed, 不等服务器 Reset
		//   - 下一局开始时服务器 Broadcast StartTime/Duration → UpdateMotherMutationCountdown 重新激活
		if (MotherMutationSeconds <= 0)
		{
			bIsMotherMutationCountdownActive = false;
			Text_MotherMutationCountdown->SetVisibility(ESlateVisibility::Collapsed);
			LastRenderedMotherMutationSeconds = 0;
			UE_LOG(LogTemp, Log,
				TEXT("[MatchInfoWidget] 母体变异倒计时归零, TextBlock 已自动隐藏 (等待服务器下一局重启)."));
		}
		else if (MotherMutationSeconds != LastRenderedMotherMutationSeconds)
		{
			LastRenderedMotherMutationSeconds = MotherMutationSeconds;

			// 显示格式: "生化变异倒计时：XX秒"
			// 使用 FText::Format 镜像 UpdateTotalRounds 的本地化友好模式
			const FString MotherMutationText = FString::Printf(TEXT("生化变异倒计时：%d秒"), MotherMutationSeconds);
			Text_MotherMutationCountdown->SetText(FText::FromString(MotherMutationText));

			UE_LOG(LogTemp, Verbose,
				TEXT("[MatchInfoWidget] 刷新母体变异倒计时: %d秒"),
				MotherMutationSeconds);
		}
	}

	// ==========================================
	// 【生化模式】空投降临倒计时更新
	// ==========================================
	//
	// 业务规则 (用户 2026.08.03 明确):
	//   - 控件在母体变异倒计时结束之前绝不能显示
	//   - 母体变异一结束, 服务器会写入 AirdropCountdownStartTime/Duration
	//   - 此时 NativeTick 每秒根据 GameState 计算剩余秒数并刷新
	//
	// 大厂原则:
	//   - 单一真理源: AirdropCountdownStartTime/Duration (GameState Replicated)
	//   - 客户端本地用 GetAirdropRemainingSeconds() 计算剩余秒数
	//   - DirtyFlag + NativeTick, 与母体变异倒计时复用同一 Tick 钩子
	//   - 0秒后自动隐藏 (不依赖服务器 Reset RPC, 镜像 v92 母体变异修复)
	if (Text_AirdropCountdown && bIsAirdropCountdownActive && CachedGameState)
	{
		// 第一道闸: 母体变异倒计时仍在进行, 一律强制隐藏空投倒计时
		// 这是用户明确要求的"业务规则", 不能让客户端在变母体前就显示空投数字
		const int32 MotherMutationRemaining = CachedGameState->GetMotherMutationRemainingSeconds();
		if (MotherMutationRemaining > 0)
		{
			if (Text_AirdropCountdown->GetVisibility() != ESlateVisibility::Collapsed)
			{
				Text_AirdropCountdown->SetVisibility(ESlateVisibility::Collapsed);
				UE_LOG(LogTemp, Verbose,
					TEXT("[MatchInfoWidget] 母体变异倒计时仍剩余 %ds, 空投倒计时继续隐藏."),
					MotherMutationRemaining);
			}
		}
		else
		{
			// 第二道闸: 倒计时归零后正常显示 + 刷新数字
			const int32 AirdropSeconds = CachedGameState->GetAirdropRemainingSeconds();

			if (AirdropSeconds <= 0)
			{
				// 镜像母体变异倒计时的"0秒自动隐藏"修复
				bIsAirdropCountdownActive = false;
				Text_AirdropCountdown->SetVisibility(ESlateVisibility::Collapsed);
				LastRenderedAirdropSeconds = 0;
				UE_LOG(LogTemp, Log,
					TEXT("[MatchInfoWidget] 空投倒计时归零, TextBlock 已自动隐藏 (等待服务器下一轮重启)."));
			}
			else if (AirdropSeconds != LastRenderedAirdropSeconds)
			{
				LastRenderedAirdropSeconds = AirdropSeconds;

				const FString AirdropText = FString::Printf(TEXT("空投降临：%d秒"), AirdropSeconds);
				Text_AirdropCountdown->SetText(FText::FromString(AirdropText));

				if (Text_AirdropCountdown->GetVisibility() != ESlateVisibility::Visible)
				{
					Text_AirdropCountdown->SetVisibility(ESlateVisibility::Visible);
				}

				UE_LOG(LogTemp, Verbose,
					TEXT("[MatchInfoWidget] 刷新空投倒计时: %d秒"),
					AirdropSeconds);
			}
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
 * UMatchInfoWidget::UpdateTotalRounds
 *
 * 【v92 大厂架构重构】替换 UpdateRemainingRounds
 *
 * @param TotalRounds 总回合数 (来自 GameState.TotalRounds, 由 GameHUDWidget 转发)
 * 显示格式: "总局数：xx"
 *
 * 大厂原则 — 单一真理源:
 *   - 数据源: GameState.TotalRounds (Replicated)
 *   - 不在 Widget 内部维护副本
 *   - 静态显示, 不依赖 NativeTick 倒数
 */
void UMatchInfoWidget::UpdateTotalRounds(int32 TotalRounds)
{
	if (!Text_RemainingRounds)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MatchInfoWidget] UpdateTotalRounds: Text_RemainingRounds 为空! "
			     "请检查 Blueprint 中是否有名为 Text_RemainingRounds 的 TextBlock 控件, "
			     "并确认其绑定了 meta=(BindWidget)."));
		return;
	}

	Text_RemainingRounds->SetText(FText::Format(
		NSLOCTEXT("MatchInfo", "TotalRoundsFormat", "总局数：{0}"),
		FText::AsNumber(TotalRounds)
	));

	UE_LOG(LogTemp, Log, TEXT("[MatchInfoWidget] 刷新总局数: %d"), TotalRounds);
}


/**
 * UMatchInfoWidget::SetVisibilityByMode
 *
 * 【v92 大厂架构重构】统一控制所有比赛相关控件的可见性
 *
 * 模式驱动显示规则 (大厂原则 — 单一入口):
 *   - Melee 模式:
 *     - Text_RoundCountdown: 显示 (倒计时走 MeleeMatchDurationSeconds)
 *     - Text_RemainingRounds: 隐藏 (刀战没有总局数概念)
 *     - Text_MotherMutationCountdown: 隐藏 (刀战没有母体变异)
 *   - Zombie 模式:
 *     - Text_RoundCountdown: 显示 (倒计时走 ZombieMatchDurationSeconds)
 *     - Text_RemainingRounds: 显示 (显示总局数)
 *     - Text_MotherMutationCountdown: 按事件动态显示 (UpdateMotherMutationCountdown 控制)
 *   - 其他模式: 全部隐藏 + Log Error (零兜底)
 */
void UMatchInfoWidget::SetVisibilityByMode(ERoomMatchMode Mode)
{
	if (Mode == ERoomMatchMode::Melee)
	{
		if (Text_RoundCountdown)
		{
			Text_RoundCountdown->SetVisibility(ESlateVisibility::Visible);
		}
		if (Text_RemainingRounds)
		{
			Text_RemainingRounds->SetVisibility(ESlateVisibility::Collapsed);
		}
		// 【生化模式】母体变异阶段严禁显示空投倒计时。
		if (Text_AirdropCountdown)
		{
			Text_AirdropCountdown->SetVisibility(ESlateVisibility::Collapsed);
		}

		// 切到非生化模式时同步清掉空投倒计时激活标志，避免下次回到生化模式时残留。
		bIsAirdropCountdownActive = false;
		LastRenderedAirdropSeconds = -1;
	}
	else if (Mode == ERoomMatchMode::Zombie)
	{
		if (Text_RoundCountdown)
		{
			Text_RoundCountdown->SetVisibility(ESlateVisibility::Visible);
		}
		if (Text_RemainingRounds)
		{
			Text_RemainingRounds->SetVisibility(ESlateVisibility::Visible);
		}
		// Text_MotherMutationCountdown 由 UpdateMotherMutationCountdown 事件动态控制, 这里不强制改
		// Text_AirdropCountdown 是否显示由 UpdateAirdropCountdown + NativeTick 决定：
		// 母体变异倒计时仍在进行时绝不能显示，业务条件由 bIsAirdropCountdownActive 把守。
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MatchInfoWidget] SetVisibilityByMode: 未识别模式=%d, 全部控件隐藏. "
			     "【修复】检查 GameMode CurrentMatchMode 是否被合法赋值."),
			static_cast<int32>(Mode));
		if (Text_RoundCountdown)
		{
			Text_RoundCountdown->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (Text_RemainingRounds)
		{
			Text_RemainingRounds->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}


// ==========================================
// 【v92 大厂架构新增】母体变异倒计时控制
// ==========================================

/**
 * UMatchInfoWidget::UpdateMotherMutationCountdown
 *
 * 由 GameHUDWidget 转发 GameState.OnMotherMutationChanged 委托触发
 * 负责:
 *   1. 根据 StartTime/Duration 决定是否显示/隐藏 TextBlock
 *   2. 标记 bIsMotherMutationCountdownActive, 让 NativeTick 开始刷新数字
 *   3. 重置 DirtyFlag, 强制立即刷新一次
 *
 * 大厂原则 — 镜像 UpdateRemainingRounds:
 *   - 不在事件回调里直接 SetText (由 NativeTick 统一刷新)
 *   - 只控制"是否显示"和"是否激活"两个布尔状态
 *
 * 大厂原则 — 零兜底:
 *   - Text_MotherMutationCountdown 绑定失败 → Log Error + return (强制修复 BP)
 *
 * @param StartTime 服务器写入的开始时间戳 (<= 0 表示未启动/已重置, 需隐藏)
 * @param Duration 倒计时总秒数 (<= 0 表示未启动/已重置, 需隐藏)
 */
void UMatchInfoWidget::UpdateMotherMutationCountdown(float StartTime, float Duration)
{
	if (!Text_MotherMutationCountdown)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MatchInfoWidget] UpdateMotherMutationCountdown: Text_MotherMutationCountdown 为空! 请检查 Blueprint 中是否有名为 Text_MotherMutationCountdown 的 TextBlock 控件 (v92 新增)."));
		return;
	}

	// 情况 1: 服务器未启动倒计时 (StartTime/Duration = 0) → 隐藏 TextBlock + 停 Tick
	if (StartTime <= 0.0f || Duration <= 0.0f)
	{
		bIsMotherMutationCountdownActive = false;
		Text_MotherMutationCountdown->SetVisibility(ESlateVisibility::Collapsed);
		LastRenderedMotherMutationSeconds = -1; // 重置 DirtyFlag, 下次激活时强制刷新
		UE_LOG(LogTemp, Log,
			TEXT("[MatchInfoWidget] UpdateMotherMutationCountdown: 倒计时已关闭 (StartTime=%.2f, Duration=%.2f), TextBlock 已隐藏."),
			StartTime, Duration);
		return;
	}

	// 情况 2: 服务器启动倒计时 → 显示 TextBlock + 激活 Tick
	bIsMotherMutationCountdownActive = true;
	Text_MotherMutationCountdown->SetVisibility(ESlateVisibility::Visible);
	LastRenderedMotherMutationSeconds = -1; // 重置 DirtyFlag, 强制 NativeTick 立即刷新

	UE_LOG(LogTemp, Log,
		TEXT("[MatchInfoWidget] UpdateMotherMutationCountdown: 倒计时已启动 (StartTime=%.2f, Duration=%.2f), TextBlock 已显示."),
		StartTime, Duration);
}


// ==========================================
// 【生化模式】空投降临倒计时控制
// ==========================================

/**
 * UMatchInfoWidget::UpdateAirdropCountdown
 *
 * 由 GameHUDWidget::OnAirdropCountdownChanged 转发 GameState.OnAirdropCountdownChanged 触发
 *
 * 大厂原则 — 与 UpdateMotherMutationCountdown 完全对称:
 *   - StartTime/Duration <= 0 → 隐藏 TextBlock + 停 Tick
 *   - StartTime/Duration > 0 → 激活 Tick (NativeTick 内还会做"母体变异未结束则继续隐藏"的业务闸)
 *
 * 业务约束 (用户 2026.08.03 明确):
 *   - 服务器只在母体变异倒计时结束之后才会写入 AirdropCountdownStartTime/Duration
 *   - 但客户端仍需自我防御: 若当前 GameState 仍有母体变异倒计时, 必须把空投控件继续隐藏
 *   - 镜像 v92 母体变异"0秒自动隐藏"修复: 倒计时归零时 Widget 主动 Collapsed, 不依赖服务器 Reset RPC
 */
void UMatchInfoWidget::UpdateAirdropCountdown(float StartTime, float Duration)
{
	if (!Text_AirdropCountdown)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MatchInfoWidget] UpdateAirdropCountdown: Text_AirdropCountdown 为空! 请检查 Blueprint 中是否有名为 Text_AirdropCountdown 的 TextBlock 控件."));
		return;
	}

	// 情况 1: 服务器未启动空投倒计时 (StartTime/Duration = 0) → 隐藏 + 停 Tick
	if (StartTime <= 0.0f || Duration <= 0.0f)
	{
		bIsAirdropCountdownActive = false;
		Text_AirdropCountdown->SetVisibility(ESlateVisibility::Collapsed);
		LastRenderedAirdropSeconds = -1;
		UE_LOG(LogTemp, Log,
			TEXT("[MatchInfoWidget] UpdateAirdropCountdown: 空投倒计时已关闭 (StartTime=%.2f, Duration=%.2f), TextBlock 已隐藏."),
			StartTime, Duration);
		return;
	}

	// 情况 2: 服务器启动空投倒计时
	//   - 仅记录"激活"标志, 真正显示交给 NativeTick
	//   - NativeTick 内会再次校验母体变异是否结束, 避免业务违规
	bIsAirdropCountdownActive = true;
	LastRenderedAirdropSeconds = -1; // 重置 DirtyFlag, 强制 NativeTick 立即刷新一次

	UE_LOG(LogTemp, Log,
		TEXT("[MatchInfoWidget] UpdateAirdropCountdown: 空投倒计时已启动 (StartTime=%.2f, Duration=%.2f), 等待 NativeTick 解锁显示."),
		StartTime, Duration);
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
