// ==========================================
// 大厂标准：游戏实例 (GameInstance)
// 作用: 编排所有 GameInstanceSubsystem 的启动顺序
//       这是 Lyra / Riot / EA 等顶级工作室的标准做法
// ==========================================

#pragma once

// 引入 UGameInstance 基类头文件
// 作用: UGameInstance 是所有 Subsystem 的容器和编排器
#include "Engine/GameInstance.h"

// UE 自动生成的头文件 (必须放在最后一行)
#include "MetalSlugGameInstance.generated.h"

/**
 * @class UMetalSlugGameInstance
 * @brief MetalSlug 自定义游戏实例，作为全局启动编排器
 *
 * 【大厂架构 - Lyra CommonGame 模式】
 *
 * 设计要点:
 * 1. UGameInstance::Init() 是 Subsystem 全部 Initialize() 之后的回调
 * 2. 在这里编排启动顺序，可避免 Subsystem 之间相互依赖导致时序错乱
 * 3. 第一阶段: 等所有 Subsystem 自然 Initialize
 * 4. 第二阶段: 在本类 Init() 末尾调用 GameFlowSubsystem->BootToLogin()
 *    此时所有依赖 GameFlow 状态的 Service (如 UIViewService) 已就绪
 *
 * 【架构优势】
 * - 单点编排: 所有启动逻辑在一个文件里
 * - 顺序确定: 不再依赖 Subsystem 的隐式初始化顺序
 * - 易于调试: 可在这里打日志追踪启动链路
 * - 测试友好: 单元测试可以替换 GameInstance 类
 *
 * 【对应大厂】
 * - Lyra: UCommonGameInstance
 * - Riot: UGameInstance::Init() 全局启动链
 * - EA Frostbite: GameDirector
 */
UCLASS()
class METALSLUG01_API UMetalSlugGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	// ==========================================
	// UGameInstance 生命周期
	// ==========================================

	/**
	 * 【大厂标准】游戏实例初始化
	 *
	 * 触发时机:
	 * - 所有 UGameInstanceSubsystem 已完成 Initialize()
	 * - World 尚未 BeginPlay
	 * - 是启动编排的最后时机
	 *
	 * 工作流程:
	 * 1. 调用 Super::Init() (让所有 Subsystem 自然初始化)
	 * 2. 获取 GameFlowSubsystem
	 * 3. 调用 BootToLogin() 触发 PreLogin → Login 转换
	 *    → 自动触发 UIViewService 显示 LoginPage
	 */
	virtual void Init() override;

	/**
	 * 【大厂标准】游戏实例关闭
	 *
	 * 工作流程:
	 * 1. 调用 Super::Shutdown() (让所有 Subsystem 自然 Deinitialize)
	 * 2. 清理自定义资源
	 */
	virtual void Shutdown() override;

	// ==========================================
	// PIE 多客户端识别
	// ==========================================

	/**
	 * 【大厂架构 - 跨模式 PIE 识别 2026.06.30】
	 *
	 * UE 官方为 PIE 多客户端提供的唯一可靠识别接口.
	 *
	 * 为什么不用 GetLocalPlayer()->GetIndexInGameInstance() 和 -PIEWindowID 命令行:
	 *   - GetIndexInGameInstance: 跨进程 PIE 下, 每个客户端 LocalPlayer[0] 都返回 0 → 失效
	 *   - -PIEWindowID: Run Under One Process=true 才有, Run Under One Process=false 时无此参数
	 *
	 * InitializeForPlayInEditor 优势:
	 *   - Run Under One Process=true 和 false 都支持
	 *   - PIEInstanceIndex 从 0 开始按启动顺序分配 (0=Server/Listener, 1,2,3...=Clients)
	 *   - 跨进程唯一, 跨 PIE 实例唯一
	 *   - Editor 进程直接广播, 子进程通过 GameInstance 拷贝获取
	 *
	 * 时序:
	 *   - Editor: UEditorEngine::StartPlayInEditorSession → UGameInstance::InitializeForPlayInEditor → Init()
	 *   - 子进程: UEditorEngine::StartPlayInNewProcessSession → 启动新的 Editor 子进程 → 重复上面流程
	 *   - 每个子进程的 GameInstance 都会调 InitializeForPlayInEditor, 拿到对应 PIEInstanceIndex
	 */
	virtual FGameInstancePIEResult InitializeForPlayInEditor(
		int32 PIEInstanceIndex,
		const FGameInstancePIEParameters& Params) override;

	/**
	 * 【大厂架构 - 跨进程 PIE PIEInstanceIndex 获取 2026.06.30】
	 *
	 * 在 InitializeForPlayInEditor 中无法从 EditorEngine 拿 PIEInstanceIndex
	 * （跨进程 PIE 子进程中 GetEngine() 返回 UGameEngine 而非 UEditorEngine）。
	 *
	 * 正确方案: 从 WorldContext 读取 PIEInstance:
	 *   GEngine->GetWorldContextFromWorld(GetWorld())->PIEInstance
	 *
	 * 这个方法在 Editor 进程和子进程都有效.
	 * StartPlayInEditorGameInstance 在两个进程中都会被调用.
	 */
	virtual FGameInstancePIEResult StartPlayInEditorGameInstance(
		ULocalPlayer* LocalPlayer,
		const FGameInstancePIEParameters& Params) override;

	/**
	 * 获取本 GameInstance 缓存的 PIE Instance Index
	 *
	 * - PIE 模式下: 返回 UE 分配的跨进程唯一编号 (0=Server/Listener, 1,2,3...=Clients)
	 * - 非 PIE 模式: 返回 INDEX_NONE, 由调用方按"默认房主"处理
	 *
	 * @return PIE Instance Index, 或 INDEX_NONE (-1) 表示非 PIE 环境
	 */
	UFUNCTION(BlueprintPure, Category = "PIE")
	int32 GetCachedPIEInstanceIndex() const { return CachedPIEInstanceIndex; }

	/** PIE 模式判别 */
	UFUNCTION(BlueprintPure, Category = "PIE")
	bool IsPIEInstance() const { return CachedPIEInstanceIndex != INDEX_NONE; }

private:
	/** InitializeForPlayInEditor 时由 UE 分配的 PIE Instance Index (INDEX_NONE = 非 PIE) */
	int32 CachedPIEInstanceIndex = INDEX_NONE;
};