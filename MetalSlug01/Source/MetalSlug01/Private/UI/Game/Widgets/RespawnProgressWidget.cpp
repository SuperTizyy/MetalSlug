// ==========================================
// 复活进度条 Widget 实现
// 关联头文件: RespawnProgressWidget.h
//
// 【2026.07.14 重构说明】
// 移除了 CharacterEvents 订阅逻辑（由 UGameHUDWidget 统一订阅）
// 本 Widget 只负责内容更新，不负责事件订阅
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

	// 只有在显示状态才更新
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
	if (Text_Countdown)
	{
		FText PercentageText = FText::Format(
			NSLOCTEXT("RespawnProgress", "PercentageFormat", "{0:.2f}%"),
			Percentage);
		Text_Countdown->SetText(PercentageText);
	}

	UE_LOG(LogTemp, Verbose,
		TEXT("[RespawnProgressWidget] UpdateProgress: Progress=%.4f (%.2f%%), Remaining=%.2fs, Total=%.2fs"),
		Progress, Percentage, RemainingSeconds, TotalDuration);
}
