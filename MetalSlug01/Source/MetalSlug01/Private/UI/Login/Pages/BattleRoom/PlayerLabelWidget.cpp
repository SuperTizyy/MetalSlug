// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Login/Pages/BattleRoom/PlayerLabelWidget.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Systems/RoomPlayerController.h"


// ==========================================
// 1. 初始化
// ==========================================

/**
 * UPlayerLabelWidget::Initialize
 *
 * 1. 绑定移除按钮的点击事件
 * 2. 默认显示"未准备"和灰色
 */
bool UPlayerLabelWidget::Initialize()
{
	if (!Super::Initialize()) return false;

	// 绑定移除按钮的点击事件
	if (Btn_RemovePlayer)
	{
		Btn_RemovePlayer->OnClicked.AddDynamic(this, &UPlayerLabelWidget::OnRemoveButtonClicked);
	}

	// 【新增】: 控件一生成，强制默认显示为"未准备"和灰色
	SetReadyState(false);

	return true;
}


// ==========================================
// 2. 公共接口
// ==========================================

/**
 * SetPlayerName
 *
 * 设置 Text_PlayerName 文本
 */
void UPlayerLabelWidget::SetPlayerName(const FString& InPlayerName)
{
	if (Text_PlayerName)
	{
		Text_PlayerName->SetText(FText::FromString(InPlayerName));
	}
}


/**
 * GetPlayerName
 *
 * @return 当前条目显示的玩家名（若 Text_PlayerName 为空返回空串）
 */
FString UPlayerLabelWidget::GetPlayerName() const
{
	if (Text_PlayerName)
	{
		return Text_PlayerName->GetText().ToString();
	}
	return FString();
}


/**
 * SetRemoveButtonVisibility
 *
 * @param bIsVisible true=显示，false=折叠
 */
void UPlayerLabelWidget::SetRemoveButtonVisibility(bool bIsVisible)
{
	if (Btn_RemovePlayer)
	{
		// 如果可见，设为 Visible；如果不可见，设为 Collapsed (折叠，不占空间)或者 Hidden
		Btn_RemovePlayer->SetVisibility(bIsVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}


// ==========================================
// 3. 踢人事件
// ==========================================

/**
 * OnRemoveButtonClicked
 *
 * 1. 拿到当前这个标签代表的倒霉蛋的名字
 * 2. 调用 PC->Server_KickPlayer
 */
void UPlayerLabelWidget::OnRemoveButtonClicked()
{
	// 1. 拿到当前这个标签代表的倒霉蛋的名字
	FString PlayerNameToKick = GetPlayerName();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, FString::Printf(TEXT("房主下达逐客令，踢出玩家: %s"), *PlayerNameToKick));
	}

	// 2. 获取看这块屏幕的玩家自己的控制器（绝对是房主，因为只有房主能看到这按钮）
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
	{
		// 3. 呼叫对讲机，向服务器大脑开火
		PC->Server_KickPlayer(PlayerNameToKick);
	}
}


// ==========================================
// 4. 状态机
// ==========================================

/**
 * SetReadyState
 *
 * 设置准备状态
 * @param bIsReady true=绿色"已准备", false=灰色"未准备"
 */
void UPlayerLabelWidget::SetReadyState(bool bIsReady)
{
	if (Text_IsReady)
	{
		if (bIsReady)
		{
			Text_IsReady->SetText(FText::FromString(TEXT("已准备")));
			Text_IsReady->SetColorAndOpacity(FSlateColor(FLinearColor::Green)); // 绿色显眼
		}
		else
		{
			Text_IsReady->SetText(FText::FromString(TEXT("未准备")));
			Text_IsReady->SetColorAndOpacity(FSlateColor(FLinearColor::Gray)); // 灰色低调
		}
	}
}


/**
 * SetAsHost
 *
 * 1. 玩家名后追加"（房主）"后缀（防重复）
 * 2. 隐藏准备文本（房主无需准备）
 * 3. 绝对防御: 强制隐藏踢人按钮（防自踢）
 */
void UPlayerLabelWidget::SetAsHost(bool bIsHost)
{
	if (bIsHost)
	{
		// 1. 拼接房主专属后缀
		FString BaseName = GetPlayerName();
		// 防重复拼接判定
		if (!BaseName.Contains(TEXT("（房主）")))
		{
			FString NewName = FString::Printf(TEXT("%s（房主）"), *BaseName);
			if (Text_PlayerName)
			{
				Text_PlayerName->SetText(FText::FromString(NewName));
			}
		}

		// ==========================================
		// 2. 【彻底清理多余状态】
		// 房主自带"随时发车"属性，不需要显示准备状态，直接将其折叠 (Collapsed)
		// ==========================================
		if (Text_IsReady)
		{
			Text_IsReady->SetVisibility(ESlateVisibility::Collapsed);
		}

		// ==========================================
		// 3. 【绝对防御机制】
		// 就算外部的权限控制失效，内部的 SetAsHost 也必须保证：
		// 只要你是房主，你的组件里就绝对不应该出现踢自己的按钮
		// ==========================================
		if (Btn_RemovePlayer)
		{
			Btn_RemovePlayer->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}


/**
 * SetAsAI
 *
 * AI 玩家: 隐藏准备文本
 * 可选: 给 AI 名字换个颜色区分（已注释）
 */
void UPlayerLabelWidget::SetAsAI()
{
	// 1. AI 不需要准备状态，直接隐藏
	if (Text_IsReady)
	{
		Text_IsReady->SetVisibility(ESlateVisibility::Collapsed);
	}
	// 2. （可选）如果你想，可以在这给 AI 的名字换个颜色区分一下
}
