// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// EscMenuWidget 实现 — 对战 ESC 菜单面板
// ==========================================
//
// 文件作用:
//   1. 实现 UEscMenuWidget — ESC 菜单的按钮点击逻辑
//   2. Initialize 绑定两个按钮的点击事件
//   3. NativeConstruct 初始化防连点状态
//   4. OnResumeClicked / OnExitClicked 按钮回调
//   5. ResetExitButtonState 防连点机制
//   6. ResumeGame / QuitGame / ReturnToLobby / GetRoomPlayerController 辅助函数
//
// 关键架构:
//   - 状态机分离: Widget 只管 UI, Pause/InputMode 由 RoomPlayerController 管理
//   - 防连点: bIsExitProcessing + ExitCooldownTime (默认 1.0s)
//   - 防御性: PlayerController 拿不到时降级 QuitGame
// ==========================================

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Game/Widgets/EscMenuWidget.h"
#include "Systems/RoomPlayerController.h"

#include "Components/Button.h"

#include "Kismet/GameplayStatics.h"


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * UEscMenuWidget::Initialize
 *
 * 绑定"继续游戏"和"退出游戏"两个按钮的点击事件
 */
bool UEscMenuWidget::Initialize()
{
	// 调用父类的 Initialize
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


/**
 * UEscMenuWidget::NativeConstruct
 *
 * 1. 初始化 bIsExitProcessing = false
 * 2. ExitCooldownTime 默认 1.0s
 */
void UEscMenuWidget::NativeConstruct()
{
	// 调用父类的 NativeConstruct
	Super::NativeConstruct();

	// 初始化防连点状态
	bIsExitProcessing = false;

	// 默认防连点时间（可通过蓝图调整）
	if (ExitCooldownTime <= 0.f)
	{
		ExitCooldownTime = 1.0f;
	}
}


// ==========================================
// 2. 按钮点击
// ==========================================

/**
 * UEscMenuWidget::OnResumeClicked
 *
 * "继续游戏"按钮点击
 * 通知 RoomPlayerController->HideEscMenu()
 * GameHUD 通过 SetVisibility(Hidden) 来隐藏
 */
void UEscMenuWidget::OnResumeClicked()
{
	// "继续游戏"按钮点击: 通知 GameHUD 隐藏 ESC 菜单
	// GameHUD 通过 SetVisibility(Hidden) 来隐藏
	// 由 GameHUD 中的输入绑定回调处理
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ARoomPlayerController* RoomPC = Cast<ARoomPlayerController>(PC))
		{
			RoomPC->HideEscMenu();
		}
	}
}


/**
 * UEscMenuWidget::OnExitClicked
 *
 * "退出游戏"按钮点击
 * 1. 【防连点检查】bIsExitProcessing 直接返回
 * 2. 标记 + 禁用按钮（视觉反馈）
 * 3. 统一由 RoomPlayerController 管理
 *    - HideEscMenu(): 同步 GamePause + InputMode
 *    - LeaveRoom(): 自动返回大厅
 * 4. 拿不到 PC 时降级 QuitGame
 */
void UEscMenuWidget::OnExitClicked()
{
	// ==========================================
	// 【防连点检查】: 如果正在处理退出逻辑，直接返回
	// ==========================================
	if (bIsExitProcessing)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EscMenuWidget] Button_Exit 正在处理中，忽略重复点击"));
		return;
	}

	// ==========================================
	// 【防连点机制】: 标记为正在处理
	// ==========================================
	bIsExitProcessing = true;

	// 立即禁用按钮，提供视觉反馈，防止用户再次点击
	if (Button_Exit)
	{
		Button_Exit->SetIsEnabled(false);
	}

	// ==========================================
	// 【架构修正】: 不再直接操作自身可见性
	// 统一由 RoomPlayerController 的 HideEscMenu() 管理,
	// 这样 GameHUDWidget、InputMode、GamePause 状态才能完整同步
	// ==========================================

	// 获取当前 PlayerController
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

	// 调用 PlayerController 的离开房间接口（会自动返回大厅）
	PC->LeaveRoom();
}


/**
 * UEscMenuWidget::ResetExitButtonState
 *
 * 重置退出按钮状态（供外部调用, 防止防连点误判）
 */
void UEscMenuWidget::ResetExitButtonState()
{
	// 重置退出按钮状态，用于防连点机制
	bIsExitProcessing = false;
}


/**
 * UEscMenuWidget::ResumeGame
 *
 * 恢复游戏逻辑（供 GameHUD 调用）
 * 显示/隐藏由 GameHUD 通过 SetVisibility 控制
 */
void UEscMenuWidget::ResumeGame()
{
	// 恢复游戏逻辑（供 GameHUD 调用）
	// 显示/隐藏由 GameHUD 通过 SetVisibility 控制
}


// ==========================================
// 3. 辅助
// ==========================================

/**
 * UEscMenuWidget::GetRoomPlayerController
 *
 * 获取拥有此 Widget 的 PlayerController
 * 已 Cast 为 ARoomPlayerController
 */
ARoomPlayerController* UEscMenuWidget::GetRoomPlayerController() const
{
	// 获取拥有此 Widget 的 PlayerController
	if (APlayerController* PC = GetOwningPlayer())
	{
		return Cast<ARoomPlayerController>(PC);
	}
	return nullptr;
}


/**
 * UEscMenuWidget::QuitGame
 *
 * 完全退出游戏（降级方案）
 * 通过 PC->ConsoleCommand("quit") 调用 UE 内置退出
 */
void UEscMenuWidget::QuitGame()
{
	// 获取 PlayerController
	if (APlayerController* PC = GetOwningPlayer())
	{
		// 调用 UE 内置的退出游戏功能
		PC->ConsoleCommand(TEXT("quit"), true);
	}
}
