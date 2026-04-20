#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "ChatWidget.generated.h"

class UScrollBox;
class UEditableTextBox;
class UButton;

/**
 * 聊天组件
 * 负责显示聊天消息和输入聊天内容
 */
UCLASS()
class METALSLUG01_API UChatWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 设置输入框是否激活（激活时显示输入界面，回车发送）
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void SetInputFocused(bool bFocused);

	// 切换聊天输入框的激活状态（按 T 时调用）
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void ToggleChatInput();

	// 是否有输入焦点
	bool IsInputFocused() const { return bIsInputFocused; }

	// 当玩家提交聊天消息时广播（供外部接收以发送网络消息）
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChatMessageReady, const FString&, PlayerName, const FString&, Message);
	UPROPERTY(BlueprintAssignable, Category = "Chat")
	FOnChatMessageReady OnChatMessageReady;

public:
	// 添加聊天消息
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void AddChatMessage(const FString& PlayerName, const FString& Message);

	// 添加系统消息
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void AddSystemMessage(const FString& Message);

	// 清空聊天记录
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void ClearChat();

protected:
	virtual bool Initialize() override;

	// 发送按钮点击事件
	UFUNCTION()
	void OnSendClicked();

	// 输入框回车事件
	UFUNCTION()
	void OnChatInputCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	// Native键盘事件：捕获 T 键打开聊天，Escape 关闭聊天
	virtual FReply NativeOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	// 当焦点从输入框移开时，自动退出输入模式
	virtual void NativeOnFocusChanging(const FWeakWidgetPath& OldWidgetPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent) override;

private:
	// 最大显示的聊天消息数量
	static constexpr int32 MaxChatMessages = 50;

	// 当前是否处于输入焦点状态
	bool bIsInputFocused = false;

	// 聊天消息滚动容器
	UPROPERTY(meta = (BindWidget))
	UScrollBox* SB_ChatMessages;

	// 聊天输入框
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* ETB_ChatInput;

	// 发送按钮
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* Btn_Send;

	// 缓存的 Slate 输入框指针（用于焦点路径检查）
	TWeakPtr<SWidget> ChatInputSlateWidget;

	// 创建单条聊天消息
	UWidget* CreateChatMessageWidget(const FString& PlayerName, const FString& Message);

	// 滚动到底部
	UFUNCTION()
	void ScrollToBottom();
};