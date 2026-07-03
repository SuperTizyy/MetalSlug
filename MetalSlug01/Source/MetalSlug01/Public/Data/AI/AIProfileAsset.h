// Copyright (c) 2026.
//
// Phase 1 数据驱动层 - AI 身份标识
// 设计: PrimaryDataAsset, 可接入 Asset Manager 做按需加载
//       通过 FGameplayTag ProfileTag 做存档/成就/掉落反查

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Systems/AI/AIBehaviorTypes.h"  // 【Phase 2】注入 EAIRole
#include "AIProfileAsset.generated.h"

class UAIBehaviorConfigSO;

/**
 * Profile 异步加载完成回调签名
 * 设计: 不依赖 StreamableManager 头文件, 让本头能被更广范围 include
 */
DECLARE_DELEGATE(FOnAIBehaviorConfigLoaded);

/**
 * UAIProfileAsset - AI 的"身份证 + 名片"
 *
 * 作用:
 *   - 给每个 AI 一个全局唯一标识 (ProfileTag)
 *   - 提供"按需加载"的 Config 软引用
 *   - 让策划能在编辑器点选"我想要的 AI 类型"
 *
 * 注意:
 *   - 此处不持有 BehaviorTree / SightConfig 硬引用
 *   - 仅持有一个指向 UAIBehaviorConfigSO 的软引用 (内存友好)
 */
UCLASS(BlueprintType)
class METALSLUG01_API UAIProfileAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /** 业务唯一标识 - 用于存档反查、AI 生成表查询 */
    UPROPERTY(EditDefaultsOnly, Category = "Identity",
        meta = (Categories = "AI.Profile"))
    FGameplayTag ProfileTag;

    /** 显示名 (策划阅读用) */
    UPROPERTY(EditDefaultsOnly, Category = "Identity")
    FText DisplayName;

    /** AI 等级 (影响经验/掉落, 与难度无关) */
    UPROPERTY(EditDefaultsOnly, Category = "Identity",
        meta = (ClampMin = "1", ClampMax = "99"))
    int32 Level = 1;

    /** 大阵营 (用于 IGenericTeamAgentInterface) - 留空表示"中立" */
    UPROPERTY(EditDefaultsOnly, Category = "Identity",
        meta = (Categories = "Faction"))
    FGameplayTag FactionTag;

    /**
     * 【Phase 2 模式化】AI 角色身份
     * 设计: 区分刀战里的"普通肉搏"和生化里的"母体/僵尸/人类AI"
     *       GameMode 的选目标策略/AI Controller 类都按这个决定
     *       不影响 Combat/Movement 等数值 (那些仍在 BehaviorConfigSO 里)
     */
    UPROPERTY(EditDefaultsOnly, Category = "Identity")
    EAIRole AIRole = EAIRole::Soldier;

    /**
     * 【Phase 2 模式化】目标选择策略
     * 替代 RoomGameMode::RequestTargetForAI 巨型 if/else
     * 留空 = 默认 NearestDistance (向后兼容)
     */
    UPROPERTY(EditDefaultsOnly, Category = "Identity")
    FAIHuntPolicy HuntPolicy;

    /**
     * 关联行为配置 - 软引用按需加载
     */
    UPROPERTY(EditDefaultsOnly, Category = "Config",
        meta = (AllowedClasses = "/Script/MetalSlug01.AIBehaviorConfigSO"))
    TSoftObjectPtr<UAIBehaviorConfigSO> BehaviorConfig;

    /**
     * 【Phase 2】AI Controller 子类
     * 替代 RoomGameMode 里写死的 AMeleeAIController::StaticClass()
     * 缺省时 = UAIProfileAsset::DefaultControllerClass (Soldier 通用)
     * 生化时 = AZombieAIController::StaticClass()
     *
     * 注意: 在 Runtime 层允许留空 (走兜底), 但 Editor 默认留空渲染为 Default.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Config",
        meta = (AllowAbstract = "false"))
    TSubclassOf<class AAIController> ControllerClass;

    /**
     * 【P0 大厂架构 2026.07.03 19:35】AI 武器 RowName
     *
     * 设计: AI 也是角色, 跟玩家一样需要武器. 但 AI 的武器选择不是玩家临时选,
     *       而是 AI 类型固定的 (刀战 AI = Knife01, 枪战 AI = 枪枪, etc.)
     *       所以挂在 Profile 上, 数据驱动, 不在 SpawnRequest 里.
     *
     * 查找逻辑:
     *   - SpawnAIInternal: 读 Profile.WeaponID → 写入 ABaseCharacter.SpawnWeaponID (服务器上)
     *   - PossessedBy: 读 Pawn.SpawnWeaponID (优先), 回退 PS (玩家)
     *
     * 编辑器填法:
     *   DA_AIProfile_MeleeGrunt -> WeaponID = "WQ001" (刀战默认武器)
     *   留空 = 不生成武器 (跟 AI 设计意图一致, 比如诱饵)
     */
    UPROPERTY(EditDefaultsOnly, Category = "Identity",
        meta = (DisplayName = "Weapon RowName (DT_WeaponInfo)"))
    FName WeaponID = NAME_None;

    /**
     * 【P0 大厂架构 2026.07.03 19:35】AI 角色 RowName (DT_CharacterInfo)
     *
     * 用途: 跟 Request.CharacterRowName 解耦 — 即使 SpawnRequest 留空,
     *       Profile 自己可以指定默认角色. SpawnAIInternal 用 Profile.CharacterRowName 兜底.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Identity",
        meta = (DisplayName = "Character RowName (DT_CharacterInfo)"))
    FName CharacterRowName = NAME_None;

    /**
     * 【P0 大厂架构 2026.07.06 19:25】AI 攻击间隔（Profile 级覆盖）
     *
     * 目的: 让策划在 AIProfileAsset 上直接控制"AI 多长时间打一次",
     *       不用每次都打开 AIBehaviorConfigSO + 难度表去改.
     *       实战中"AI 持续连击"问题常因为间隔太小 / 没配默认值, 暴露在这里更直观.
     *
     * 取值优先级（自上而下短路求值）:
     *   1. Profile.AttackInterval > 0        → 直接用这个值
     *   2. Profile.AttackInterval <= 0       → 回退 AIBehaviorConfigSO.Combat.AttackCooldown
     *   3. 还没拿到 Config                    → 兜底 1.5s
     *
     * 范围建议:
     *   - 0.5 ~ 1.0s : 狂暴 Boss / Boss 阶段二 (密集连击, 给玩家压迫感)
     *   - 1.2 ~ 1.8s : 普通近战 AI (默认, 玩家可以反应)
     *   - 2.0 ~ 3.5s : 笨重型僵尸 / 重武器 AI (慢, 但伤害高)
     *
     * 注意: 这个值只控制"两次攻击之间的最短间隔", 不会让 Combo 段内部也变慢
     *       (Combo1 段动画完整播放由 OnAIAttackMontageEnded 事件回调保证).
     *
     * < 0 = 用 ConfigSO 默认; 0 = 禁止攻击; > 0 = 自定义间隔
     */
    UPROPERTY(EditDefaultsOnly, Category = "Combat",
        meta = (ClampMin = "-1.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "5.0",
                DisplayName = "Attack Interval (秒, -1=用Config默认)"))
    float AttackInterval = -1.0f;

    /**
     * 【P0 2026.07.06 19:25】攻击间隔的有效获取函数
     *
     * 用途: BTTask / AIController 一行代码拿到最终间隔, 不用关心回退链
     *       复用 ConfigSO 已有的 GetScaledCombat().AttackCooldown 做二级回退
     *
     * @return 最终攻击间隔（秒）; 0 表示禁止攻击
     */
    UFUNCTION(BlueprintPure, Category = "AI|Profile")
    float GetEffectiveAttackInterval() const;

    /** 同步加载 Config (用于 PIE 测试 / 单机关卡) */
    UFUNCTION(BlueprintCallable, Category = "AI|Profile")
    UAIBehaviorConfigSO* LoadBehaviorConfigSync();

    /**
     * 异步加载 Config
     * @param OnLoaded 加载完成后回调 (无参数)
     * 设计: 使用 DECLARE_DELEGATE 而非 FStreamableDelegate, 避免 Public 头传染 StreamableManager.h
     */
    void LoadBehaviorConfigAsync(FOnAIBehaviorConfigLoaded OnLoaded);

    /** Asset Manager 注入 - 让 BA 知道本资源属于哪个 Pool */
    virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
