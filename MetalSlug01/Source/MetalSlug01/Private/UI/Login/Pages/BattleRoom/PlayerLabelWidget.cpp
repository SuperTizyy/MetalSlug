#include "UI/Login/Pages/BattleRoom/PlayerLabelWidget.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Systems/RoomPlayerController.h"

bool UPlayerLabelWidget::Initialize()
{
	if (!Super::Initialize()) return false;

	// 绑定移除按钮的点击事件
	if (Btn_RemovePlayer)
	{
		Btn_RemovePlayer->OnClicked.AddDynamic(this, &UPlayerLabelWidget::OnRemoveButtonClicked);
	}
	
	// 【新增】：控件一生成，强制默认显示为“未准备”和灰色！
	SetReadyState(false);
	
	return true;
}

void UPlayerLabelWidget::SetPlayerName(const FString& InPlayerName)
{
	if (Text_PlayerName)
	{
		Text_PlayerName->SetText(FText::FromString(InPlayerName));
	}
}

FString UPlayerLabelWidget::GetPlayerName() const
{
	if (Text_PlayerName)
	{
		return Text_PlayerName->GetText().ToString();
	}
	return FString();
}

void UPlayerLabelWidget::SetRemoveButtonVisibility(bool bIsVisible)
{
	if (Btn_RemovePlayer)
	{
		// 如果可见，设为 Visible；如果不可见，设为 Collapsed (折叠，不占空间)或者 Hidden
		Btn_RemovePlayer->SetVisibility(bIsVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

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
		// 3. 呼叫对讲机，向服务器大脑开火！
		PC->Server_KickPlayer(PlayerNameToKick);
	}
}

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

void UPlayerLabelWidget::SetAsHost(bool bIsHost)
{
	if (bIsHost)
	{
		// 1. 拼接名字：账户名（房主）
		FString BaseName = GetPlayerName();
		// 防止重复拼接（比如刷新UI时再次调用）
		if (!BaseName.Contains(TEXT("（房主）")))
		{
			FString NewName = FString::Printf(TEXT("%s（房主）"), *BaseName);
			if (Text_PlayerName) Text_PlayerName->SetText(FText::FromString(NewName));
		}

		// 2. 房主不需要显示“准备/未准备”文本，直接折叠隐藏
		if (Text_IsReady)
		{
			Text_IsReady->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UPlayerLabelWidget::SetAsAI()
{
	// 1. AI 不需要准备状态，直接隐藏！
	if (Text_IsReady)
	{
		Text_IsReady->SetVisibility(ESlateVisibility::Collapsed);
	}
	// 2. （可选）如果你想，可以在这给 AI 的名字换个颜色区分一下
}