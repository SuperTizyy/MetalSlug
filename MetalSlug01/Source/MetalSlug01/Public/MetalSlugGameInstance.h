// ==========================================
// 大厂标准：游戏实例 (GameInstance) 头文件
// ==========================================
//
// 文件作用:
//   1. 声明 UMetalSlugGameInstance 类 — 继承 UGameInstance
//   2. 声明 Init() / Shutdown() 生命周期虚函数 override
//   3. UMetalSlugGameInstance 在 cpp 中实现编排逻辑
//
// 设计要点:
//   1. UGameInstance::Init() 是 Subsystem 全部 Initialize() 之后的回调
//   2. 在这里编排启动顺序，可避免 Subsystem 之间相互依赖导致时序错乱
//   3. 第一阶段: 等所有 Subsystem 自然 Initialize
//   4. 第二阶段: 在本类 Init() 末尾调用 GameFlowSubsystem->BootToLogin()
//      此时所有依赖 GameFlow 状态的 Service (如 UIViewService) 已就绪
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
 *
 * 【关键字段说明】
 * - 无自定义 UPROPERTY: GameInstance 仅做编排, 不持有游戏状态
 *   所有业务状态都在 Subsystem / GameState / PlayerState 中
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
	 * @brief  UGameInstanceSubsystem 全部 Initialize() 后由引擎回调
	 *
	 * 触发时机:
	 * - 所有 UGameInstanceSubsystem 已完成 Initialize()
	 * - World 尚未 BeginPlay
	 * - 是启动编排的最后时机
	 *
	 * 工作流程 (大厂 P0 修复 2026.07.03):
	 * 1. 调用 Super::Init() (让所有 Subsystem 自然初始化)
	 * 2. 打印启动日志 (不再主动 BootToLogin)
	 * 3. BootToLogin 由 PostLoadMapWithWorld 统一调度 — 见 cpp 注释
	 */
	virtual void Init() override;

	/**
	 * 【大厂标准】游戏实例关闭
	 *
	 * @brief  游戏退出/PIE 结束时由引擎回调 — 清理自定义资源
	 *
	 * 工作流程:
	 * 1. 调用 Super::Shutdown() (让所有 Subsystem 自然 Deinitialize)
	 * 2. 清理自定义资源 (本项目目前无自定义资源, 仅打印日志)
	 */
	virtual void Shutdown() override;
};