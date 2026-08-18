// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// ChatWidget 头文件 — 战斗内聊天组件
// ==========================================
//
// 文件作用:
//   1. 声明 UChatWidget — 战斗内聊天 UI 组件 (玩家消息 + 系统消息)
//   2. 提供输入框 (UEditableTextBox) + 滚动容器 (UScrollBox) + 发送按钮
//   3. 维护 50 条消息上限自动滚动
//
// 设计理念 (大厂原则 - 输入模式 + 委托):
//   1. 输入模式: GameAndUI + WidgetToFocus 让玩家边玩边打字
//   2. 事件委托: OnChatMessageReady 供外部 (GameHUDWidget) 接收以转发网络
//   3. 风格解耦: 字体/颜色全部由蓝图配置 (EditAnywhere)
//   4. Slate 焦点: 缓存 ETB_ChatInput->GetCachedWidget() 用于焦点路径检查
//   5. 字体修复: 关键修复是支持中文的字体配置
//
// 大厂对应:
//   - Lyra: UCommonChatWidget
//   - 商业 FPS: 通用聊天 UI 模式
// ==========================================

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "ChatWidget.generated.h"

// 前向声明
class UScrollBox;
class UEditableTextBox;
class UButton;


/**
 * @class UChatWidget
 * @brief 聊天组件
 *
 * 职责说明:
 * - 显示聊天消息（玩家消息 + 系统消息）
 * - 提供输入框供玩家发送消息
 * - 维护 50 条消息上限自动滚动
 * - 支持 T 键激活输入、ESC 退出
 * - 焦点移出自动退出输入模式
 *
 * 架构理念:
 * 1. 输入模式: GameAndUI + WidgetToFocus 让玩家边玩边打字
 * 2. 事件委托: OnChatMessageReady 供外部接收以发送网络消息
 * 3. 风格解耦: 字体/颜色全部由蓝图配置
 * 4. Slate 焦点: 缓存 ETB_ChatInput->GetCachedWidget() 用于焦点路径检查
 * 5. 字体修复: 关键修复是支持中文的字体配置
 */
UCLASS()
class METALSLUG01_API UChatWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 公共接口
	// ==========================================

	/**
	 * 设置输入框是否激活
	 * @param bFocused true=激活（清空+显示+焦点+GameAndUI）, false=退出（清空+折叠+GameOnly）
	 */
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void SetInputFocused(bool bFocused);

	/**
	 * 切换聊天输入框的激活状态（按 T 时调用）
	 */
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void ToggleChatInput();

	/**
	 * 是否有输入焦点
	 */
	bool IsInputFocused() const { return bIsInputFocused; }

	/**
	 * 当玩家提交聊天消息时广播（供外部接收以发送网络消息）
	 * 委托签名: (PlayerName, Message)
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChatMessageReady, const FString&, PlayerName, const FString&, Message);
	UPROPERTY(BlueprintAssignable, Category = "Chat")
	FOnChatMessageReady OnChatMessageReady;

	/**
	 * 添加聊天消息
	 */
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void AddChatMessage(const FString& PlayerName, const FString& Message);

	/**
	 * 添加系统消息
	 */
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void AddSystemMessage(const FString& Message);

	/**
	 * 清空聊天记录
	 */
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void ClearChat();

protected:
	// ==========================================
	// 2. 生命周期
	// ==========================================

	virtual bool Initialize() override;

	// ==========================================
	// 3. 内部回调
	// ==========================================

	/**
	 * 发送按钮点击事件
	 */
	UFUNCTION()
	void OnSendClicked();

	/**
	 * 输入框回车事件
	 * 1. 校验 OnEnter
	 * 2. 从 PlayerState 拿玩家名
	 * 3. 广播 OnChatMessageReady
	 * 4. 退出输入模式
	 */
	UFUNCTION()
	void OnChatInputCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	/**
	 * Native 键盘事件: 捕获 Escape 键关闭聊天
	 */
	virtual FReply NativeOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/**
	 * 当焦点从输入框移开时, 自动退出输入模式
	 * 用途: 用 ChatInputSlateWidget 检查新焦点路径
	 */
	virtual void NativeOnFocusChanging(const FWeakWidgetPath& OldWidgetPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent) override;

	// ==========================================
	// 4. 风格配置
	// ==========================================

	/**
	 * 玩家名称和聊天内容的字体设置
	 * 关键: 必须支持中文
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat|Style")
	FSlateFontInfo ChatFont;

	/** 玩家名称的颜色（默认青色） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat|Style")
	FLinearColor PlayerNameColor = FLinearColor(0.0f, 1.0f, 1.0f, 1.0f);

	/** 聊天内容的颜色（默认白色） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat|Style")
	FLinearColor ChatMessageColor = FLinearColor::White;

	/** 系统消息的颜色（默认黄色） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat|Style")
	FLinearColor SystemMessageColor = FLinearColor::Yellow;

	/**
	 * 在编辑器中初始化默认字体
	 * 用途: 防止蓝图未配置时字体太小
	 */
	virtual void NativePreConstruct() override;

private:
	// ==========================================
	// 5. 私有成员
	// ==========================================

	/** 最大显示的聊天消息数量（超过则移除最早的） */
	static constexpr int32 MaxChatMessages = 50;

	/** 当前是否处于输入焦点状态 */
	bool bIsInputFocused = false;

	/** 聊天消息滚动容器 */
	UPROPERTY(meta = (BindWidget))
	UScrollBox* SB_ChatMessages;

	/** 聊天输入框 */
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* ETB_ChatInput;

	/** 发送按钮（可选） */
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* Btn_Send;

	/**
	 * 缓存的 Slate 输入框指针
	 * 用途: 焦点路径检查
	 */
	TWeakPtr<SWidget> ChatInputSlateWidget;

	// ==========================================
	// 6. 私有方法
	// ==========================================

	/**
	 * 创建单条聊天消息
	 * 返回: UOverlay 包含玩家名 + 内容（用 HorizontalBox 防止重叠）
	 */
	UWidget* CreateChatMessageWidget(const FString& PlayerName, const FString& Message);

	/**
	 * 滚动到底部
	 */
	UFUNCTION()
	void ScrollToBottom();
};
