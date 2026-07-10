// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// ==========================================
// URoomTargetingSubsystem — AI 仇恨账本 + 目标选择子系统
//
// 【2026.07.11 v31 大厂架构重构】从 RoomGameMode 拆出
//
// 设计原则:
//   - 单一职责: AI 选目标 + 仇恨账本维护 (谁在追谁)
//   - 不持 Pawn 列表: 候选敌人从外部传入 (避免双向依赖)
//   - 不读 PlayerState: 目标是否死亡由调用方决定 (本系统不判断)
//
// 职责清单:
//   - RequestTargetForAI: 主入口 — AI 申请一个攻击目标
//   - ScoreCandidateForAI: 按 ConfigSO.HuntPolicy 评分
//   - GetAllAliveEnemiesFor: 候选池 (需要在 Subsystem 内或 GameMode 提供 alive 判断)
//   - GetAttackerCount / IsTargetLocked / IsTargetLockedByOthers: 账本查询
//   - ReleaseTarget: AI 死亡/换目标时释放记录
//   - GetEffectiveHuntPolicy: 从 AI 拿它当前的 HuntPolicy
//
// 【v54 大厂架构重构】Profile.HuntPolicy → ConfigSO.HuntPolicy
//   - UAIProfileAsset 已删除, HuntPolicy 字段迁移到 UAIBehaviorConfigSO
//   - 评分权重由 ConfigSO.HuntPolicy 决定, 不允许硬编码
//   - 反扎堆账本 (AIHuntingMap) 完全在本系统内维护
//   - GameMode 不允许直读/直写这两个 TMap
//
// 访问入口:
//   URoomTargetingSubsystem* TargetSys = URoomTargetingSubsystem::Get(this);
// ==========================================

// UE 引擎基础
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

// AI 行为类型 (FAIHuntPolicy / EAIRole / etc — 已在内部间接 include RoomEnums / GameplayTagContainer)
#include "Systems/AI/AIBehaviorTypes.h"

// 自动生成的反射头 — 必须放在所有 #include 之后, forward declaration 之前
// 大厂注意 (UE 5.6 UHT 严格模式): .generated.h 必须用裸文件名, 不能带目录前缀
//   UHT Parser 中: includeNameString 跟 GeneratedHeaderFileName 做字面 OrdinalIgnoreCase 比对
//   "Systems/Targeting/RoomTargetingSubsystem.generated.h" 永远 != "RoomTargetingSubsystem.generated.h"
#include "RoomTargetingSubsystem.generated.h"

// ==========================================
// 前向声明 — 避免在本头中 include 完整定义
// ==========================================
class ABaseCharacter;
class ABaseAIController;
class UAIBehaviorConfigSO;  // 【v54 大厂架构重构】替代 UAIProfileAsset
class ARoomGameState;

UCLASS()
class METALSLUG01_API URoomTargetingSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// ==========================================
	// 生命周期
	// ==========================================

	static URoomTargetingSubsystem* Get(const UObject* WorldContextObject);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/**
	 * @brief 取候选敌人池 (活 + 异阵营)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Targeting")
	TArray<ABaseCharacter*> GetAllAliveEnemiesFor(ABaseCharacter* RequestingAI);

	// ==========================================
	// 目标选择主入口
	// ==========================================

	/**
	 * @brief AI 申请一个攻击目标
	 *
	 * 流程:
	 *   1. 取候选敌人 (GameMode 传入, 本系统不维护 alive 列表)
	 *   2. 距离预过滤 (Policy.MaxChaseDistance)
	 *   3. 优先 unlocked, fallback 到 locked
	 *   4. 按 ConfigSO.HuntPolicy 评分, 选最佳
	 *   5. 写入 AIHuntingMap 账本
	 *
	 * @param RequestingAI 请求方
	 * @param Candidates 候选敌人列表 (GameMode 提供, 本系统不维护)
	 * @return 分配的敌人目标, 找不到返回 nullptr
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Targeting")
	ABaseCharacter* RequestTargetForAI(ABaseCharacter* RequestingAI, const TArray<ABaseCharacter*>& Candidates);

	/**
	 * @brief 按 ConfigSO.HuntPolicy 求一个候选的"分数"
	 * 【v54 大厂架构重构】Policy 参数从 Profile.HuntPolicy 改为 ConfigSO.HuntPolicy
	 * @return 评分 (0~1)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Targeting")
	float ScoreCandidateForAI(ABaseCharacter* RequestingAI,
		ABaseCharacter* Candidate, const FAIHuntPolicy& Policy) const;

	// ==========================================
	// 账本查询
	// ==========================================

	UFUNCTION(BlueprintPure, Category = "Room|Targeting")
	int32 GetAttackerCount(ABaseCharacter* TargetEnemy);

	UFUNCTION(BlueprintCallable, Category = "Room|Targeting")
	bool IsTargetLocked(ABaseCharacter* TargetEnemy);

	UFUNCTION(BlueprintCallable, Category = "Room|Targeting")
	bool IsTargetLockedByOthers(ABaseCharacter* TargetEnemy, ABaseCharacter* ExcludeAI);

	// ==========================================
	// 账本管理
	// ==========================================

	UFUNCTION(BlueprintCallable, Category = "Room|Targeting")
	void ReleaseTarget(ABaseCharacter* RequestingAI);

	/**
	 * @brief 从 AI 拿它当前生效的 HuntPolicy
	 */
	FAIHuntPolicy GetEffectiveHuntPolicy(ABaseCharacter* AI) const;

	/**
	 * @brief 清空所有账本 (每局开始)
	 */
	void ClearAllHunting();

protected:
	// ==========================================
	// 内部状态: 仇恨账本
	// ==========================================

	/**
	 * Key = 猎人 (AI), Value = 猎物 (玩家)
	 * 用于反扎堆均摊: 数 Value 出现次数即可知道某敌人被几个 AI 追
	 */
	UPROPERTY()
	TMap<ABaseCharacter*, ABaseCharacter*> AIHuntingMap;
};