// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Game/Widgets/SubWidgets/KillFeedEntryWidget.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"


// ==========================================
// 1. 公共接口
// ==========================================

/**
 * UKillFeedEntryWidget::SetKillInfo
 *
 * 1. 设置击杀者名称
 * 2. 设置被击杀者名称
 * 3. 设置击杀方式（会同时更新图标）
 */
void UKillFeedEntryWidget::SetKillInfo(const FString& InKillerName, const FString& InVictimName, EKillMethod InKillMethod)
{
	// 设置击杀者名称
	SetKillerName(InKillerName);

	// 设置被击杀者名称
	SetVictimName(InVictimName);

	// 设置击杀方式（会同时更新图标）
	SetKillMethod(InKillMethod);
}


/**
 * UKillFeedEntryWidget::SetKillerName
 *
 * 设置击杀者名称，使用青色高亮显示 (0, 1, 1, 1)
 */
void UKillFeedEntryWidget::SetKillerName(const FString& InKillerName)
{
	if (Text_KillerName)
	{
		// 设置击杀者名称，使用青色高亮显示
		Text_KillerName->SetText(FText::FromString(InKillerName));
		Text_KillerName->SetColorAndOpacity(FSlateColor(FLinearColor(0.0f, 1.0f, 1.0f, 1.0f)));
	}
}


/**
 * UKillFeedEntryWidget::SetVictimName
 *
 * 设置被击杀者名称，使用橙色高亮显示 (1, 0.5, 0, 1)
 */
void UKillFeedEntryWidget::SetVictimName(const FString& InVictimName)
{
	if (Text_VictimName)
	{
		// 设置被击杀者名称，使用橙色高亮显示
		Text_VictimName->SetText(FText::FromString(InVictimName));
		Text_VictimName->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.5f, 0.0f, 1.0f)));
	}
}


/**
 * UKillFeedEntryWidget::SetKillMethod
 *
 * 从数据表中查找并设置对应的击杀图标
 */
void UKillFeedEntryWidget::SetKillMethod(EKillMethod InKillMethod)
{
	// 从数据表中查找并设置对应的击杀图标
	FindKillIcon(InKillMethod);
}


// ==========================================
// 2. 数据表注入
// ==========================================

/**
 * UKillFeedEntryWidget::SetKillIconDataTable
 *
 * 供父控件注入数据表引用
 */
void UKillFeedEntryWidget::SetKillIconDataTable(class UDataTable* InDataTable)
{
	KillIconDataTable = InDataTable;
}


// ==========================================
// 3. 辅助判断
// ==========================================

/**
 * UKillFeedEntryWidget::IsHeadshotKill
 *
 * @param InKillMethod 击杀方式
 * @return 是否为爆头
 */
bool UKillFeedEntryWidget::IsHeadshotKill(EKillMethod InKillMethod) const
{
	// 根据击杀方式判断是否为爆头
	switch (InKillMethod)
	{
	case EKillMethod::PrimaryHeadshot:
	case EKillMethod::SecondaryHeadshot:
	case EKillMethod::MeleeHeadshot:
		return true;
	default:
		return false;
	}
}


/**
 * UKillFeedEntryWidget::GetIconColor
 *
 * @param InKillMethod 击杀方式
 * @return 图标颜色
 * - 主武器: 绿 (0, 1, 0)
 * - 副武器: 蓝 (0.3, 0.5, 1)
 * - 近战: 红 (1, 0.3, 0.3)
 * - 默认: 白
 */
FLinearColor UKillFeedEntryWidget::GetIconColor(EKillMethod InKillMethod) const
{
	// 根据击杀方式返回图标颜色
	switch (InKillMethod)
	{
	case EKillMethod::PrimaryWeapon:
	case EKillMethod::PrimaryHeadshot:
		// 主武器: 绿色
		return FLinearColor(0.0f, 1.0f, 0.0f, 1.0f);
	case EKillMethod::SecondaryWeapon:
	case EKillMethod::SecondaryHeadshot:
		// 副武器: 蓝色
		return FLinearColor(0.3f, 0.5f, 1.0f, 1.0f);
	case EKillMethod::MeleeWeapon:
	case EKillMethod::MeleeHeadshot:
		// 近战武器: 红色
		return FLinearColor(1.0f, 0.3f, 0.3f, 1.0f);
	default:
		// 默认: 白色
		return FLinearColor::White;
	}
}


// ==========================================
// 4. 数据表查找
// ==========================================

/**
 * UKillFeedEntryWidget::FindKillIcon
 *
 * 遍历数据表查找匹配的击杀方式
 * 使用 ForeachRow + lambda
 * 找到后立即设置 Image_KillIcon
 */
class UTexture2D* UKillFeedEntryWidget::FindKillIcon(EKillMethod InKillMethod)
{
	// 如果没有配置数据表或 Image 组件无效，直接返回
	if (!KillIconDataTable || !Image_KillIcon)
	{
		UE_LOG(LogTemp, Warning, TEXT("[KillFeed] FindKillIcon: KillIconDataTable=%s, Image_KillIcon=%s"),
			*GetNameSafe(KillIconDataTable), *GetNameSafe(Image_KillIcon));
		return nullptr;
	}

	bool bFound = false;
	// 遍历数据表查找匹配的击杀方式
	KillIconDataTable->ForeachRow<FKillIconInfo>(TEXT("LookupKillIcon"),
		[InKillMethod, this, &bFound](const FName& RowName, const FKillIconInfo& Row)
		{
			UE_LOG(LogTemp, Log, TEXT("[KillFeed] Check row '%s': Row.KillMethod=%d, LookingFor=%d, Icon=%s"),
				*RowName.ToString(), (int32)Row.KillMethod, (int32)InKillMethod, *GetNameSafe(Row.KillIcon));
			if (Row.KillMethod == InKillMethod && Row.KillIcon)
			{
				// 找到匹配的行，设置图标
				Image_KillIcon->SetBrushFromTexture(Row.KillIcon);
				bFound = true;
				UE_LOG(LogTemp, Log, TEXT("[KillFeed] MATCH! Set icon for KillMethod=%d from row '%s'"), (int32)InKillMethod, *RowName.ToString());
			}
		});

	if (!bFound)
	{
		UE_LOG(LogTemp, Warning, TEXT("[KillFeed] FindKillIcon: No matching icon found for KillMethod=%d! Please check DT_KillIcon data table."), (int32)InKillMethod);
	}

	return nullptr;
}
