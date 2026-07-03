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

// EAIHuntStrategy 已在下方正式定义 (见 FAIHuntPolicy 前)

UENUM(BlueprintType)
enum class EAIBlackboardKey : uint8
{
	TargetActor     UMETA(DisplayName = "TargetActor"),     // 仲裁分配的锁定目标（由 BTService_RefreshTarget 写入）
	NearbyThreat    UMETA(DisplayName = "NearbyThreat"),     // 极近距离遭遇的敌人（AttackRange 范围内时写入）
	bIsInCombat     UMETA(DisplayName = "bIsInCombat"),     // 是否在战斗中
	// 【P0 2026.07.09 架构重构】删除 bHasAttackToken
	//   原用途: BTService_UpdateBlackboard 周期写 Token (上帝 Service 反模式)
	//   现在: BTDecorator_CooldownReady 直接实时读 World.Time vs BB.CooldownEndTime, 不需要中间态
	//   保留枚举条目仅作兼容: 旧 BP / Sequence 仍可能引用, 编译期给个明显提示
	//   后续 P1 清理: BB_AI_Melee.uasset 删除这个 Key 后, 本条目可彻底删除
	bHasAttackToken_DEPRECATED UMETA(Hidden, DisplayName = "bHasAttackToken (DEPRECATED - 2026.07.09, 用 Decorator_CooldownReady 替代)"),
	// 【P0 2026.07.06】C++ 与 BT 通信: BT Attack task 内部标志 - C++ TickChaseFallback 看到此标志才停止 AddMovementInput
	bIsAttackingNow UMETA(DisplayName = "bIsAttackingNow"),
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
 * 战斗参数 - AI 战斗相关数值集中管理
 *
 * 【P0 2026.07.07 大厂架构】距离参数合并
 *   原 OverrideBTDistance 字段已删除 — 用户原话:
 *     "OverrideBTDistance 其实可以就用 AttackRange 参数代替即可,
 *      进入 AttackRange 武器攻击范围,就是极近距离"
 *   现 NearbyThreat 触发距离 = AttackRange (见 ABaseAIController::UpdateNearbyThreatByDistance)
 *   语义: AttackRange 同时承担 (a) AI 停下距离 (b) 攻击触发距离 (c) NearbyThreat 阈值
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
    float AttackCooldown = 5.f;

    /**
     * 【P0 大厂架构 2026.07.06 重构】AI 攻击基础伤害（默认值）
     *
     * 数据流 (修复后):
     *   FAICombatParams::Damage (策划配置)
     *     ↓ UAIRuntimeConfigComponent::GetScaledCombat (难度缩放: Hard=1.3x, Easy=0.8x)
     *     ↓ ABaseCharacter::OnAIRequestAttack_Simple (AI 攻击入口应用)
     *     ↓ Server_ReportHit_AI (新加的 RPC, 服务器权威)
     *     ↓ UGameplayStatics::ApplyPointDamage (扣血)
     *
     * 历史 (为什么之前没生效):
     *   旧版 AI 攻击路径只播动画 (PlayAnimMontage), 没调 StartWeaponTrace / Server_ReportHit
     *   → ConfigSO.Damage 永远不读, AI 攻击 0 伤害
     *   重构后: AI 攻击自动按 ConfigSO.Damage 扣血, 跟玩家攻击走相同的 Server 扣血流程
     *
     * 与武器 LightDamageBody 的关系 (大厂设计):
     *   - BaseWeapon.LightDamageBody = 武器固有属性 (这把刀砍谁都打 20)
     *   - AIBehaviorConfigSO.Damage = AI 行为修正 (这个 AI 用刀时, 无论啥刀, 都打 12)
     *   - 最终伤害 = DamageOverride (AI 传) > 0 ? DamageOverride : Weapon.LightDamageBody
     *   - 这是经典"行为参数化": 同一把刀给不同 AI, 伤害可以不同 (Boss 武器 = +50%, 杂兵 = -30%)
     */
    UPROPERTY(EditDefaultsOnly, Category = "Combat",
        meta = (ClampMin = "0.0", ClampMax = "1000.0"))
    float Damage = 12.f;

    /**
     * 【P0 2026.07.06】AI 攻击爆头伤害（可选）
     *
     * 设计: 大厂 AI 行为配置会区分身体/爆头, 例如狙击手 AI 爆头 1.5x, 杂兵爆头 2.0x
     *       < 0 = 禁用爆头检测 (跟玩家一样不打头)
     *       = 0 = 用 Damage 作为爆头伤害 (1x, 等同身体)
     *       > 0 = 自定义爆头伤害值
     *
     * 当前实现状态: 数据已加, 实际爆头检测需要在 AI 攻击时做 Trace (下个 Phase)
     *              现在先让 OnAIRequestAttack_Simple 用 Damage (身体) 扣血
     */
    UPROPERTY(EditDefaultsOnly, Category = "Combat",
        meta = (ClampMin = "-1.0", ClampMax = "1000.0"))
    float DamageHeadshot = -1.f;

    /** 弹药类型 (GameplayTag) - 留给武器系统 Phase 3 接入 */
    UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (Categories = "Weapon.AmmoType"))
    FGameplayTag AmmoType;

    /**
     * 【P0 2026.07.07 大厂架构重构】攻击时是否允许移动
     *
     * 用途: 控制 AI 攻击蒙太奇播放期间是否允许移动
     *
     * 行为:
     *   - true (默认, 向后兼容): 攻击中仍可渐进减速到位 (ComputeArrivalDecision 正常决策)
     *   - false: 攻击蒙太奇播放期间强制 LockStop, 只有 OnMontageEnded 回调后恢复移动
     *
     * 典型场景:
     *   - bAllowMovementDuringAttack = true:  "冲锋型" AI, 边冲边砍 (如 Boss 冲刺攻击)
     *   - bAllowMovementDuringAttack = false: "站桩型" AI, 站定后才砍 (如近战杂兵)
     *
     * 单一数据源: ConfigSO.Combat.bAllowMovementDuringAttack → AIRuntimeConfigComponent.GetScaledCombat()
     *             任何调用方 (ComputeArrivalDecision / BTTask / 蓝图调试) 走同一路径, 无状态分裂
     */
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    bool bAllowMovementDuringAttack = true;

    /**
     * 【P0 大厂架构 2026.07.07 加固】冷却期间距离维持的迟滞缓冲 (厘米)
     *
     * 设计意图:
     *   解决 AI 在 AttackRange 边界处抖动问题 — 在 [AR-Hyst, AR+Hyst] 区间不调整,
     *   只有超出迟滞区间才触发 Chase / 后退,避免帧间反复切换方向
     *
     *   - 玩家距离从 250 → 240 时: 仍在迟滞区, AI 保持原行为 (不抖动)
     *   - 玩家距离从 240 → 200 时: 离开迟滞区, AI 立即开始后退
     *
     * v13 用户反馈 2026.07.07 19:30 — 字段删除:
     *   "AttackRangeHysteresis 感觉没什么用啊, 删掉吧"
     *   "正常后退即可, 无需这个参数"
     *
     * v17 用户反馈 2026.07.07 20:20 — 修正 v13 删除判断:
     *   "现在都没问题了, 只有一个问题, 就是 ai 会原地走动"
     *   → 用 C++ 硬编码 30cm 暂时绕开 (字段未恢复)
     *
     * v18 用户反馈 2026.07.07 20:35 — 真正的根因暴露:
     *   "AI 会走到敌人面前但不攻击, 必须玩家朝向 AI 走一点路才能被攻击"
     *   根因: v17 Hyst=30 硬编码让边界上沿变成 AR+30=210, AI 在 200cm 距离时
     *         就 LockStop, 不追到 180cm 攻击范围 → BTTask 永远不触发
     *   用户明确诉求: "把 Hyst 暴露到 ConfigSO 出来, 我好方便调试"
     *   → v18 重新加回字段 (用户请求), 默认 0 (与 v13 用户初衷一致),
     *     Refractory Period 200ms 引擎层兜底抗抖动, 但 Hyst 可调
     *
     * v18 移除原因 (大厂原则 — 减熵) → 已撤回:
     *   - ConfigSO 字段层**重新暴露** (v18 用户明确请求"我好方便调试")
     *   - 默认 = 0 (与 v13 用户 19:30 原话"正常后退即可,无需这个参数" 一致)
     *   - 引擎层 Refractory Period 200ms 仍保留抗抖动
     *   - 用户调试时可以填任意正值 (如 10~30cm) 试迟滞区间效果
     *
     * v18 行为:
     *   - Hyst = ConfigSO.Combat.AttackRangeHysteresis (默认 0, 可调)
     *   - 边界区间 [AR-Hyst, AR+Hyst]: D ∈ 区间 → LockStop
     *   - 跨区间决策翻转由 Refractory Period (200ms, 扩展为任何决策切换) 抑制
     *   - 默认值 = 0 时, 与 v13 完全一致 (单点 AR 边界, 抗抖动靠 Refractory)
     *
     * 对 AI 行为的可观察影响 (Hyst=0 默认):
     *   - 单点 AR 边界: D < AR → Chase(-0.7), D == AR → 立刻 LockStop
     *   - AI 永远会追到 AR 边界 (不会卡在 AR+Hyst 远处不动)
     *   - 抗抖动由 Refractory Period (200ms) 兜底 (不依赖 Hyst)
     *   - 用户可以填 5~30 测试不同的迟滞效果
     */
    // v18 恢复 — 用户 2026.07.07 20:35 反馈"AI 走到敌人面前但不攻击, 把 Hyst 暴露 ConfigSO 出来",
    //              Hyst=30 硬编码让 AI 卡在 AR+30=210 处不动, 无法进入 AR=180 攻击范围.
    //              默认 0 (与 v13 用户 19:30 原意"正常后退即可"一致), 用户可调试时填 5~30 测试.
    UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0.0", ClampMax = "100.0",
        ToolTip = "AttackRange boundary hysteresis (cm). Default 0 means sharp boundary. Set 5-30 to test debouncing. Refractory Period (200ms) provides engine-level anti-jitter regardless of this value."))
    float AttackRangeHysteresis = 0.f; // v18 恢复 (v13 删, v17 C++ 硬编码, v18 用户明确请求重新暴露)
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

// 【P0 2026.07.07 大厂架构清理】FAIHuntPolicy 暂保留 — RoomGameMode.cpp FAIModeRules 间接引用了此类型
//   已确认: RequestTargetForAI 零调用方, 属于潜在死代码, 但当前被依赖链约束, 暂保留

/**
 * 目标选择策略枚举 — 被 FAIHuntPolicy 引用
 */
UENUM(BlueprintType)
enum class EAIHuntStrategy : uint8
{
    NearestDistance      UMETA(DisplayName = "最近距离"),
    RandomValid        UMETA(DisplayName = "随机有效"),
    HighestScore       UMETA(DisplayName = "最高积分"),
    TimeAttitudeWeighted UMETA(DisplayName = "剩余时间加权"),
    Custom             UMETA(DisplayName = "自定义扩展"),
};

/**
 * 目标选择策略配置 — Phase 2 模式化
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
 *
 * 【P0 2026.07.09 架构重构】
 *   - 冷却 BB Key "CooldownEndTime" 仍保留 (BTTask_PlayAttackMontage 一次性写, Decorator_CooldownReady 实时读)
 *   - "bHasAttackToken" 字符串常量保留 (兜底兼容, BTService 残留代码若还在读就返回空 FName 等同清空)
 *   - 强烈建议未来 P1 清理: 从 BB_AI_Melee.uasset 删除 "bHasAttackToken" Key 后, 此字符串常量可彻底移除
 */
namespace AIBlackboardKeyNames
{
    // 原始事实 (BTService 派生)
    constexpr const TCHAR* TargetActor      = TEXT("TargetActor");
    constexpr const TCHAR* DistanceToTarget = TEXT("DistanceToTarget");
    constexpr const TCHAR* bHasTarget      = TEXT("bHasTarget");
    constexpr const TCHAR* AttackRange     = TEXT("AttackRange");

    // 派生事实 (BTService 派生)
    constexpr const TCHAR* HealthPercent   = TEXT("HealthPercent");
    constexpr const TCHAR* CooldownEndTime = TEXT("CooldownEndTime");

    // C++ 与 BT 通信
    constexpr const TCHAR* bIsAttackingNow = TEXT("bIsAttackingNow");

    // 【v22】精简: AttackRangeMin / AttackRangeMax 已从 BB 移除, 决策改由 C++ Decorator 内计算
}
