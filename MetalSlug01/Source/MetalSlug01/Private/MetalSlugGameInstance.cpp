// ==========================================
// 大厂标准：游戏实例实现
// ==========================================

// 引入本类头文件
#include "MetalSlugGameInstance.h"

// 引入 GameFlow 子系统 (用于触发 BootToLogin)
#include "Systems/GameFlowSubsystem.h"

// 引入统一日志通道
#include "Logs/MetalSlugLogChannels.h"

// ==========================================
// 生命周期
// ==========================================

void UMetalSlugGameInstance::Init()
{
	// ==========================================
	// 第一阶段: 让引擎完成所有 Subsystem 的自然初始化
	// ==========================================
	// 在调用 Super::Init() 之前，所有 GameInstanceSubsystem 都未初始化
	// Super::Init() 会触发它们按 Engine 内部顺序执行 Initialize()
	// 包括:
	//   - GameFlowSubsystem::Initialize()  (检查 DataTable)
	//   - UIViewService::Initialize()      (订阅 OnStateChanged + 预创建)
	//   - AccountSubsystem::Initialize()   (账号数据加载)
	//   - SessionManagerSubsystem::Initialize()
	//   - LANRoomPresenter::Initialize()
	Super::Init();

	UE_LOG(LogGameFlow, Log, TEXT("[GameInstance] 第一阶段完成：所有 Subsystem 已 Initialize"));

	// ==========================================
	// 第二阶段: 编排启动序列 (大厂核心模式)
	// ==========================================
	// 此时:
	// - 所有 Subsystem 已就绪
	// - UIViewService 已订阅 GameFlow::OnStateChanged
	// - UIViewService 已同步初始状态 (但拿到的是 PreLogin, 不显示任何面板)
	//
	// 我们主动触发 PreLogin → Login 转换:
	// - GameFlowSubsystem::BootToLogin() 内部调用 TransitToState(Login)
	// - TransitToState 广播 OnStateChanged(Login)
	// - UIViewService 收到事件 → 在 StateToPanelMap 中查找 Login 对应面板
	// - 调用 ShowPanel(Login) → 显示 LoginPage 蓝图
	//
	// 这就是大厂 Single Source of Truth: 一个事件驱动所有 UI

	if (UGameFlowSubsystem* FlowSubsystem = GetSubsystem<UGameFlowSubsystem>())
	{
		UE_LOG(LogGameFlow, Log, TEXT("[GameInstance] 第二阶段：触发 BootToLogin (PreLogin → Login)"));
		FlowSubsystem->BootToLogin();
	}
	else
	{
		UE_LOG(LogGameFlow, Error, TEXT("[GameInstance] 未找到 GameFlowSubsystem！无法启动游戏流程"));
	}

	UE_LOG(LogGameFlow, Log, TEXT("[GameInstance] 启动编排完成"));
}

void UMetalSlugGameInstance::Shutdown()
{
	// 关闭期: 先调用 Super (Subsystem 自然 Deinitialize)
	Super::Shutdown();

	// 自定义清理逻辑可加在这里
	UE_LOG(LogGameFlow, Log, TEXT("[GameInstance] 已关闭"));
}