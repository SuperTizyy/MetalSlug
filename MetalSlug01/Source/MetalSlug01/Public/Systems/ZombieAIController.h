// Copyright (c) 2026.
//
// 【Phase 2 模式化】生化模式专用 AI Controller
//
// 设计原则 (大厂架构):
//   - 不写任何"如果我是僵尸就..."的 if 条件 — 避免污染共用层
//   - 全部继承 ABaseAIController 的统一链路 (阵营协议 + 感知配置 + BT 启动)
//   - 仅 override 生化专属的"感染人 → 改 TeamId + 重启 BT"这一根钩子
//
// 不做的:
//   ✗ 不重新实现感知配置 — Base 已经从 Profile->Perception 注入
//   ✗ 不重写 BT 启动 — Base 已经走 Profile.HuntPolicy 聚合策略
//   ✗ 不写任何调血/调速逻辑 — 数值都在 BehaviorConfigSO 里, 策划调
//   ✗ 不改 GameMode::RequestTargetForAI — GameMode 通过 Profile.HuntPolicy 多态即可
//
// 为什么需要单独一个 Controller 类:
//   1. CF 生化模式"母体"机制: 母体死亡时, 选中的人类感染者自动晋升为母体 (Phase 3)
//      这是僵尸"专属事件", 不在共用层实现.
//   2. GAS / GameplayAbility 绑订阅者: 感染技能触发 OnPawnBecomeZombie, 母体死亡广播 OnMotherFallen
//      这些 Delegate 不能放在共用层 (会污染刀战).
//   3. 工程上预留扩展点: 未来加 "群体扑击" / "感染范围伤害" 都进这一层.

#pragma once

#include "CoreMinimal.h"
#include "Systems/BaseAIController.h"
#include "ZombieAIController.generated.h"

/**
 * AZombieAIController
 * 生化模式 AI 控制器 (Phase 2)
 *
 * 复用策略:
 *   - 所有"读 Pawn / 读 BB / 调感知" 仍走 Base
 *   - 所有"按 Profile 选目标" 走 RoomGameMode::RequestTargetForAI (多态按 HuntPolicy)
 *   - 仅 override OnTargetDetected 做"母体特殊触发" + OnPossess 绑感染钩子
 */
UCLASS()
class METALSLUG01_API AZombieAIController : public ABaseAIController
{
	GENERATED_BODY()

public:
	AZombieAIController();

	/**
	 * 【Phase 2】生化控制器专属入口
	 * GameMode 在调用 InitializeFromProfile 之前, 会判 Profile.ControllerClass 是否 = ZombieAIController
	 * 如果是, 调本方法注入"感染机制订阅".
	 *
	 * 行为:
	 *   - 订阅 Pawn 的 OnHealthZeroed 事件 (死亡时若 Profile.AIRole==Mother, 触发母体阵亡广播)
	 *   - 不在这里跑 BT, BT 由 Base.StartBehaviorTreeFromProfile 启动
	 *
	 * 留可扩展: 后续可在这里订阅 GAS 事件链
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Zombie")
	void SetupZombieAI(UAIProfileAsset* ZombieProfile);

	/**
	 * 【Phase 3 预留】人类被感染回调
	 * 策划现阶段无需实现, Phase 3 接入: 某个 BTTask 调用 Server_Infect(thisPawn) → OnPawnBecomeZombie 广播
	 * 基类订阅, 子类选择性响应.
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPawnBecomeZombie, APawn* /*InfectedPawn*/);
	FOnPawnBecomeZombie OnPawnBecomeZombie;

protected:
	/**
	 * 生化专属: 重新绑定 OnTargetDetected 时, 调父类处理后再追加"距离极近时注册感染意图"
	 */
	virtual void OnTargetDetected(AActor* Actor, FAIStimulus Stimulus) override;

	/**
	 * 生化专属: 当 Pawn 被 Possess 后, 自动尝试开启 BT (Base 已做, 这里只是挂感染/死亡订阅)
	 */
	virtual void OnPossess(APawn* InPawn) override;
};
