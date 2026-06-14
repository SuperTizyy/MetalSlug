// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Game/Widgets/KillStreakWidget.h"
#include "Components/Image.h"
#include "TimerManager.h"
#include "Engine/DataTable.h"


// ==========================================
// 1. 初始化
// ==========================================

/**
 * UKillStreakWidget::Initialize
 *
 * 1. 隐藏 Image_Kill
 * 2. 初始化连杀计数 = 0
 * 3. 初始化 bLastKillWasHeadshot = false
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
 * UKillStreakWidget::RecordKill
 *
 * 1. 记录爆头状态
 * 2. 累加连杀数
 * 3. 重置 1 分钟连杀计时器
 * 4. 更新图标
 * 5. 启动 3 秒图标自动隐藏计时器
 */
void UKillStreakWidget::RecordKill(bool bIsHeadshot)
{
	UE_LOG(LogTemp, Log, TEXT("[KillStreakWidget] RecordKill 开始"));
	if (!Image_Kill)
	{
		UE_LOG(LogTemp, Error, TEXT("[KillStreakWidget] RecordKill: Image_Kill 为空!"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[KillStreakWidget] RecordKill 被调用: bIsHeadshot=%d"), bIsHeadshot);
	UE_LOG(LogTemp, Log, TEXT("[KillStreakWidget] RecordKill: Image_Kill=%s, DataTable=%s"),
		*Image_Kill->GetName(),
		KillStreakIconDataTable ? *KillStreakIconDataTable->GetName() : TEXT("NULL"));

	// 记录爆头状态
	bLastKillWasHeadshot = bIsHeadshot;

	// 累加连杀数
	CurrentKillStreak++;

	UE_LOG(LogTemp, Log, TEXT("[KillStreakWidget] RecordKill: Kills=%d, bIsHeadshot=%d"), CurrentKillStreak, bIsHeadshot);

	// 重置 1 分钟连杀计时器
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
	UpdateKillIcon();

	// 启动图标自动隐藏计时器
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(IconDisplayTimer);
		World->GetTimerManager().SetTimer(
			IconDisplayTimer,
			this,
			&UKillStreakWidget::HideIcon,
			IconDisplayDuration,
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
 * 时机: IconDisplayTimer 到期（3 秒）
 * 用途: 隐藏 Image_Kill
 */
void UKillStreakWidget::HideIcon()
{
	if (Image_Kill)
	{
		Image_Kill->SetVisibility(ESlateVisibility::Hidden);
	}
}


/**
 * UKillStreakWidget::OnKillStreakExpired
 *
 * 时机: KillStreakTimer 到期（1 分钟无击杀）
 * 用途: 重置 CurrentKillStreak = 0
 */
void UKillStreakWidget::OnKillStreakExpired()
{
	UE_LOG(LogTemp, Log, TEXT("[KillStreakWidget] 连杀超时，重置"));
	CurrentKillStreak = 0;
	bLastKillWasHeadshot = false;
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
 * 规则: 爆头优先显示爆头图标；否则按 1~5 杀
 */
EKillStreakType UKillStreakWidget::GetKillStreakType(int32 Kills, bool bIsHeadshot) const
{
	// 爆头击杀优先显示爆头图标
	if (bIsHeadshot)
	{
		return EKillStreakType::Headshot;
	}

	// 根据击杀数返回对应图标类型
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
 * 1. UEnum::GetValueAsString 获取 "EKillStreakType::OneKill" 格式
 * 2. 截取 "OneKill" 部分
 * 3. FindRow<FKillStreakIconInfo> 查找
 * @return 图标贴图（找不到返回 nullptr）
 */
UTexture2D* UKillStreakWidget::GetKillStreakIcon(EKillStreakType StreakType)
{
	if (!KillStreakIconDataTable)
	{
		UE_LOG(LogTemp, Error, TEXT("[KillStreakWidget] KillStreakIconDataTable 未配置!"));
		return nullptr;
	}

	// 获取枚举值名称作为行名（英文，与 DataTable 行名对应）
	FString EnumValueName = UEnum::GetValueAsString(StreakType);
	if (EnumValueName.Contains(TEXT("::")))
	{
		EnumValueName = EnumValueName.RightChop(EnumValueName.Find(TEXT("::")) + 2);
	}

	// 获取 UMETA DisplayName（仅用于日志调试）
	FText DisplayText = StaticEnum<EKillStreakType>()->GetDisplayNameTextByValue(static_cast<int64>(StreakType));
	FString DisplayName = DisplayText.ToString();

	UE_LOG(LogTemp, Log, TEXT("[KillStreakWidget] 查找图标: 枚举=%s, DisplayName=%s"),
		*EnumValueName, *DisplayName);

	UE_LOG(LogTemp, Log, TEXT("[KillStreakWidget] DataTable=0x%p, RowCount=%d, 全部行名如下:"), (void*)KillStreakIconDataTable, KillStreakIconDataTable->GetRowNames().Num());
	for (const FName& RN : KillStreakIconDataTable->GetRowNames())
	{
		UE_LOG(LogTemp, Log, TEXT("  [%s]"), *RN.ToString());
	}
	UE_LOG(LogTemp, Log, TEXT("  查找目标=[%s]"), *EnumValueName);

	// 使用英文行名从数据表查找
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
 * 1. 获取 EKillStreakType
 * 2. 从数据表取图标
 * 3. 设置 Image_Kill 的画刷
 * 4. 找不到时隐藏图标
 */
void UKillStreakWidget::UpdateKillIcon()
{
	if (!Image_Kill)
	{
		return;
	}

	// 获取击杀类型
	EKillStreakType StreakType = GetKillStreakType(CurrentKillStreak, bLastKillWasHeadshot);

	// 从数据表获取图标
	UTexture2D* Icon = GetKillStreakIcon(StreakType);

	if (Icon)
	{
		FSlateBrush Brush = Image_Kill->GetBrush();
		Brush.SetResourceObject(Icon);
		Image_Kill->SetBrush(Brush);
		Image_Kill->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		UE_LOG(LogTemp, Log, TEXT("[KillStreakWidget] 显示图标: %s"), *Icon->GetName());
	}
	else
	{
		// 数据表未配置时隐藏图标
		Image_Kill->SetVisibility(ESlateVisibility::Hidden);
	}
}
