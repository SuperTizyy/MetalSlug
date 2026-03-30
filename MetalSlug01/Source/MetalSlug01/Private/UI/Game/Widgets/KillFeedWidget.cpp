#include "UI/Game/Widgets/KillFeedWidget.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Kismet/GameplayStatics.h"

void UKillFeedWidget::AddKillInfo(const FString& KillerName, const FString& VictimName, bool bIsHeadshot)
{
	if (!VB_KillFeed)
	{
		return;
	}

	UUserWidget* MessageWidget = CreateKillMessage(KillerName, VictimName, bIsHeadshot);
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

	UUserWidget* MessageWidget = CreateSystemMessage(Message);
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

UUserWidget* UKillFeedWidget::CreateKillMessage(const FString& KillerName, const FString& VictimName, bool bIsHeadshot)
{
	// 创建消息容器面板
	UCanvasPanel* MessagePanel = NewObject<UCanvasPanel>(this);
	if (!MessagePanel)
	{
		return nullptr;
	}

	// 创建击杀者名称文本
	UTextBlock* KillerText = NewObject<UTextBlock>(MessagePanel);
	if (KillerText)
	{
		KillerText->SetText(FText::FromString(KillerName));
		KillerText->SetColorAndOpacity(FLinearColor::Cyan);
		KillerText->SetFont(FSlateFontInfo(FSlateFontInfo().FontObject, 12));
		MessagePanel->AddChild(KillerText);
	}

	// 创建 " → " 分隔符
	UTextBlock* ArrowText = NewObject<UTextBlock>(MessagePanel);
	if (ArrowText)
	{
		ArrowText->SetText(NSLOCTEXT("KillFeed", "KillArrow", " → "));
		ArrowText->SetColorAndOpacity(FLinearColor::White);
		ArrowText->SetFont(FSlateFontInfo(FSlateFontInfo().FontObject, 12));
		MessagePanel->AddChild(ArrowText);
	}

	// 创建被击杀者名称文本
	UTextBlock* VictimText = NewObject<UTextBlock>(MessagePanel);
	if (VictimText)
	{
		VictimText->SetText(FText::FromString(VictimName));
		VictimText->SetColorAndOpacity(FLinearColor::Orange);
		VictimText->SetFont(FSlateFontInfo(FSlateFontInfo().FontObject, 12));
		MessagePanel->AddChild(VictimText);
	}

	// 如果是爆头，添加爆头标识
	if (bIsHeadshot)
	{
		UTextBlock* HeadshotText = NewObject<UTextBlock>(MessagePanel);
		if (HeadshotText)
		{
			HeadshotText->SetText(NSLOCTEXT("KillFeed", "Headshot", " [HEADSHOT]"));
			HeadshotText->SetColorAndOpacity(FLinearColor::Red);
			HeadshotText->SetFont(FSlateFontInfo(FSlateFontInfo().FontObject, 12));
			MessagePanel->AddChild(HeadshotText);
		}
	}

	return MessagePanel;
}

UUserWidget* UKillFeedWidget::CreateSystemMessage(const FString& Message)
{
	UTextBlock* MessageText = NewObject<UTextBlock>(this);
	if (MessageText)
	{
		MessageText->SetText(FText::FromString(Message));
		MessageText->SetColorAndOpacity(FLinearColor::Yellow);
		MessageText->SetFont(FSlateFontInfo(FSlateFontInfo().FontObject, 12));
	}

	return MessageText;
}

void UKillFeedWidget::RemoveExpiredMessages()
{
	// 如果消息数量超过限制，移除最早的
	if (VB_KillFeed && VB_KillFeed->GetChildrenCount() > MaxKillMessages)
	{
		VB_KillFeed->RemoveChildAt(0);
	}
}