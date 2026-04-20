#include "UI/Game/Widgets/ChatWidget.h"
#include "Components/ScrollBox.h"
#include "Components/EditableTextBox.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/Widget.h"
#include "GameFramework/PlayerState.h"

bool UChatWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// 绑定发送按钮事件
	if (Btn_Send)
	{
		Btn_Send->OnClicked.AddDynamic(this, &UChatWidget::OnSendClicked);
	}

	// 绑定输入框回车事件
	if (ETB_ChatInput)
	{
		ETB_ChatInput->OnTextCommitted.AddDynamic(this, &UChatWidget::OnChatInputCommitted);
	}

	return true;
}

void UChatWidget::SetInputFocused(bool bFocused)
{
	bIsInputFocused = bFocused;

	if (ETB_ChatInput)
	{
		if (bFocused)
		{
			// 激活输入框：清空内容 + 设置焦点
			ETB_ChatInput->SetText(FText::GetEmpty());
			ETB_ChatInput->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			ETB_ChatInput->SetUserFocus(GetOwningPlayer());
		}
		else
		{
			// 隐藏输入框并清空内容
			ETB_ChatInput->SetText(FText::GetEmpty());
			ETB_ChatInput->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UChatWidget::ToggleChatInput()
{
	SetInputFocused(!bIsInputFocused);
}

void UChatWidget::AddChatMessage(const FString& PlayerName, const FString& Message)
{
	if (!SB_ChatMessages)
	{
		return;
	}

	UWidget* MessageWidget = CreateChatMessageWidget(PlayerName, Message);
	if (MessageWidget)
	{
		SB_ChatMessages->AddChild(MessageWidget);

		// 超过最大数量时移除最早的
		while (SB_ChatMessages->GetChildrenCount() > MaxChatMessages)
		{
			SB_ChatMessages->RemoveChildAt(0);
		}

		// 自动滚动到底部
		ScrollToBottom();
	}
}

void UChatWidget::AddSystemMessage(const FString& Message)
{
	if (!SB_ChatMessages)
	{
		return;
	}

	UTextBlock* SystemText = NewObject<UTextBlock>(this);
	if (SystemText)
	{
		SystemText->SetText(FText::FromString(Message));
		SystemText->SetColorAndOpacity(FLinearColor::Yellow);
		SystemText->SetFont(FSlateFontInfo(FSlateFontInfo().FontObject, 12));

		SB_ChatMessages->AddChild(SystemText);

		// 超过最大数量时移除最早的
		while (SB_ChatMessages->GetChildrenCount() > MaxChatMessages)
		{
			SB_ChatMessages->RemoveChildAt(0);
		}

		ScrollToBottom();
	}
}

void UChatWidget::ClearChat()
{
	if (SB_ChatMessages)
	{
		SB_ChatMessages->ClearChildren();
	}
}

void UChatWidget::OnSendClicked()
{
	if (ETB_ChatInput)
	{
		FString Message = ETB_ChatInput->GetText().ToString();
		if (!Message.IsEmpty())
		{
			OnChatInputCommitted(FText::FromString(Message), ETextCommit::OnEnter);
		}
	}
}

void UChatWidget::OnChatInputCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		FString Message = Text.ToString();
		if (!Message.IsEmpty())
		{
			// 从 PlayerState 获取玩家名称
			FString PlayerName = TEXT("Player");
			if (APlayerController* PC = GetOwningPlayer())
			{
				if (APlayerState* PS = PC->GetPlayerState<APlayerState>())
				{
					PlayerName = PS->GetPlayerName();
				}
			}

			// 广播消息，由外部（Controller）处理网络发送
			OnChatMessageReady.Broadcast(PlayerName, Message);

			// 本地显示
			AddChatMessage(PlayerName, Message);
		}

		// 发送完毕后：清空输入框并退出输入模式（把控制权交还游戏）
		if (ETB_ChatInput)
		{
			ETB_ChatInput->SetText(FText::GetEmpty());
		}
		SetInputFocused(false);
	}
}

UWidget* UChatWidget::CreateChatMessageWidget(const FString& PlayerName, const FString& Message)
{
	UOverlay* MessageContainer = NewObject<UOverlay>(this);
	if (!MessageContainer)
	{
		return nullptr;
	}
	
	// 玩家名称
	UTextBlock* NameText = NewObject<UTextBlock>(MessageContainer);
	if (NameText)
	{
		NameText->SetText(FText::FromString(PlayerName + TEXT(": ")));
		NameText->SetColorAndOpacity(FSlateColor(FLinearColor(0.0f, 1.0f, 1.0f, 1.0f))); // Cyan color
		NameText->SetFont(FSlateFontInfo(FSlateFontInfo().FontObject, 12));
		MessageContainer->AddChild(NameText);
	}
	
	// 消息内容
	UTextBlock* MessageText = NewObject<UTextBlock>(MessageContainer);
	if (MessageText)
	{
		MessageText->SetText(FText::FromString(Message));
		MessageText->SetColorAndOpacity(FLinearColor::White);
		MessageText->SetFont(FSlateFontInfo(FSlateFontInfo().FontObject, 12));
		MessageContainer->AddChild(MessageText);
	}
	
	return MessageContainer;
}

void UChatWidget::ScrollToBottom()
{
	if (SB_ChatMessages)
	{
		SB_ChatMessages->ScrollToEnd();
	}
}