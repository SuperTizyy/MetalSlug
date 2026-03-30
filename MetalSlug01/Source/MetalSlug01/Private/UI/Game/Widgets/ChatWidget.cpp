#include "UI/Game/Widgets/ChatWidget.h"
#include "Components/ScrollBox.h"
#include "Components/EditableTextBox.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"

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

void UChatWidget::AddChatMessage(const FString& PlayerName, const FString& Message)
{
	if (!SB_ChatMessages)
	{
		return;
	}

	UUserWidget* MessageWidget = CreateChatMessageWidget(PlayerName, Message);
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
			// 发送消息事件（实际发送逻辑需要在Controller或GameMode中实现）
			OnChatInputCommitted(FText::FromString(Message), ETextCommit::OnEnter);

			// 清空输入框
			ETB_ChatInput->SetText(FText::GetEmpty());
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
			// 本地显示玩家消息（玩家名称从Controller获取）
			FString PlayerName = TEXT("Player");
			if (APlayerController* PC = GetOwningPlayer())
			{
				if (APlayerState* PS = PC->GetPlayerState<APlayerState>())
				{
					PlayerName = PS->GetPlayerName();
				}
			}

			// 添加到聊天列表
			AddChatMessage(PlayerName, Message);

			// TODO: 通过网络发送消息到服务器
			// Server_SendChatMessage(PlayerName, Message);
		}
	}
}

UUserWidget* UChatWidget::CreateChatMessageWidget(const FString& PlayerName, const FString& Message)
{
	UCanvasPanel* MessagePanel = NewObject<UCanvasPanel>(this);
	if (!MessagePanel)
	{
		return nullptr;
	}

	// 玩家名称
	UTextBlock* NameText = NewObject<UTextBlock>(MessagePanel);
	if (NameText)
	{
		NameText->SetText(FText::FromString(PlayerName + TEXT(": ")));
		NameText->SetColorAndOpacity(FLinearColor::Cyan);
		NameText->SetFont(FSlateFontInfo(FSlateFontInfo().FontObject, 12));
		MessagePanel->AddChild(NameText);
	}

	// 消息内容
	UTextBlock* MessageText = NewObject<UTextBlock>(MessagePanel);
	if (MessageText)
	{
		MessageText->SetText(FText::FromString(Message));
		MessageText->SetColorAndOpacity(FLinearColor::White);
		MessageText->SetFont(FSlateFontInfo(FSlateFontInfo().FontObject, 12));
		MessagePanel->AddChild(MessageText);
	}

	return MessagePanel;
}

void UChatWidget::ScrollToBottom()
{
	if (SB_ChatMessages)
	{
		SB_ChatMessages->ScrollToEnd();
	}
}