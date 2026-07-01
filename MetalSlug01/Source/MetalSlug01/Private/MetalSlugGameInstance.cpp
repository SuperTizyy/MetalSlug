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
	// 第二阶段: 大厂编排层 (大厂 P0 修复 2026.07.03)
	// ==========================================
	// 重大修复:
	//   旧方案在此处主动调 GameFlowSubsystem::BootToLogin()
	//   但 Subsystem::Initialize 内部已经把 BootToLogin 删除 (时机问题)
	//   现在改由 PostLoadMapWithWorld 统一调度 (时序保证 World 已加载)
	//
	// 如果在这里调 BootToLogin, 会触发 TransitToState(Login) →
	//   HandleStateEntry(Login) 会 OpenLevel("L_Login") (若当前不在 Login 地图)
	//   → 打开登录地图, 玩家看到登录页
	//
	// 但 PIE 入口是战斗地图时 (Japanese_Temple_Demo), OpenLevel 会切图
	//   → 新 World 也会走 PostLoadMapWithWorld → BootToLogin
	//   → 这是冗余触发, 且可能在切图过程中 UI 闪烁
	//
	// 大厂方案: 让 PostLoadMapWithWorld (单一入口) 负责启动
	//          GameInstance 不再主动 Boot (避免重复)
	// ==========================================
	UE_LOG(LogGameFlow, Log,
		TEXT("[GameInstance] 第二阶段：状态启动已统一迁移至 PostLoadMapWithWorld (大厂 P0 修复 2026.07.03)"));

	UE_LOG(LogGameFlow, Log, TEXT("[GameInstance] 启动编排完成"));
}

void UMetalSlugGameInstance::Shutdown()
{
	// 关闭期: 先调用 Super (Subsystem 自然 Deinitialize)
	Super::Shutdown();

	// 自定义清理逻辑可加在这里
	UE_LOG(LogGameFlow, Log, TEXT("[GameInstance] 已关闭"));
}