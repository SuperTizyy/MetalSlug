// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本控制器头文件
#include "Systems/LoginPlayerController.h"

// ==========================================
// 【P0 架构清理 2026.06.28】
//   移除以下不再使用的头文件:
//   - GameFlowSubsystem.h       (PC 不再主动查询/切换状态)
//   - Blueprint/UserWidget.h    (PC 不再创建 Widget)
//   - MetalSlugTestSettings.h   (开发测试模式移交给 UIViewService 或独立测试入口)
//   - AccountSubsystem.h        (MockLogin 改由 UIViewService/账号服务编排)
//   - UIViewService.h           (PC 不再主动 ShowPanel, 由 UIViewService 自己响应 OnStateChanged)
// ==========================================


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * ALoginPlayerController::BeginPlay
 *
 * 登录地图控制器的初始化入口
 * ==========================================
 * 【大厂架构 - P0 修复 2026.06.28】
 * ==========================================
 * 职责划分:
 *   - PC: 只做"PC 自身硬件配置" (鼠标 / 输入模式)
 *   - UIViewService: 统一监听 GameFlow.OnStateChanged, 自动 ShowPanel
 *   - GameFlowSubsystem: 状态机编排 (包括 PostLoadMapWithWorld → MainLobby 主动覆盖)
 *
 * 关键教训: 之前版本中, LoginPC 在 BeginPlay 里延迟 0.2s 主动 ShowPanel,
 *   导致状态切到 MainLobby 后又被旧值捕获的 Login 状态覆盖回 LoginPage。
 *   这是典型的"职责越界 + 时序竞争"反模式。
 *
 * 现在: BeginPlay 仅做硬件初始化, 不参与 UI 拉起编排。
 */
void ALoginPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 强制显示鼠标，设置输入模式为仅 UI
	bShowMouseCursor = true;
	SetInputMode(FInputModeUIOnly());

	// ==========================================
	// 【架构清理 - P0】删除 LoginPC 的 0.2s 延迟主动 ShowPanel
	// ==========================================
	// 原因:
	//   1. UIViewService 已订阅 GameFlow::OnStateChanged, 会自动 ShowPanel
	//   2. LoginPC 的延迟 ShowPanel 会"二次覆盖" UIViewService 的自动拉起
	//   3. 值捕获的旧状态会导致 PostLoadMapWithWorld 的新状态被反转覆盖
	//      (例如: 退房回 L_Login 时, PostLoadMapWithWorld 切到 MainLobby,
	//       但 LoginPC 的 0.2s 延迟回调在 BeginPlay 时已捕获 Login 状态,
	//       200ms 后会 ShowPanel(Login), 把 LANRoomPage 覆盖回 LoginPage)
	//
	// 之前: 9.54.43 的修复之后用户报告"还是老样子没解决" → 根因就是这里
	//
	// 现在: LoginPC 完全不参与 UI 编排, 只配置 PC 硬件 (鼠标/输入)
	// ==========================================

	if (!GetWorld()->GetMapName().Contains(TEXT("L_Login")))
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Red,
			FString::Printf(TEXT("[LoginPC Debug] 不在 L_Login 地图中，跳过 PC 硬件初始化")));
		return;
	}
}

/**
 * ALoginPlayerController::EndPlay
 * 销毁期: 仅保留 Super 调用 (无订阅需要解绑)
 */
void ALoginPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}
