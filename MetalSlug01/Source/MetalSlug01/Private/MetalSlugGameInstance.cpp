// ==========================================
// 大厂标准：游戏实例实现 (MetalSlug 自定义 GameInstance)
// ==========================================
//
// 【大厂架构 - Lyra CommonGame 模式】
//   本类作为全局启动编排器 — 在 Init() 钩子里编排所有 Subsystem 启动顺序
//   这是 Lyra / Riot / EA 等顶级工作室的标准做法
//
// 【职责链】
//   第一阶段 (Super::Init): 让引擎完成所有 GameInstanceSubsystem 的自然初始化
//     - GameFlowSubsystem::Initialize()
//     - UIViewService::Initialize()
//     - AccountSubsystem::Initialize()
//     - SessionManagerSubsystem::Initialize()
//     - LANRoomPresenter::Initialize()
//   第二阶段 (本类编排): 仅打印日志，不再主动 Boot (已迁移至 PostLoadMapWithWorld)
//     - 旧版在此处主动调 BootToLogin → 导致 PIE 切图时 UI 闪烁
//     - 新版让 PostLoadMapWithWorld 统一调度 → 时序保证 World 已加载
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

/**
 * Init — UGameInstance 启动钩子
 *
 * @brief  所有 UGameInstanceSubsystem 已完成 Initialize() 后由引擎回调
 * @note   启动时序 (大厂 P0 修复 2026.07.03):
 *         1. Super::Init() 让所有 Subsystem 自然 Initialize
 *         2. 打印阶段日志
 *         3. 不再主动 BootToLogin — 改由 PostLoadMapWithWorld 统一调度
 * @note   主动 Boot 的反模式 (已废除):
 *         - GameInstance 调 BootToLogin → PIE 入口是战斗地图时 OpenLevel 切图
 *         - 新 World 也走 PostLoadMapWithWorld → 冗余触发 + UI 闪烁
 *         - 大厂方案: 单点编排 = PostLoadMapWithWorld (避免重复)
 */
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

/**
 * Shutdown — UGameInstance 关闭钩子
 *
 * @brief  游戏退出/PIE 结束时由引擎回调 — 清理自定义资源
 * @note   Super::Shutdown() 会让所有 Subsystem 自然 Deinitialize (顺序与 Init 相反)
 */
void UMetalSlugGameInstance::Shutdown()
{
	// 关闭期: 先调用 Super (Subsystem 自然 Deinitialize)
	Super::Shutdown();

	// 自定义清理逻辑可加在这里
	UE_LOG(LogGameFlow, Log, TEXT("[GameInstance] 已关闭"));
}