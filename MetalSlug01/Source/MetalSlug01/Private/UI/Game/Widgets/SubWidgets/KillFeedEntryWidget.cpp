#include "UI/Game/Widgets/SubWidgets/KillFeedEntryWidget.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"

void UKillFeedEntryWidget::SetKillInfo(const FString& InKillerName, const FString& InVictimName, EKillMethod InKillMethod)
{
	// 设置击杀者名称
	SetKillerName(InKillerName);

	// 设置被击杀者名称
	SetVictimName(InVictimName);

	// 设置击杀方式（会同时更新图标）
	SetKillMethod(InKillMethod);
}

void UKillFeedEntryWidget::SetKillerName(const FString& InKillerName)
{
	if (Text_KillerName)
	{
		// 设置击杀者名称，使用青色高亮显示
		Text_KillerName->SetText(FText::FromString(InKillerName));
		Text_KillerName->SetColorAndOpacity(FSlateColor(FLinearColor(0.0f, 1.0f, 1.0f, 1.0f)));
	}
}

void UKillFeedEntryWidget::SetVictimName(const FString& InVictimName)
{
	if (Text_VictimName)
	{
		// 设置被击杀者名称，使用橙色高亮显示
		Text_VictimName->SetText(FText::FromString(InVictimName));
		Text_VictimName->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.5f, 0.0f, 1.0f)));
	}
}

void UKillFeedEntryWidget::SetKillMethod(EKillMethod InKillMethod)
{
	// 从数据表中查找并设置对应的击杀图标
	FindKillIcon(InKillMethod);
}

void UKillFeedEntryWidget::SetKillIconDataTable(class UDataTable* InDataTable)
{
	KillIconDataTable = InDataTable;
}

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

FLinearColor UKillFeedEntryWidget::GetIconColor(EKillMethod InKillMethod) const
{
	// 根据击杀方式返回图标颜色
	switch (InKillMethod)
	{
	case EKillMethod::PrimaryWeapon:
	case EKillMethod::PrimaryHeadshot:
		// 主武器：绿色
		return FLinearColor(0.0f, 1.0f, 0.0f, 1.0f);
	case EKillMethod::SecondaryWeapon:
	case EKillMethod::SecondaryHeadshot:
		// 副武器：蓝色
		return FLinearColor(0.3f, 0.5f, 1.0f, 1.0f);
	case EKillMethod::MeleeWeapon:
	case EKillMethod::MeleeHeadshot:
		// 近战武器：红色
		return FLinearColor(1.0f, 0.3f, 0.3f, 1.0f);
	default:
		// 默认：白色
		return FLinearColor::White;
	}
}

class UTexture2D* UKillFeedEntryWidget::FindKillIcon(EKillMethod InKillMethod)
{
	// 如果没有配置数据表或 Image 组件无效，直接返回
	if (!KillIconDataTable || !Image_KillIcon)
	{
		return nullptr;
	}

	// 遍历数据表查找匹配的击杀方式
	KillIconDataTable->ForeachRow<FKillIconInfo>(TEXT("LookupKillIcon"),
		[InKillMethod, this](const FName& RowName, const FKillIconInfo& Row)
		{
			if (Row.KillMethod == InKillMethod && Row.KillIcon)
			{
				// 找到匹配的行，设置图标
				Image_KillIcon->SetBrushFromTexture(Row.KillIcon);
			}
		});

	return nullptr;
}
