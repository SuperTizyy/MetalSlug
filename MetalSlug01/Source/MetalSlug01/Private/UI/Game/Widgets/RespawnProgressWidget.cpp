// ==========================================
// 复活进度条 Widget 实现
// 关联头文件: RespawnProgressWidget.h
//
// 【2026.07.14 重构说明】
// 移除了 CharacterEvents 订阅逻辑（由 UGameHUDWidget 统一订阅）
// 本 Widget 只负责内容更新，不负责事件订阅
//
// 【v201.6 大厂架构新增】
// 支持移动锁定倒计时显示（与无敌期倒计时并列）
// ==========================================
#include "UI/Game/Widgets/RespawnProgressWidget.h"
#include "Components/HealthComponent.h"
#include "Characters/BaseCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"


// ==========================================
// 1. 生命周期
// ==========================================

void URespawnProgressWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UE_LOG(LogTemp, Display,
		TEXT("[RespawnProgressWidget] NativeConstruct. Widget=%s, PB=%s, Text=%s"),
		*GetName(),
		PB_InvincibilityProgress ? *PB_InvincibilityProgress->GetName() : TEXT("nullptr"),
		Text_Countdown ? *Text_Countdown->GetName() : TEXT("nullptr"));

	// 初始状态隐藏（Visibility 由 UGameHUDWidget 控制）
	Hide();
}

void URespawnProgressWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 【v201.6】优先处理移动锁定倒计时（如果正在监听）
	if (bListeningToMovementLock)
	{
		// 减少剩余时间
		MovementLockRemainingSeconds -= InDeltaTime;

		// 检查是否已到期
		if (MovementLockRemainingSeconds <= 0.0f)
		{
			// 倒计时结束，隐藏
			MovementLockRemainingSeconds = 0.0f;
			if (PB_InvincibilityProgress)
			{
				PB_InvincibilityProgress->SetPercent(1.0f);
			}
			if (Text_Countdown)
			{
				Text_Countdown->SetText(FText::FromString(TEXT("100.00%")));
			}
			HideMovementLock();
			return;
		}

		// 更新移动锁定倒计时
		UpdateMovementLockProgress();
		return;
	}

	// 只有在显示状态才更新（无敌期路径）
	if (!bIsShowing)
	{
		return;
	}

	// 获取剩余秒数
	const float Remaining = GetRemainingSeconds();

	// 检查是否已到期
	if (Remaining <= 0.0f)
	{
		// 倒计时结束，显示100%后隐藏
		if (PB_InvincibilityProgress)
		{
			PB_InvincibilityProgress->SetPercent(1.0f);
		}
		// 【v40.9 修复】同 UpdateProgress 改用 FString::Printf 替代 FText::Format (ICU 不支持 :.2f)
		if (Text_Countdown)
		{
			Text_Countdown->SetText(FText::FromString(TEXT("100.00%")));
		}
		Hide();
		return;
	}

	// 获取总时长
	const float Total = GetTotalDuration();

	// 更新进度条和倒计时
	UpdateProgress(Remaining, Total);
}


// ==========================================
// 2. 公开接口
// ==========================================

void URespawnProgressWidget::Show()
{
	UE_LOG(LogTemp, Display,
		TEXT("[RespawnProgressWidget] Show() 被调用. Widget=%s"),
		*GetName());

	// 验证必要组件（先验证，防止组件未绑定就设置 Visibility）
	if (!PB_InvincibilityProgress)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RespawnProgressWidget] Show: PB_InvincibilityProgress 未绑定! "
				"请在 Blueprint 中绑定 ProgressBar 控件。"));
		return;
	}

	if (!Text_Countdown)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RespawnProgressWidget] Show: Text_Countdown 未绑定! "
				"请在 Blueprint 中绑定 TextBlock 控件。"));
		return;
	}

	// 强制设置 Widget 可见（不受 bIsShowing 状态影响，支持重复调用刷新）
	SetVisibility(ESlateVisibility::Visible);

	// 记录总时长
	RecordedTotalDuration = GetTotalDuration();

	UE_LOG(LogTemp, Display,
		TEXT("[RespawnProgressWidget] Show: 设置Visible, 总时长=%.2fs, PB=%s, Text=%s"),
		RecordedTotalDuration,
		PB_InvincibilityProgress ? *PB_InvincibilityProgress->GetName() : TEXT("nullptr"),
		Text_Countdown ? *Text_Countdown->GetName() : TEXT("nullptr"));

	// 设置为正在显示
	bIsShowing = true;

	// 立即更新一次（避免第一帧延迟）
	const float Remaining = GetRemainingSeconds();
	UpdateProgress(Remaining, RecordedTotalDuration);
}

void URespawnProgressWidget::Hide()
{
	// 强制设置 Widget 隐藏（不受 bIsShowing 状态影响，支持重复调用）
	SetVisibility(ESlateVisibility::Collapsed);

	// 设置为隐藏
	bIsShowing = false;

	// 【v201.6】同时取消移动锁定监听
	bListeningToMovementLock = false;

	// 重置进度
	if (PB_InvincibilityProgress)
	{
		PB_InvincibilityProgress->SetPercent(0.0f);
	}

	// 清除倒计时文字
	if (Text_Countdown)
	{
		Text_Countdown->SetText(FText::GetEmpty());
	}

	UE_LOG(LogTemp, Display,
		TEXT("[RespawnProgressWidget] Hide: 设置Collapsed, 隐藏复活进度条。"));
}


// ==========================================
// 【v201.6 大厂架构新增】移动锁定倒计时
// ==========================================

void URespawnProgressWidget::ShowMovementLock(float DurationSeconds)
{
	UE_LOG(LogTemp, Display,
		TEXT("[RespawnProgressWidget] ShowMovementLock: Duration=%.2fs, Widget=%s"),
		DurationSeconds, *GetName());

	// 验证必要组件
	if (!PB_InvincibilityProgress || !Text_Countdown)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RespawnProgressWidget] ShowMovementLock: 组件未绑定! PB=%s, Text=%s"),
			PB_InvincibilityProgress ? *PB_InvincibilityProgress->GetName() : TEXT("nullptr"),
			Text_Countdown ? *Text_Countdown->GetName() : TEXT("nullptr"));
		return;
	}

	// 显示 Widget
	SetVisibility(ESlateVisibility::Visible);

	// 记录移动锁定的时长
	RecordedTotalDuration = DurationSeconds;
	MovementLockRemainingSeconds = DurationSeconds;
	bListeningToMovementLock = true;
	bIsShowing = true;

	// 立即更新一次（避免第一帧延迟）
	UpdateMovementLockProgress();
}

void URespawnProgressWidget::HideMovementLock()
{
	bListeningToMovementLock = false;
	MovementLockRemainingSeconds = 0.0f;

	// 如果当前不是监听无敌期，则完全隐藏
	if (!bIsShowing)
	{
		SetVisibility(ESlateVisibility::Collapsed);
		if (PB_InvincibilityProgress)
		{
			PB_InvincibilityProgress->SetPercent(0.0f);
		}
		if (Text_Countdown)
		{
			Text_Countdown->SetText(FText::GetEmpty());
		}
	}

	UE_LOG(LogTemp, Display,
		TEXT("[RespawnProgressWidget] HideMovementLock: 隐藏移动锁定倒计时。 bIsShowing=%d"),
		bIsShowing ? 1 : 0);
}

void URespawnProgressWidget::UpdateMovementLockProgress()
{
	if (!bListeningToMovementLock)
	{
		return;
	}

	// 计算进度百分比
	float Progress = 0.0f;
	float Percentage = 0.0f;
	if (RecordedTotalDuration > 0.0f)
	{
		Progress = FMath::Clamp((RecordedTotalDuration - MovementLockRemainingSeconds) / RecordedTotalDuration, 0.0f, 1.0f);
		Percentage = Progress * 100.0f;
	}

	// 更新进度条
	if (PB_InvincibilityProgress)
	{
		PB_InvincibilityProgress->SetPercent(Progress);
	}

	// 更新倒计时文字 - 显示百分比格式 "X.XX%"
	if (Text_Countdown)
	{
		const FString PercentText = FString::Printf(TEXT("%.2f%%"), Percentage);
		Text_Countdown->SetText(FText::FromString(PercentText));
	}

	UE_LOG(LogTemp, Verbose,
		TEXT("[RespawnProgressWidget] UpdateMovementLockProgress: Progress=%.4f (%.2f%%), Remaining=%.2fs, Total=%.2fs"),
		Progress, Percentage, MovementLockRemainingSeconds, RecordedTotalDuration);
}


// ==========================================
// 3. 内部辅助
// ==========================================

float URespawnProgressWidget::GetRemainingSeconds() const
{
	// 获取 Owning Pawn
	APawn* Pawn = GetOwningPlayerPawn();
	if (!Pawn)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RespawnProgressWidget] GetRemainingSeconds: GetOwningPlayerPawn 返回 nullptr! Widget=%s"),
			*GetName());
		return 0.0f;
	}

	// 获取 HealthComponent
	UHealthComponent* HC = Pawn->FindComponentByClass<UHealthComponent>();
	if (!HC)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RespawnProgressWidget] GetRemainingSeconds: Pawn 上没有 HealthComponent! Pawn=%s"),
			*Pawn->GetName());
		return 0.0f;
	}

	const float Remaining = HC->GetInvincibilityRemainingSeconds();

	UE_LOG(LogTemp, Verbose,
		TEXT("[RespawnProgressWidget] GetRemainingSeconds: Pawn=%s, Remaining=%.2f, HC->IsInvincible=%d"),
		*Pawn->GetName(), Remaining, HC->IsInvincible() ? 1 : 0);

	return Remaining;
}

float URespawnProgressWidget::GetTotalDuration() const
{
	// 获取 Owning Pawn
	APawn* Pawn = GetOwningPlayerPawn();
	if (!Pawn)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RespawnProgressWidget] GetTotalDuration: GetOwningPlayerPawn 返回 nullptr! Widget=%s"),
			*GetName());
		return 0.0f;
	}

	// 获取 HealthComponent
	UHealthComponent* HC = Pawn->FindComponentByClass<UHealthComponent>();
	if (!HC)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RespawnProgressWidget] GetTotalDuration: Pawn 上没有 HealthComponent! Pawn=%s"),
			*Pawn->GetName());
		return 0.0f;
	}

	const float Duration = HC->GetInvincibilityDuration();

	UE_LOG(LogTemp, Verbose,
		TEXT("[RespawnProgressWidget] GetTotalDuration: Pawn=%s, Duration=%.2f, HC->IsInvincible=%d"),
		*Pawn->GetName(), Duration, HC->IsInvincible() ? 1 : 0);

	// 直接返回激活时存储的总时长（这是 HealthComponent 激活时写入的准确值）
	// 不再每帧动态计算（会导致进度逻辑错误）
	return Duration;
}

void URespawnProgressWidget::UpdateProgress(float RemainingSeconds, float TotalDuration)
{
	// 计算进度百分比 (0.0 ~ 1.0)
	// 进度从0到1：开始时0%，结束时100%
	float Progress = 0.0f;
	float Percentage = 0.0f;
	if (TotalDuration > 0.0f)
	{
		// 倒计时用完的比例 = (总时长 - 剩余) / 总时长
		Progress = FMath::Clamp((TotalDuration - RemainingSeconds) / TotalDuration, 0.0f, 1.0f);
		Percentage = Progress * 100.0f;
	}

	// 更新进度条
	if (PB_InvincibilityProgress)
	{
		PB_InvincibilityProgress->SetPercent(Progress);
	}

	// 更新倒计时文字 - 显示百分比格式 "X.XX%"
	//
	// 【v40.9 大厂架构 — 修复 FText::Format 格式错误】
	//   根因 (Session1.log 实证): 旧版用 FText::Format("{0:.2f}%", Percentage)
	//     - FText::Format 是基于 ICU 库, **不支持 printf 风格的 `:.2f`**
	//     - 每次 SetText 都触发: "LogTextFormatter: Failed to parse argument '0:.2f' as a number"
	//     - fallback 值是 "0", 所以倒计时百分比文字**永远是 "0%"** — 不实时更新
	//     - 用户报告 "复活进度条没有实时更新" 的真正根因
	//   修复 (v40.9): 用 FString::Printf (C++ 标准 printf) 格式化, 包成 FText
	//     - FString::Printf(TEXT("%.2f%%"), Percentage) → "12.34%"
	//     - FText::FromString(...) → FText (UMG 控件接受)
	//   零重复: 不影响进度条数值 (Progress = Remaining / Total), 只修字符串格式
	if (Text_Countdown)
	{
		const FString PercentText = FString::Printf(TEXT("%.2f%%"), Percentage);
		Text_Countdown->SetText(FText::FromString(PercentText));
	}

	UE_LOG(LogTemp, Verbose,
		TEXT("[RespawnProgressWidget] UpdateProgress: Progress=%.4f (%.2f%%), Remaining=%.2fs, Total=%.2fs"),
		Progress, Percentage, RemainingSeconds, TotalDuration);
}
