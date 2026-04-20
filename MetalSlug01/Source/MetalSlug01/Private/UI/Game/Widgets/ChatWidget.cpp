#include "UI/Game/Widgets/ChatWidget.h"
#include "Components/ScrollBox.h"
#include "Components/EditableTextBox.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/Widget.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
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

			// 缓存 Slate 指针用于焦点路径检查
			TSharedPtr<SWidget> SlateWidget = ETB_ChatInput->GetCachedWidget();
			ChatInputSlateWidget = SlateWidget;
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
		SystemText->SetText(FText::AsCultureInvariant(Message));
		SystemText->SetColorAndOpacity(FLinearColor::Yellow);
		SystemText->SetFont(FSlateFontInfo(FSlateFontInfo().FontObject, 14));

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

		// 发送完毕后：清空输入框、退出输入模式、恢复游戏输入
		if (ETB_ChatInput)
		{
			ETB_ChatInput->SetText(FText::GetEmpty());
		}
		SetInputFocused(false);
		if (APlayerController* PC = GetOwningPlayer())
		{
			PC->SetInputMode(FInputModeGameOnly());
			PC->bShowMouseCursor = false;
		}
	}
}

FReply UChatWidget::NativeOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	// T 键：切换聊天输入框
	if (Key == EKeys::T)
	{
		if (!bIsInputFocused)
		{
			SetInputFocused(true);
			// 切换到 GameAndUI 模式，让游戏和 UI 同时接收输入
			if (APlayerController* PC = GetOwningPlayer())
			{
				PC->SetInputMode(FInputModeGameAndUI());
				PC->bShowMouseCursor = true;
			}
		}
		return FReply::Handled();
	}

	// Escape 键：关闭聊天输入
	if (Key == EKeys::Escape)
	{
		if (bIsInputFocused)
		{
			SetInputFocused(false);
			if (APlayerController* PC = GetOwningPlayer())
			{
				PC->SetInputMode(FInputModeGameOnly());
				PC->bShowMouseCursor = false;
			}
		}
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(MyGeometry, InKeyEvent);
}

void UChatWidget::NativeOnFocusChanging(const FWeakWidgetPath& OldWidgetPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent)
{
	// 如果输入框正在激活状态，且焦点要移出输入框，则退出输入模式
	if (bIsInputFocused && IsValid(ETB_ChatInput))
	{
		// 检查新的焦点路径中是否包含输入框
		if (ChatInputSlateWidget.IsValid() && !NewWidgetPath.ContainsWidget(ChatInputSlateWidget.Pin().Get()))
		{
			SetInputFocused(false);
			if (APlayerController* PC = GetOwningPlayer())
			{
				PC->SetInputMode(FInputModeGameOnly());
				PC->bShowMouseCursor = false;
			}
		}
	}

	Super::NativeOnFocusChanging(OldWidgetPath, NewWidgetPath, InFocusEvent);
}

UWidget* UChatWidget::CreateChatMessageWidget(const FString& PlayerName, const FString& Message)
{
	UOverlay* MessageContainer = NewObject<UOverlay>(this);
	if (!MessageContainer)
	{
		return nullptr;
	}

	FSlateFontInfo Font = FSlateFontInfo(FSlateFontInfo().FontObject, 14);

	// 玩家名称
	UTextBlock* NameText = NewObject<UTextBlock>(MessageContainer);
	if (NameText)
	{
		NameText->SetText(FText::AsCultureInvariant(FString::Printf(TEXT("%s: "), *PlayerName)));
		NameText->SetColorAndOpacity(FLinearColor(0.0f, 1.0f, 1.0f, 1.0f)); // Cyan
		NameText->SetFont(Font);
		MessageContainer->AddChild(NameText);
	}

	// 消息内容
	UTextBlock* MessageText = NewObject<UTextBlock>(MessageContainer);
	if (MessageText)
	{
		MessageText->SetText(FText::AsCultureInvariant(Message));
		MessageText->SetColorAndOpacity(FLinearColor::White);
		MessageText->SetFont(Font);
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