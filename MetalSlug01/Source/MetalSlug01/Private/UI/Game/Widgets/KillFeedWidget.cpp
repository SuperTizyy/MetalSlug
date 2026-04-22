#include "UI/Game/Widgets/KillFeedWidget.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/HorizontalBox.h"
#include "Components/Widget.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Game/Widgets/SubWidgets/KillFeedEntryWidget.h"

void UKillFeedWidget::AddKillInfo(const FString& KillerName, const FString& VictimName, EKillMethod KillMethod)
{
	if (!VB_KillFeed)
	{
		return;
	}

	UWidget* MessageWidget = CreateKillMessage(KillerName, VictimName, KillMethod);
	if (MessageWidget)
	{
		VB_KillFeed->AddChild(MessageWidget);

		// 超过最大数量时移除最早的
		while (VB_KillFeed->GetChildrenCount() > MaxKillMessages)
		{
			VB_KillFeed->RemoveChildAt(0);
		}
	}
}

void UKillFeedWidget::AddKillInfoEx(const FString& KillerName, const FString& VictimName, EKillMethod KillMethod, bool bIsHeadshot)
{
	if (!VB_KillFeed)
	{
		return;
	}

	// 根据基础击杀方式和爆头状态获取最终的击杀方法
	EKillMethod FinalKillMethod = GetFinalKillMethod(KillMethod, bIsHeadshot);

	UWidget* MessageWidget = CreateKillMessage(KillerName, VictimName, FinalKillMethod);
	if (MessageWidget)
	{
		VB_KillFeed->AddChild(MessageWidget);

		// 超过最大数量时移除最早的
		while (VB_KillFeed->GetChildrenCount() > MaxKillMessages)
		{
			VB_KillFeed->RemoveChildAt(0);
		}
	}
}

void UKillFeedWidget::AddKillInfoWithHeadshot(const FString& KillerName, const FString& VictimName, bool bIsHeadshot)
{
	if (!VB_KillFeed)
	{
		return;
	}

	UWidget* MessageWidget = CreateKillMessageOld(KillerName, VictimName, bIsHeadshot);
	if (MessageWidget)
	{
		VB_KillFeed->AddChild(MessageWidget);

		// 超过最大数量时移除最早的
		while (VB_KillFeed->GetChildrenCount() > MaxKillMessages)
		{
			VB_KillFeed->RemoveChildAt(0);
		}
	}
}

void UKillFeedWidget::AddSystemMessage(const FString& Message)
{
	if (!VB_KillFeed)
	{
		return;
	}

	UWidget* MessageWidget = CreateSystemMessage(Message);
	if (MessageWidget)
	{
		VB_KillFeed->AddChild(MessageWidget);

		// 超过最大数量时移除最早的
		while (VB_KillFeed->GetChildrenCount() > MaxKillMessages)
		{
			VB_KillFeed->RemoveChildAt(0);
		}
	}
}

void UKillFeedWidget::ClearKillFeed()
{
	if (VB_KillFeed)
	{
		VB_KillFeed->ClearChildren();
	}
}

void UKillFeedWidget::SetKillIconDataTable(class UDataTable* InDataTable)
{
	KillIconDataTable = InDataTable;
}

bool UKillFeedWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// 设置定时清理
	FTimerHandle CleanupTimer;
	GetWorld()->GetTimerManager().SetTimer(
		CleanupTimer,
		this,
		&UKillFeedWidget::RemoveExpiredMessages,
		MessageLifetime,
		true
	);

	return true;
}

UWidget* UKillFeedWidget::CreateKillMessage(const FString& KillerName, const FString& VictimName, EKillMethod KillMethod)
{
	// 如果配置了子控件类，使用子控件创建击杀信息
	if (KillFeedEntryWidgetClass)
	{
		UKillFeedEntryWidget* EntryWidget = CreateWidget<UKillFeedEntryWidget>(this, KillFeedEntryWidgetClass);
		if (EntryWidget)
		{
			// 传递数据表给子控件，以便查找图标
			EntryWidget->SetKillIconDataTable(KillIconDataTable);
			EntryWidget->SetKillInfo(KillerName, VictimName, KillMethod);
			return EntryWidget;
		}
	}

	// 如果没有配置子控件类，fallback 到旧的方式创建
	return CreateKillMessageOld(KillerName, VictimName, IsHeadshotFromKillMethod(KillMethod));
}

UWidget* UKillFeedWidget::CreateKillMessageOld(const FString& KillerName, const FString& VictimName, bool bIsHeadshot)
{
	UOverlay* MessageContainer = NewObject<UOverlay>(this);
	if (!MessageContainer)
	{
		return nullptr;
	}

	// 创建击杀者名称文本
	UTextBlock* KillerText = NewObject<UTextBlock>(MessageContainer);
	if (KillerText)
	{
		KillerText->SetText(FText::FromString(KillerName));
		KillerText->SetColorAndOpacity(FSlateColor(FLinearColor(0.0f, 1.0f, 1.0f, 1.0f))); // Cyan color
		KillerText->SetFont(FSlateFontInfo(FSlateFontInfo().FontObject, 12));
		MessageContainer->AddChild(KillerText);
	}

	// 创建 " → " 分隔符
	UTextBlock* ArrowText = NewObject<UTextBlock>(MessageContainer);
	if (ArrowText)
	{
		ArrowText->SetText(NSLOCTEXT("KillFeed", "KillArrow", " → "));
		ArrowText->SetColorAndOpacity(FLinearColor::White);
		ArrowText->SetFont(FSlateFontInfo(FSlateFontInfo().FontObject, 12));
		MessageContainer->AddChild(ArrowText);
	}

	// 创建被击杀者名称文本
	UTextBlock* VictimText = NewObject<UTextBlock>(MessageContainer);
	if (VictimText)
	{
		VictimText->SetText(FText::FromString(VictimName));
		VictimText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.5f, 0.0f, 1.0f))); // Orange color
		VictimText->SetFont(FSlateFontInfo(FSlateFontInfo().FontObject, 12));
		MessageContainer->AddChild(VictimText);
	}

	// 如果是爆头，添加爆头标识
	if (bIsHeadshot)
	{
		UTextBlock* HeadshotText = NewObject<UTextBlock>(MessageContainer);
		if (HeadshotText)
		{
			HeadshotText->SetText(NSLOCTEXT("KillFeed", "Headshot", " [爆头]"));
			HeadshotText->SetColorAndOpacity(FLinearColor::Red);
			HeadshotText->SetFont(FSlateFontInfo(FSlateFontInfo().FontObject, 12));
			MessageContainer->AddChild(HeadshotText);
		}
	}

	return MessageContainer;
}

UWidget* UKillFeedWidget::CreateSystemMessage(const FString& Message)
{
	UOverlay* Container = NewObject<UOverlay>(this);
	if (!Container)
	{
		return nullptr;
	}

	UTextBlock* MessageText = NewObject<UTextBlock>(Container);
	if (MessageText)
	{
		MessageText->SetText(FText::FromString(Message));
		MessageText->SetColorAndOpacity(FLinearColor::Yellow);
		MessageText->SetFont(FSlateFontInfo(FSlateFontInfo().FontObject, 12));
		Container->AddChild(MessageText);
	}

	return Container;
}

void UKillFeedWidget::RemoveExpiredMessages()
{
	// 如果消息数量超过限制，移除最早的
	if (VB_KillFeed && VB_KillFeed->GetChildrenCount() > MaxKillMessages)
	{
		VB_KillFeed->RemoveChildAt(0);
	}
}

bool UKillFeedWidget::IsHeadshotFromKillMethod(EKillMethod KillMethod) const
{
	// 根据击杀方式判断是否为爆头
	switch (KillMethod)
	{
	case EKillMethod::PrimaryHeadshot:
	case EKillMethod::SecondaryHeadshot:
	case EKillMethod::MeleeHeadshot:
		return true;
	default:
		return false;
	}
}

EKillMethod UKillFeedWidget::GetFinalKillMethod(EKillMethod BaseMethod, bool bIsHeadshot)
{
	// 如果基础方法已经包含了爆头信息，直接返回
	if (IsHeadshotFromKillMethod(BaseMethod))
	{
		return BaseMethod;
	}

	// 如果不是爆头，返回基础方法
	if (!bIsHeadshot)
	{
		return BaseMethod;
	}

	// 根据基础武器类型和爆头状态返回对应的击杀方法
	switch (BaseMethod)
	{
	case EKillMethod::PrimaryWeapon:
		return EKillMethod::PrimaryHeadshot;
	case EKillMethod::SecondaryWeapon:
		return EKillMethod::SecondaryHeadshot;
	case EKillMethod::MeleeWeapon:
		return EKillMethod::MeleeHeadshot;
	default:
		return BaseMethod;
	}
}