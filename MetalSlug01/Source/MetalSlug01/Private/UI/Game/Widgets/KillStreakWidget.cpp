// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Game/Widgets/KillStreakWidget.h"
#include "Components/Image.h"
#include "TimerManager.h"
#include "Engine/DataTable.h"
#include "Data/Tables/KillIconTableRow.h"


// ==========================================
// 1. 初始化
// ==========================================

/**
 * UKillStreakWidget::Initialize
 *
 * 1. 隐藏 Image_Kill
 * 2. 初始化连杀计数 = 0
 */
bool UKillStreakWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// 初始化隐藏图标
	if (Image_Kill)
	{
		Image_Kill->SetVisibility(ESlateVisibility::Hidden);
	}

	// 初始化连杀计数
	CurrentKillStreak = 0;
	bLastKillWasHeadshot = false;

	return true;
}


// ==========================================
// 2. 公共接口
// ==========================================

/**
 * UKillStreakWidget::SetKillStreakConfig
 *
 * v41 大厂架构重构: 简化接口, 直接注入两个参数
 * 由 GameHUDWidget 在 NativeConstruct 中调用
 *
 * @param InKillStreakDuration 连杀超时时间 (秒)
 * @param InIconDisplayDuration 图标显示时间 (秒)
 */
void UKillStreakWidget::SetKillStreakConfig(float InKillStreakDuration, float InIconDisplayDuration)
{
	KillStreakDuration = InKillStreakDuration;
	KillStreakIconDisplayDuration = InIconDisplayDuration;
	UE_LOG(LogTemp, Log, TEXT("[KillStreakWidget] SetKillStreakConfig: Duration=%.1f, IconDisplay=%.1f"),
		KillStreakDuration, KillStreakIconDisplayDuration);
}

/**
 * UKillStreakWidget::SetKillStreakIconDataTable
 */
void UKillStreakWidget::SetKillStreakIconDataTable(class UDataTable* InDataTable)
{
	KillStreakIconDataTable = InDataTable;
}

/**
 * UKillStreakWidget::RecordKill
 *
 * 1. 记录爆头状态
 * 2. 累加连杀数
 * 3. 重置计时器
 * 4. 更新图标
 * 5. 启动自动隐藏计时器
 */
void UKillStreakWidget::RecordKill(bool bIsHeadshot)
{
	if (!Image_Kill)
	{
		UE_LOG(LogTemp, Error, TEXT("[KillStreakWidget] RecordKill: Image_Kill 为空!"));
		return;
	}

	// 记录爆头状态
	bLastKillWasHeadshot = bIsHeadshot;

	// 累加连杀数
	CurrentKillStreak++;

	UE_LOG(LogTemp, Log, TEXT("[KillStreakWidget] RecordKill: Kills=%d, bIsHeadshot=%d"),
		CurrentKillStreak, bIsHeadshot);

	// 重置连杀计时器 (使用配置中的时间)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(KillStreakTimer);
		World->GetTimerManager().SetTimer(
			KillStreakTimer,
			this,
			&UKillStreakWidget::OnKillStreakExpired,
			KillStreakDuration,
			false
		);
	}

	// 更新图标显示
	UpdateKillIcon(CurrentKillStreak, bIsHeadshot);

	// 启动图标自动隐藏计时器 (使用配置中的时间)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(IconDisplayTimer);
		World->GetTimerManager().SetTimer(
			IconDisplayTimer,
			this,
			&UKillStreakWidget::HideIcon,
			KillStreakIconDisplayDuration,
			false
		);
	}
}


// ==========================================
// 3. 定时器回调
// ==========================================

/**
 * UKillStreakWidget::HideIcon
 *
 * 时机: IconDisplayTimer 到期
 * 用途: 隐藏 Image_Kill
 */
void UKillStreakWidget::HideIcon()
{
	SetIconVisibility(false);
}

/**
 * UKillStreakWidget::OnKillStreakExpired
 *
 * v41 Bug 修复: 超时后应隐藏图标，而非显示一杀
 *
 * 旧版问题:
 *   - OnKillStreakExpired 只重置 CurrentKillStreak = 0
 *   - 没有更新图标 → 之前没有击杀时，超时后显示一杀图标 (错误!)
 *
 * 新版修复:
 *   - 重置 CurrentKillStreak = 0
 *   - 重置 bLastKillWasHeadshot = false
 *   - 隐藏图标 (因为当前没有连杀)
 *
 * 大厂原则 - 零兜底:
 *   - 任何状态变化必须同步更新 UI
 *   - 连杀数为 0 时，图标必须隐藏
 */
void UKillStreakWidget::OnKillStreakExpired()
{
	UE_LOG(LogTemp, Log, TEXT("[KillStreakWidget] 连杀超时, 重置并隐藏图标"));

	// 重置连杀计数
	CurrentKillStreak = 0;
	bLastKillWasHeadshot = false;

	// v41 Bug 修复: 超时后隐藏图标 (因为当前没有连杀)
	// 而不是让图标保持旧状态或显示一杀
	SetIconVisibility(false);
}


// ==========================================
// 4. 核心逻辑
// ==========================================

/**
 * UKillStreakWidget::GetKillStreakType
 *
 * @param Kills 当前连杀数
 * @param bIsHeadshot 是否爆头
 * @return 对应的 EKillStreakType
 * 规则: 爆头优先显示爆头图标; 否则按 1~5 杀
 */
EKillStreakType UKillStreakWidget::GetKillStreakType(int32 Kills, bool bIsHeadshot) const
{
	// 爆头击杀优先显示爆头图标 (爆头音效也优先)
	if (bIsHeadshot)
	{
		return EKillStreakType::Headshot;
	}

	// 【业务规则 2026.07.26 — 封顶语义】
	//   "连杀超过五杀,再激活连杀还是五杀图标和声音"
	//   - Kills >= 5 统一归 FiveKills (不再区分 5/6/7/8 杀)
	//   - 玩家每次击杀 RecordKill 都会:
	//     (a) 重新显示 FiveKills 图标 (IconDisplayTimer 续期)
	//     (b) 重新播放 FiveKills 音效 (Multicast_NotifyKill → KillSoundComp)
	//   - 实现 = Kills >= 5 单一分支, 镜像 RoomPlayerState::ServerUpdateKillStreak
	if (Kills >= 5)
	{
		return EKillStreakType::FiveKills;
	}
	if (Kills >= 4)
	{
		return EKillStreakType::FourKills;
	}
	if (Kills >= 3)
	{
		return EKillStreakType::ThreeKills;
	}
	if (Kills >= 2)
	{
		return EKillStreakType::TwoKills;
	}
	if (Kills >= 1)
	{
		return EKillStreakType::OneKill;
	}
	return EKillStreakType::None;
}

/**
 * UKillStreakWidget::GetKillStreakIcon
 *
 * 从数据表按枚举名查找图标
 */
UTexture2D* UKillStreakWidget::GetKillStreakIcon(EKillStreakType StreakType)
{
	if (!KillStreakIconDataTable)
	{
		UE_LOG(LogTemp, Error, TEXT("[KillStreakWidget] KillStreakIconDataTable 未配置!"));
		return nullptr;
	}

	// 获取枚举值名称作为行名
	FString EnumValueName = UEnum::GetValueAsString(StreakType);
	if (EnumValueName.Contains(TEXT("::")))
	{
		EnumValueName = EnumValueName.RightChop(EnumValueName.Find(TEXT("::")) + 2);
	}

	// 从数据表查找
	static const FString ContextString(TEXT("KillStreakIconLookup"));
	FKillStreakIconInfo* Row = KillStreakIconDataTable->FindRow<FKillStreakIconInfo>(
		FName(*EnumValueName),
		ContextString,
		false
	);

	if (Row && Row->StreakIcon)
	{
		return Row->StreakIcon;
	}

	UE_LOG(LogTemp, Warning, TEXT("[KillStreakWidget] 未找到图标行: %s"), *EnumValueName);
	return nullptr;
}

/**
 * UKillStreakWidget::UpdateKillIcon
 *
 * v41 大厂架构重构: 接收 Kills 和 bIsHeadshot 参数
 *
 * 1. 获取 EKillStreakType
 * 2. 从数据表取图标
 * 3. 设置 Image_Kill 的画刷
 * 4. 找不到时隐藏图标
 *
 * @param Kills 当前连杀数
 * @param bIsHeadshot 是否爆头
 */
void UKillStreakWidget::UpdateKillIcon(int32 Kills, bool bIsHeadshot)
{
	if (!Image_Kill)
	{
		return;
	}

	// 获取击杀类型
	EKillStreakType StreakType = GetKillStreakType(Kills, bIsHeadshot);

	// 从数据表获取图标
	UTexture2D* Icon = GetKillStreakIcon(StreakType);

	if (Icon)
	{
		FSlateBrush Brush = Image_Kill->GetBrush();
		Brush.SetResourceObject(Icon);
		Image_Kill->SetBrush(Brush);
		SetIconVisibility(true);

		UE_LOG(LogTemp, Log, TEXT("[KillStreakWidget] 显示图标: %s (Kills=%d, Headshot=%d)"),
			*Icon->GetName(), Kills, bIsHeadshot);
	}
	else
	{
		// 数据表未配置时隐藏图标
		SetIconVisibility(false);
	}
}

/**
 * UKillStreakWidget::SetIconVisibility
 *
 * 封装图标显示/隐藏逻辑
 *
 * @param bVisible 是否显示
 */
void UKillStreakWidget::SetIconVisibility(bool bVisible)
{
	if (Image_Kill)
	{
		Image_Kill->SetVisibility(bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
	}
}
