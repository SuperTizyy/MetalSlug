// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// ChatWidget 实现 — 战斗内聊天组件
// ==========================================
//
// 文件作用:
//   1. 实现 UChatWidget — 聊天 UI 的所有交互逻辑
//   2. 输入框激活/退出 (GameAndUI 模式切换)
//   3. 消息管理 (AddChatMessage/AddSystemMessage/ClearChat)
//   4. 输入回调 (回车提交 + 发送按钮 + ESC 关闭)
//   5. 键盘事件 + 焦点事件处理
//   6. 消息 Widget 创建 (HorizontalBox 排列防重叠)
//   7. 预构造 (NativePreConstruct 设置字体默认值)
//
// 关键设计:
//   - 输入模式切换: GameOnly (战斗中) ↔ GameAndUI (聊天时)
//   - 字体修复: 应用蓝图中配置的支持中文的 ChatFont
//   - 焦点路径检查: NativeOnFocusChanging 检测焦点是否移出输入框
// ==========================================

// ==========================================
// 头文件包含区
// ==========================================
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


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * UChatWidget::Initialize
 *
 * 1. 绑定发送按钮点击
 * 2. 绑定输入框回车事件
 */
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


// ==========================================
// 2. 输入框控制
// ==========================================

/**
 * UChatWidget::SetInputFocused
 *
 * 激活输入框:
 * 1. 清空内容 + 显示
 * 2. 切 GameAndUI 模式 + WidgetToFocus
 * 3. SetKeyboardFocus
 * 4. 缓存 Slate 指针用于焦点检查
 * 显示鼠标
 *
 * 退出输入框:
 * 1. 清空 + 折叠
 * 2. 切 GameOnly 模式
 * 3. 隐藏鼠标
 */
void UChatWidget::SetInputFocused(bool bFocused)
{
	bIsInputFocused = bFocused;

	if (ETB_ChatInput)
	{
		if (bFocused)
		{
			// 激活输入框: 清空内容 + 设置可见性
			ETB_ChatInput->SetText(FText::GetEmpty());
			ETB_ChatInput->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

			// 切为 GameAndUI，并将输入法强制锁定到该输入框
			if (APlayerController* PC = GetOwningPlayer())
			{
				FInputModeGameAndUI InputMode;
				InputMode.SetWidgetToFocus(ETB_ChatInput->TakeWidget());
				InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
				PC->SetInputMode(InputMode);
				PC->bShowMouseCursor = true;
			}

			// 设置输入焦点
			ETB_ChatInput->SetKeyboardFocus();

			// 缓存 Slate 指针用于焦点路径检查
			ChatInputSlateWidget = ETB_ChatInput->GetCachedWidget();
		}
		else
		{
			// 隐藏输入框并清空内容
			ETB_ChatInput->SetText(FText::GetEmpty());
			ETB_ChatInput->SetVisibility(ESlateVisibility::Collapsed);

			// 退出时，确保交还给游戏
			if (APlayerController* PC = GetOwningPlayer())
			{
				PC->SetInputMode(FInputModeGameOnly());
				PC->bShowMouseCursor = false;
			}
		}
	}
}


/**
 * UChatWidget::ToggleChatInput
 *
 * 切换输入状态（按 T 时调用）
 */
void UChatWidget::ToggleChatInput()
{
	SetInputFocused(!bIsInputFocused);
}


// ==========================================
// 3. 消息管理
// ==========================================

/**
 * UChatWidget::AddChatMessage
 *
 * 1. 创建聊天消息 Widget（UOverlay + HorizontalBox 排版）
 * 2. AddChild 到 SB_ChatMessages
 * 3. 超过 50 条时移除最早的
 * 4. 滚动到底部
 */
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


/**
 * UChatWidget::AddSystemMessage
 *
 * 1. 创建系统消息 UTextBlock
 * 2. 应用 SystemMessageColor（黄）+ ChatFont
 * 3. 添加并限制 50 条 + ScrollToEnd
 */
void UChatWidget::AddSystemMessage(const FString& Message)
{
	if (!SB_ChatMessages) return;

	UTextBlock* SystemText = NewObject<UTextBlock>(this);
	if (SystemText)
	{
		// 玩家输入的通常用 FromString，AsCultureInvariant 一般用于数字或非本地化 ID
		SystemText->SetText(FText::FromString(Message));
		SystemText->SetColorAndOpacity(SystemMessageColor);

		// 【核心修复】: 应用蓝图中配置的支持中文的字体
		SystemText->SetFont(ChatFont);

		SB_ChatMessages->AddChild(SystemText);

		while (SB_ChatMessages->GetChildrenCount() > MaxChatMessages)
		{
			SB_ChatMessages->RemoveChildAt(0);
		}
		ScrollToBottom();
	}
}


/**
 * UChatWidget::ClearChat
 *
 * 清空所有聊天记录
 */
void UChatWidget::ClearChat()
{
	if (SB_ChatMessages)
	{
		SB_ChatMessages->ClearChildren();
	}
}


// ==========================================
// 4. 输入回调
// ==========================================

/**
 * UChatWidget::OnSendClicked
 *
 * 发送按钮点击
 * 1. 获取输入框文本
 * 2. 非空时调用 OnChatInputCommitted 模拟回车
 */
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


/**
 * UChatWidget::OnChatInputCommitted
 *
 * 输入框回车事件
 * 1. 校验 OnEnter
 * 2. 从 PlayerState 拿玩家名（建议生产环境做空指针安全检查）
 * 3. 广播 OnChatMessageReady
 * 4. 发送完毕后退出输入模式
 */
void UChatWidget::OnChatInputCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	// 确保是回车键提交
	if (CommitMethod == ETextCommit::OnEnter)
	{
		FString Message = Text.ToString();
		if (!Message.IsEmpty())
		{
			// 从 PlayerState 获取玩家名称（建议在生产环境做空指针安全检查）
			FString PlayerName = TEXT("Player");
			if (APlayerController* PC = GetOwningPlayer())
			{
				if (APlayerState* PS = PC->GetPlayerState<APlayerState>())
				{
					PlayerName = PS->GetPlayerName();
				}
			}

			// 广播消息给业务逻辑层
			OnChatMessageReady.Broadcast(PlayerName, Message);

		}

		// 发送完毕后，直接调用统一接口退出输入模式
		SetInputFocused(false);
	}
}


// ==========================================
// 5. 键盘 / 焦点事件
// ==========================================

/**
 * UChatWidget::NativeOnKeyDown
 *
 * 捕获 Escape 键关闭聊天（仅在输入状态）
 * 注意: 激活聊天的逻辑已剥离到 PlayerController 中
 */
FReply UChatWidget::NativeOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	// 仅处理在输入状态下，按 Escape 退出聊天的逻辑
	// 注意: 激活聊天的逻辑已经剥离到 PlayerController 中
	if (Key == EKeys::Escape && bIsInputFocused)
	{
		SetInputFocused(false);
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(MyGeometry, InKeyEvent);
}


/**
 * UChatWidget::NativeOnFocusChanging
 *
 * 当焦点从输入框移开时, 自动退出输入模式
 * 1. 检查新焦点路径是否包含输入框（不包含则关闭）
 * 2. 退出输入模式 + 切 GameOnly
 */
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


// ==========================================
// 6. 消息创建辅助
// ==========================================

/**
 * UChatWidget::CreateChatMessageWidget
 *
 * 创建单条聊天消息
 * 结构: UOverlay 包含一个 UHorizontalBox（玩家名 + 内容）
 * 优化: 用 HBox 排列防止文字重叠
 * 关键修复: 应用 ChatFont 支持中文
 */
UWidget* UChatWidget::CreateChatMessageWidget(const FString& PlayerName, const FString& Message)
{
	UOverlay* MessageContainer = NewObject<UOverlay>(this);
	if (!MessageContainer) return nullptr;

	// 玩家名称
	UTextBlock* NameText = NewObject<UTextBlock>(MessageContainer);
	if (NameText)
	{
		NameText->SetText(FText::FromString(FString::Printf(TEXT("%s: "), *PlayerName)));
		NameText->SetColorAndOpacity(PlayerNameColor);
		// 【核心修复】: 应用蓝图中配置的支持中文的字体
		NameText->SetFont(ChatFont);
		MessageContainer->AddChild(NameText);
	}

	// 消息内容（你原本的代码可能会让玩家名和消息内容重叠，因为都加到了 Overlay 且没有设置 Padding）
	// 这里顺手用 HorizontalBox 帮你优化一下排列，防止文字重叠
	UHorizontalBox* HBox = NewObject<UHorizontalBox>(MessageContainer);
	if (HBox)
	{
		HBox->AddChild(NameText);

		UTextBlock* MessageText = NewObject<UTextBlock>(HBox);
		if (MessageText)
		{
			MessageText->SetText(FText::FromString(Message));
			MessageText->SetColorAndOpacity(ChatMessageColor);
			// 【核心修复】: 应用蓝图中配置的支持中文的字体
			MessageText->SetFont(ChatFont);
			HBox->AddChild(MessageText);
		}

		MessageContainer->AddChild(HBox);
	}

	return MessageContainer;
}


/**
 * UChatWidget::ScrollToBottom
 *
 * 滚动到底部
 */
void UChatWidget::ScrollToBottom()
{
	if (SB_ChatMessages)
	{
		SB_ChatMessages->ScrollToEnd();
	}
}


// ==========================================
// 7. 预构造
// ==========================================

/**
 * UChatWidget::NativePreConstruct
 *
 * 在蓝图编译或初始化前给安全的默认值
 * 设置 ChatFont.Size = 14 防止蓝图未配置时字体太小
 */
void UChatWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

	// 设置字体的默认大小，防止蓝图中未配置时字体太小看不见
	if (ChatFont.Size == 0)
	{
		ChatFont.Size = 14;
	}
}
