#include "UI/Game/Widgets/KillFeedWidget.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/HorizontalBox.h"
#include "Components/Widget.h"
#include "Kismet/GameplayStatics.h"

void UKillFeedWidget::AddKillInfo(const FString& KillerName, const FString& VictimName, bool bIsHeadshot)
{
	if (!VB_KillFeed)
	{
		return;
	}

	UWidget* MessageWidget = CreateKillMessage(KillerName, VictimName, bIsHeadshot);
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

UWidget* UKillFeedWidget::CreateKillMessage(const FString& KillerName, const FString& VictimName, bool bIsHeadshot)
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
			HeadshotText->SetText(NSLOCTEXT("KillFeed", "Headshot", " [HEADSHOT]"));
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