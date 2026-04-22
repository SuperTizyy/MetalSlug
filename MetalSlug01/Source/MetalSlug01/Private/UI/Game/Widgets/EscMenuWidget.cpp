#include "UI/Game/Widgets/EscMenuWidget.h"
#include "Systems/RoomPlayerController.h"

#include "Components/Button.h"

#include "Kismet/GameplayStatics.h"

bool UEscMenuWidget::Initialize()
{
	// 调用父类的Initialize
	if (!Super::Initialize())
	{
		return false;
	}

	// 绑定"继续游戏"按钮的点击事件
	if (Button_Resume)
	{
		Button_Resume->OnClicked.AddDynamic(this, &UEscMenuWidget::OnResumeClicked);
	}

	// 绑定"退出游戏"按钮的点击事件
	if (Button_Exit)
	{
		Button_Exit->OnClicked.AddDynamic(this, &UEscMenuWidget::OnExitClicked);
	}

	return true;
}

void UEscMenuWidget::NativeConstruct()
{
	// 调用父类的NativeConstruct
	Super::NativeConstruct();

	// 初始化防连点状态
	bIsExitProcessing = false;

	// 默认防连点时间（可通过蓝图调整）
	if (ExitCooldownTime <= 0.f)
	{
		ExitCooldownTime = 1.0f;
	}
}

void UEscMenuWidget::OnResumeClicked()
{
	// "继续游戏"按钮点击：通知GameHUD隐藏ESC菜单
	// GameHUD通过SetVisibility(Hidden)来隐藏
	// 由GameHUD中的输入绑定回调处理
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ARoomPlayerController* RoomPC = Cast<ARoomPlayerController>(PC))
		{
			RoomPC->HideEscMenu();
		}
	}
}

void UEscMenuWidget::OnExitClicked()
{
	// ==========================================
	// 【防连点检查】：如果正在处理退出逻辑，直接返回
	// ==========================================
	if (bIsExitProcessing)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EscMenuWidget] Button_Exit 正在处理中，忽略重复点击"));
		return;
	}

	// ==========================================
	// 【防连点机制】：标记为正在处理
	// ==========================================
	bIsExitProcessing = true;

	// 立即禁用按钮，提供视觉反馈，防止用户再次点击
	if (Button_Exit)
	{
		Button_Exit->SetIsEnabled(false);
	}

	// ==========================================
	// 【架构修正】：不再直接操作自身可见性！
	// 统一由 RoomPlayerController 的 HideEscMenu() 管理，
	// 这样 GameHUDWidget、InputMode、GamePause 状态才能完整同步。
	// ==========================================

	// 获取当前PlayerController
	ARoomPlayerController* PC = GetRoomPlayerController();
	if (!PC)
	{
		// 如果获取失败，直接退出游戏
		UE_LOG(LogTemp, Warning, TEXT("[EscMenuWidget] Failed to get PlayerController, quitting game..."));
		QuitGame();
		return;
	}

	// 先通过 Controller 隐藏 ESC 菜单（同步 GamePause + InputMode）
	PC->HideEscMenu();

	// 调用PlayerController的离开房间接口（会自动返回大厅）
	PC->LeaveRoom();
}

void UEscMenuWidget::ResetExitButtonState()
{
	// 重置退出按钮状态，用于防连点机制
	bIsExitProcessing = false;
}

void UEscMenuWidget::ResumeGame()
{
	// 恢复游戏逻辑（供GameHUD调用）
	// 显示/隐藏由GameHUD通过SetVisibility控制
}

ARoomPlayerController* UEscMenuWidget::GetRoomPlayerController() const
{
	// 获取拥有此Widget的PlayerController
	if (APlayerController* PC = GetOwningPlayer())
	{
		return Cast<ARoomPlayerController>(PC);
	}
	return nullptr;
}

void UEscMenuWidget::QuitGame()
{
	// 获取PlayerController
	if (APlayerController* PC = GetOwningPlayer())
	{
		// 调用UE内置的退出游戏功能
		PC->ConsoleCommand(TEXT("quit"), true);
	}
}
