// Copyright (c) 2026.
//
// Phase 1 数据驱动层 - 行为类型定义
// 约定: 所有 AI 模块的"概念定义"集中在此, 不含任何运行逻辑

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h" // 【Phase 1】UE5 官方阵营协议
#include "Data/Enums/RoomEnums.h"      // 【Phase 2】用 ERoomMatchMode 做模式化配置
#include "AIBehaviorTypes.generated.h"

// 【Phase 2】前置声明 UAIProfileAsset — 避免 AIProfileAsset.h <-> AIBehaviorTypes.h 循环依赖
// TSoftObjectPtr<UAIProfileAsset> 仅在 FAIProfileRegistry 中使用, UHT 用 forward decl 已足够.
class UAIProfileAsset;

/**
 * 角色身份 - 模式无关的统一身份
 * 设计: 刀战不分人类/僵尸, 生化分.
 *       这个枚举是"AI 自我认知"的语义, 跟 FactionTag 解耦.
 *       所有需要按身份走不同逻辑的地方读这个.
 */
UENUM(BlueprintType)
enum class EAIRole : uint8
{
    Soldier      UMETA(DisplayName = "士兵"),       // 通用肉搏怪 (刀战僵尸/近战怪都算)
    Sniper       UMETA(DisplayName = "狙击手"),     // 远距离优先
    Human        UMETA(DisplayName = "人类玩家型"), // 生化里的人类AI (如果有的话)
    Zombie       UMETA(DisplayName = "僵尸"),       // 生化里的僵尸
    Mother       UMETA(DisplayName = "母体"),       // 生化里的母体 (按剩余时间猎杀)
    Boss         UMETA(DisplayName = "Boss"),       // 预留 (未来的 BOSS)
};

/**
 * AI 难度档位
 * 设计: 所有数值倍率在 Config 里 * EnumLevel 系数读取
 * 应用: 由 GameMode 通过 ABaseAIController::SetDifficultyTier 注入
 */
UENUM(BlueprintType)
enum class EAIDifficultyTier : uint8
{
    Easy      UMETA(DisplayName = "Easy"),       // 0.7x 数值
    Normal    UMETA(DisplayName = "Normal"),     // 1.0x 数值
    Hard      UMETA(DisplayName = "Hard"),       // 1.4x 数值
    Insane    UMETA(DisplayName = "Insane"),     // 1.8x 数值
};

/**
 * AI 目标选择策略
 * 设计: 不同模式/角色 用不同策略选目标.
 *       走"策略对象模式"而不是巨型 if 分叉:
 *         - Melee   : NearestDistance  (够近就追)
 *         - Zombie  : RandomValid      (公平猎杀)
 *         - Mother  : TimeAttitudeWeight (剩余时间权重最高)
 *
 * 注意: 模式=RoomGameMode/ProjectSettings, 角色=Profile.AIRole
 *       这两层正交, 不能写死 if-and-only-if.
 */
UENUM(BlueprintType)
enum class EAIHuntStrategy : uint8
{
    NearestDistance       UMETA(DisplayName = "最近距离"),           // 刀战 default
    RandomValid           UMETA(DisplayName = "随机有效"),           // 生化 default
    HighestScore          UMETA(DisplayName = "最高积分"),           // 复仇者
    TimeAttitudeWeighted  UMETA(DisplayName = "剩余时间加权"),       // 生化母体 (CF 经典)
    Custom                UMETA(DisplayName = "自定义扩展"),
};

/**
 * Blackboard Key 集中注册 (用 Tag 描述, 编译期可校验)
 * 设计: 所有 BT/BTTask 读 Key 时, 通过 UAIBlackboardKeyRegistrySubsystem::ResolveKey 解析
 *       严禁在任何 C++ / BP 中直接出现 "ImmediateTarget" 这种裸字符串字面量
 */
UENUM(BlueprintType)
enum class EAIBlackboardKey : uint8
{
    TargetActor       UMETA(DisplayName = "TargetActor"),      // 仲裁分配的锁定目标（由 BTService 写入）
    NearbyThreat      UMETA(DisplayName = "NearbyThreat"),      // 极近距离遭遇的敌人（OverrideBTDistance 触发时写入）
    HomeLocation      UMETA(DisplayName = "HomeLocation"),      // AI 出生点（巡逻/回家用）
    LastKnownLocation UMETA(DisplayName = "LastKnownLocation"), // 目标丢失前的最后位置
    bIsInCombat       UMETA(DisplayName = "bIsInCombat"),      // 是否在战斗中
    bHasAttackToken   UMETA(DisplayName = "bHasAttackToken"),  // 攻击令牌（防抖标志）
    PatrolPath        UMETA(DisplayName = "PatrolPath"),       // 巡逻路径数据
    CurrentPhase      UMETA(DisplayName = "CurrentPhase"),     // 当前行为阶段（预留）
    PhaseTimer        UMETA(DisplayName = "PhaseTimer"),       // 阶段计时器
};

/**
 * 感知参数 - 替换 MeleeAIController.cpp 第 43-48 行硬编码
 * 暴露给编辑器: 类别按"功能组"折叠, 数值带 Clamp 限制
 */
USTRUCT(BlueprintType)
struct FAIPerceptionParams
{
    GENERATED_BODY()

    /** 视野有效半径 (cm) */
    UPROPERTY(EditDefaultsOnly, Category = "Perception", meta = (ClampMin = "0.0", ClampMax = "10000.0"))
    float SightRadius = 1500.f;

    /** 失去视野的有效半径 (cm) - 必须 >= SightRadius */
    UPROPERTY(EditDefaultsOnly, Category = "Perception", meta = (ClampMin = "0.0", ClampMax = "10000.0"))
    float LoseSightRadius = 1800.f;

    /** 周视角角度 (°) */
    UPROPERTY(EditDefaultsOnly, Category = "Perception", meta = (ClampMin = "0.0", ClampMax = "180.0"))
    float PeripheralVisionAngleDegrees = 90.f;

    /** 听觉半径 (cm) - 后期接入 UAISenseConfig_Hearing */
    UPROPERTY(EditDefaultsOnly, Category = "Perception", meta = (ClampMin = "0.0"))
    float HearingRadius = 1200.f;

    UPROPERTY(EditDefaultsOnly, Category = "Perception|Detection")
    bool bDetectEnemies = true;

    UPROPERTY(EditDefaultsOnly, Category = "Perception|Detection")
    bool bDetectNeutrals = true;

    UPROPERTY(EditDefaultsOnly, Category = "Perception|Detection")
    bool bDetectFriendlies = true;
};

/**
 * 战斗参数 - 替换 BaseAIController.cpp 第 93 行硬编码 250.0f
 * 注意: 重命名默认值, 旧值是 250, 这里暴露 OverrideBTDistance 给编辑器配置.
 *       之前写死 OverrideBTDistance = 250.f; 改造后由策划在 DA 里填, 默认 250 不变.
 */
USTRUCT(BlueprintType)
struct FAICombatParams
{
    GENERATED_BODY()

    /** 攻击判定距离 (cm) */
    UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0.0", ClampMax = "2000.0"))
    float AttackRange = 180.f;

    /** 攻击冷却 (秒) */
    UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0.0"))
    float AttackCooldown = 1.2f;

    /** 单次攻击伤害 */
    UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0.0"))
    float Damage = 12.f;

    /** 弹药类型 (GameplayTag) - 留给武器系统 Phase 3 接入 */
    UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (Categories = "Weapon.AmmoType"))
    FGameplayTag AmmoType;

    /** 极近距离 (cm) - 进入该距离时强制覆盖 BT 写 ImmediateTarget */
    UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
    float OverrideBTDistance = 250.f;
};

/**
 * 移动参数 - 后续 NavMesh / 寻路统一入口
 */
USTRUCT(BlueprintType)
struct FAIMovementParams
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = "0.0"))
    float WalkSpeed = 250.f;

    UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = "0.0"))
    float RunSpeed = 600.f;

    UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = "0.0"))
    float AcceptanceRadius = 60.f;
};

/**
 * 调试参数 - 控制 UE 原生 AIPerceptionDebugDrawer / BehaviorTreeDebugger 输出
 */
USTRUCT(BlueprintType)
struct FAIDebugParams
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, Category = "Debug")
    bool bDrawPerception = true;

    UPROPERTY(EditDefaultsOnly, Category = "Debug")
    bool bDrawBehaviorTreeState = false;

    UPROPERTY(EditDefaultsOnly, Category = "Debug")
    FColor DebugColor = FColor::Red;
};

/**
 * 【Phase 2 模式化】目标选择策略配置
 * 设计:
 *   - 替代 RoomGameMode::RequestTargetForAI 里的巨型 if/else
 *   - 每条 Profile 按自己的 Strategy 选目标 (刀战=Nearest, 生化=Random, 母体=TimeAttitude)
 *   - 反扎堆仍由 GameMode 负责 (Lock 账本), 这里是"如何挑选"的算法
 *
 * 行为:
 *   - Nearest: 在候选人列表里选距离最近的, 距离平方 < 权重阈值
 *   - RandomValid: 随机抽一个未被攻击/未死的
 *   - HighestScore: PS->GetScore() 最高的 (排名优先)
 *   - TimeAttitudeWeighted: 隐身时间最少 + 距离近的, 综合权重 (CF 母体算法)
 *
 * 权重字段说明 (字段命名统一):
 *   - 0 = 不用; 1.0 = 唯一指标; 0~1 = 与其他指标混合.
 *   例: Mother 用 DistanceWeight=0.2, ScoreWeight=0.0, TimeRemainingWeight=0.8
 */
USTRUCT(BlueprintType)
struct FAIHuntPolicy
{
    GENERATED_BODY()

    /** 主策略枚举 */
    UPROPERTY(EditDefaultsOnly, Category = "Hunt")
    EAIHuntStrategy Strategy = EAIHuntStrategy::NearestDistance;

    /** 是否启用反扎堆均摊 (被多个 AI 追的目标会减分) */
    UPROPERTY(EditDefaultsOnly, Category = "Hunt", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float AntiHuddleWeight = 0.5f;

    /** Nearest: 距离权重 (1.0 纯距离, 0.0 不影响) */
    UPROPERTY(EditDefaultsOnly, Category = "Hunt", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DistanceWeight = 1.0f;

    /** HighestScore: PS->GetScore() 权重 */
    UPROPERTY(EditDefaultsOnly, Category = "Hunt", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ScoreWeight = 0.0f;

    /** Mother 用: 隐身时间最少 (剩余时间越短权重越高) */
    UPROPERTY(EditDefaultsOnly, Category = "Hunt", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float TimeRemainingWeight = 0.0f;

    /** 候选敌人最大距离 (cm), 超过直接剔除 */
    UPROPERTY(EditDefaultsOnly, Category = "Hunt", meta = (ClampMin = "0.0"))
    float MaxChaseDistance = 3000.f;

    /** 候选敌人最低血量阈值 (低于该值不去追, 默认 0=血多也能追) */
    UPROPERTY(EditDefaultsOnly, Category = "Hunt", meta = (ClampMin = "0.0"))
    float MinHealthThreshold = 0.f;
};

/**
 * 【Phase 2 模式化】AI Spawn 参数包
 * 设计:
 *   - 替代 RoomGameMode::AddAIToRoom 里的散参数 (bToAttackTeam, CharacterName, Count)
 *   - 加 spawn 模式 + 角色标签 + 数量
 *   - 由 GameMode 内部构造, 调用方只填业务字段
 */
USTRUCT(BlueprintType)
struct FAISpawnRequest
{
    GENERATED_BODY()

    /** 来源模式 - 决定用哪条 Profile */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Spawn")
    ERoomMatchMode Mode = ERoomMatchMode::None;

    /** 目标队伍 - 刀战的 Attack/Defense, 生化里当 Faction.Human 容器 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Spawn")
    ERoomTeam Team = ERoomTeam::Attack;

    /** 角色蓝图名 (查 CharacterDataTable) - 留空 = Profile 内默认 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Spawn")
    FName CharacterRowName = NAME_None;

    /** AI 类型标签 - 用于从 ProfilesByMode 选 Profile (例如 AI.Profile.MeleeGrunt / AI.Profile.ZombieMother) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Spawn",
        meta = (Categories = "AI.Profile"))
    FGameplayTag ProfileTag;

    /** 生成数量 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Spawn", meta = (ClampMin = "1"))
    int32 Count = 1;

    /** 是否覆盖出生点 (true=用系统默认, false=按队伍分配) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Spawn")
    bool bUseTeamSpawnPoint = true;
};

/**
 * 【Phase 2 模式化】单模式下的 Profile 注册表
 * 设计: 替代 TMap<ERoomMatchMode, TMap<Tag, Profile>> 的内层 TMap.
 *       UHT 不支持 TMap 嵌套作为 TMap value, 这里用 USTRUCT 包装.
 *       Key 不变 (FGameplayTag), Value 仍是 TSoftObjectPtr<UAIProfileAsset>.
 */
USTRUCT(BlueprintType)
struct FAIProfileRegistry
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, Category = "Profile", meta = (Categories = "AI.Profile"))
    TMap<FGameplayTag, TSoftObjectPtr<UAIProfileAsset>> Profiles;
};

/**
 * 【Phase 2 模式化】模式专属规则配置
 * 设计:
 *   - GameMode 持 TMap<ERoomMatchMode, FAIModeRules>
 *   - AddAIToRoom 读规则: 模式专属战斗时长/Profile/出生点分类/武器禁用
 *   - 老路径里 hardcoded "Faction.Player/Zombie" 改用 Profile.FactionTag
 */
USTRUCT(BlueprintType)
struct FAIModeRules
{
    GENERATED_BODY()

    /** 该模式的默认 AI Profile (按 ProfileTag 多个, 查表 Role 字段再选) */
    UPROPERTY(EditDefaultsOnly, Category = "Mode")
    FAIProfileRegistry DefaultProfiles;

    /** 攻方阵营 Tag - 替代硬编码 "Faction.Player" */
    UPROPERTY(EditDefaultsOnly, Category = "Mode|Faction",
        meta = (Categories = "Faction"))
    FGameplayTag AttackTeamFaction;

    /** 守方阵营 Tag - 替代硬编码 "Faction.Zombie" */
    UPROPERTY(EditDefaultsOnly, Category = "Mode|Faction",
        meta = (Categories = "Faction"))
    FGameplayTag DefenseTeamFaction;

    /** 该模式是否启用回合制 (true=生化风格, false=刀战一整局) */
    UPROPERTY(EditDefaultsOnly, Category = "Mode")
    bool bRoundBased = false;

    /** 模式回合数 (生效于 bRoundBased=true) */
    UPROPERTY(EditDefaultsOnly, Category = "Mode", meta = (ClampMin = "1"))
    int32 TotalRounds = 5;
};


/**
 * 将 EAIBlackboardKey 枚举映射到 BB 资产中的 FName
 * 设计: Subsystem 在 Initialize 时把枚举和 BB Key 一一对应
 *       任意读 Key 处均通过 Subsystem::ResolveKey(EAIBlackboardKey::TargetActor) 拿到 FName
 */
namespace AIBlackboardKeyNames
{
    constexpr const TCHAR* TargetActor       = TEXT("TargetActor");
    constexpr const TCHAR* NearbyThreat      = TEXT("NearbyThreat");    // 极近距离遭遇（替代旧 ImmediateTarget）
    constexpr const TCHAR* HomeLocation      = TEXT("HomeLocation");
    constexpr const TCHAR* LastKnownLocation = TEXT("LastKnownLocation");
    constexpr const TCHAR* bIsInCombat       = TEXT("bIsInCombat");
    constexpr const TCHAR* bHasAttackToken   = TEXT("bHasAttackToken");
    constexpr const TCHAR* PatrolPath        = TEXT("PatrolPath");
    constexpr const TCHAR* CurrentPhase      = TEXT("CurrentPhase");
    constexpr const TCHAR* PhaseTimer        = TEXT("PhaseTimer");

    static FName Get(EAIBlackboardKey Key)
    {
        switch (Key)
        {
        case EAIBlackboardKey::TargetActor:       return FName(TargetActor);
        case EAIBlackboardKey::NearbyThreat:      return FName(NearbyThreat);
        case EAIBlackboardKey::HomeLocation:       return FName(HomeLocation);
        case EAIBlackboardKey::LastKnownLocation:  return FName(LastKnownLocation);
        case EAIBlackboardKey::bIsInCombat:        return FName(bIsInCombat);
        case EAIBlackboardKey::bHasAttackToken:     return FName(bHasAttackToken);
        case EAIBlackboardKey::PatrolPath:         return FName(PatrolPath);
        case EAIBlackboardKey::CurrentPhase:       return FName(CurrentPhase);
        case EAIBlackboardKey::PhaseTimer:         return FName(PhaseTimer);
        default:                                   return NAME_None;
        }
    }
}
