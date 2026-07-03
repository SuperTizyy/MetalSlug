// Copyright (c) 2026.

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"

#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "GenericTeamAgentInterface.h"
#include "Data/AI/AIProfileAsset.h" // 【Phase 1】FOnAIBehaviorConfigLoaded
#include "Systems/AI/AIBehaviorTypes.h"
#include "BaseAIController.generated.h"

// 前置声明（加快编译）
class UAIPerceptionComponent;
class UBehaviorTree;
class UAIRuntimeConfigComponent;
class UAIBlackboardKeyRegistrySubsystem;

/**
 * ABaseAIController
 * 项目所有 AI 控制器的 C++ 基类
 *
 * 【Phase 1 重构】一次性切换到 UE5 阵营协议:
 *   - 砍掉自造的 uint8 TeamID + Cast<ABaseCharacter> 阵营判定
 *   - 阵营判定走 IGenericTeamAgentInterface::GetTeamAttitudeTowards (UE5 原生)
 *   - AIPerception 检测到目标时, 引擎自己询问敌我, 自动决定是否回调 OnTargetPerceptionUpdated
 *   - 行为树参数 (250 / 1500 / 1800 / 90) 全部下沉到 UAIBehaviorConfigSO (RuntimeConfigComponent 提供)
 *   - 行为树启动走 UAIProfileAsset 异步加载; 老直启字段已废
 */
/**
 * 【UE 5.6 修复】 AAIController 已经继承 IGenericTeamAgentInterface
 * 我们只需 override, 不必重复声明接口冒. 重写 GetGenericTeamId / GetTeamAttitudeTowards 即可.
 */
UCLASS()
class METALSLUG01_API ABaseAIController : public AAIController
{
	GENERATED_BODY()

public:
	ABaseAIController();

	/**
	 * 入口: 由 GameMode/Spawner 在生成 AI 后调用
	 * 行为契约:
	 *   - Profile → 同步 LoadBehaviorConfigSync → ApplyConfig → async LoadBehaviorTree → RunBehaviorTree
	 *   - Profile 为空: 标记无 Profile 状态, 调用 RunLegacyIfPossible 走 fallback (Blueprint 默认 BT)
	 */
	UFUNCTION(BlueprintCallable, Category = "AI")
	void InitializeFromProfile(UAIProfileAsset* InProfile);

	/** 难度热注入 */
	UFUNCTION(BlueprintCallable, Category = "AI")
	void SetDifficultyTier(EAIDifficultyTier NewTier);

	/** 统一读 Config 入口 */
	UFUNCTION(BlueprintPure, Category = "AI")
	UAIRuntimeConfigComponent* GetRuntimeConfig() const { return RuntimeConfig; }

	/**
	 * 【Phase 2】公开读取当前 Profile — 让 GameMode 等外部系统不用 friend 也能拿到
	 * 设计: 当前生效的 AI Profile (可能为 nullptr, 表示走 Legacy)
	 */
	UFUNCTION(BlueprintPure, Category = "AI")
	UAIProfileAsset* GetCurrentProfile() const { return CurrentProfile; }

	/** 获取黑板键名解析器 */
	UFUNCTION(BlueprintPure, Category = "AI|Blackboard")
	UAIBlackboardKeyRegistrySubsystem* GetKeyRegistry() const;

	/**
	 * 【Phase 1 重构】AI 自身阵营
	 * 设计: 走 IGenericTeamAgentInterface (不再自造 uint8 TeamID)
	 * 默认 ID=255 (Hostile), 由 InitializeFromProfile 根据 Profile.FactionTag 切到具体阵营
	 */
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
	UAIPerceptionComponent* AIPerception;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
	TObjectPtr<UAIRuntimeConfigComponent> RuntimeConfig;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
	TObjectPtr<UAIProfileAsset> CurrentProfile;

	/**
	 * 【Phase 1 重构】感知触发的单一入口
	 * 设计:
	 *   - 引擎已通过 IGenericTeamAgentInterface 把敌我对错处理完毕, 这里不再做 Cast<ABaseCharacter>
	 *   - 极近距离时把目标覆盖写入 BB 的 ImmediateTarget Key (由 Subsystem 解析名)
	 *   - 距离阈值从 RuntimeConfig->GetScaledCombat().OverrideBTDistance 读取
	 */
	UFUNCTION()
	virtual void OnTargetDetected(AActor* Actor, FAIStimulus Stimulus);

private:
	void OnProfileLoaded();
	void RegisterBlackboardKeys();
	void StartBehaviorTreeFromProfile();
	void RunLegacyBehaviorTree();

	/**
	 * 【P0 大厂架构 2026.07.03 19:22】主动扫描最近的敌方 Character
	 * 当 AIPerception 失效 / SightR 不够 / Perception 初始化未完成时的兜底
	 * 用阵营接口找最近敌人, 返回 ScanRange 内最近的
	 * @param MyCharacter  自身 Character (用于计算距离)
	 * @return 最近的敌方 Character, 没找到返回 nullptr
	 */
	AActor* ScanForNearestEnemy(ACharacter* MyCharacter);

	/**
	 * 【Phase 2 共用层】按 Profile->Perception 配 AIPerception
	 * 设计: 把"刀战专属"的 ConfigurePerceptionFromConfig 收归 Base
	 *       所有 AI 派生类 (Melee/Zombie/Boss) 都不用再写一遍
	 * 调用时机: OnProfileLoaded 之后, BT 启动之前
	 */
	void ConfigurePerceptionFromConfig();

	/**
	 * 【P0 架构升级 2026.07.03】启动期一次性全景诊断
	 * 输出 4 项关键状态: Profile / 阵营 / 感知 / NavMesh
	 * 任何一项异常用 Error 级别, 方便一次扫描日志看清全局
	 * 调用时机: OnProfileLoaded 末尾 (配置完成后), BeginPlay (兜底)
	 */
	void DiagnoseAndLogBootStatus() const;

	/**
	 * 【P0 架构升级 2026.07.03】C++ 层 MoveTo 兜底
	 * 设计原则 (大厂):
	 *   "永远不要让单点故障 (BT 树设计错) 拖垮整个 AI 系统"
	 *   BT 树可能是 .uasset 二进制, 出错不好修.
	 *   本函数在 C++ 层每 0.3s 检查一次: 如果 TargetActor 存在但 AI 没在追, 主动 MoveTo.
	 *   - 仅在距离 > AttackRange 且 BB 没标记 InCombat 时触发
	 *   - 不与 BT 树冲突: MoveToLocation 是异步的, BT 重跑会自然接管
	 *   - 攻击范围 (<=AttackRange) 时让 BT Attack task 接管, C++ 不抢
	 *
	 * 调用时机: Tick (BeginPlay 启用 PrimaryActorTick.bCanEverTick)
	 */
	void TickChaseFallback();

	/** FOnAIBehaviorConfigLoaded 句柄, 跨 OnProfileLoaded 暂存 */
	FOnAIBehaviorConfigLoaded ProfileLoadedDelegate;

protected:
	/**
	 * 【Phase 1】派生类钩子入口
	 * 设计: 派生类 SetupMeleeAI 重写感知配置后, 必须调用本方法触发 BT 启动
	 * 行为:
	 *   - 已开始 BT (bBehaviorTreeStarted): 直接返回
	 *   - 未开始: 走 Config 同步+异步加载路径
	 */
	void StartBehaviorTreeFromConfig();

private:
	bool bBehaviorTreeStarted = false;

	/**
	 * 【P0 架构升级 2026.07.03】C++ 层 MoveTo 兜底的节流计时
	 * 避免每帧 MoveTo 浪费 CPU, 0.3s 检查一次足够 (AI 不会感知到 0.3s 内的位置变化)
	 */
	float TimeSinceLastChaseCheck = 0.f;

	/**
	 * 【P0 架构升级 2026.07.03 19:14】AddMovementInput 兜底方案的目标缓存
	 * 设计: 每 0.3s 从 BB 读一次目标, 中间帧复用缓存
	 *  - 避免每帧 BB->GetValueAsObject (哈希查询, 浪费 CPU)
	 *  - 目标失效 (Player 死亡/丢失) 时缓存自然失效 (TWeakObjectPtr)
	 * 用 TWeakObjectPtr 而非裸指针: Player 死亡时不会野指针
	 */
	TWeakObjectPtr<AActor> CachedChaseTarget;
};
