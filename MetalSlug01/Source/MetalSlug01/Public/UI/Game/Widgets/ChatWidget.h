#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
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

private:
	// 最大显示的聊天消息数量
	static constexpr int32 MaxChatMessages = 50;

	// 聊天消息滚动容器
	UPROPERTY(meta = (BindWidget))
	UScrollBox* SB_ChatMessages;

	// 聊天输入框
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* ETB_ChatInput;

	// 发送按钮
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* Btn_Send;

	// 创建单条聊天消息
	UUserWidget* CreateChatMessageWidget(const FString& PlayerName, const FString& Message);

	// 滚动到底部
	UFUNCTION()
	void ScrollToBottom();
};