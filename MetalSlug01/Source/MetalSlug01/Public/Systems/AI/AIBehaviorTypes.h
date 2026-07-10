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
#include "BehaviorTree/BehaviorTree.h"   // 【v54.4 编译修复】TSoftObjectPtr<UBehaviorTree>::Get() 需要完整定义 (dynamic_cast)
#include "AIBehaviorTypes.generated.h"

// 【v54 大厂架构重构】删除 UAIProfileAsset 前置声明 (类已删除)
// 【v51 大厂重构】前置声明 ABaseCharacter — 用于 FAISpawnRequest.AIPawnClass 字段
// 【v54.1 大厂架构新增】前置声明 AAIController — TSubclassOf<AAIController> 模板参数只需要前向声明
class ABaseCharacter;
class AAIController;

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
	// 【v40.8 2026.07.13 漫游支持】漫游目标点 (BTTask_FindRandomLocation 写入, BTTask_MoveTo 读取)
	WanderTarget    UMETA(DisplayName = "WanderTarget"),
	// 【v40.8 2026.07.13 漫游支持】漫游出生点 (Possess 时写入, 整个会话不变, 用于以该点为中心漫游)
	WanderHome      UMETA(DisplayName = "WanderHome"),
	// 【v40.9 2026.07.13 拟人化环绕】环绕点 (BTTask_PickCirclePoint 写入, BTTask_MoveToCirclePoint 读取)
	CirclePoint     UMETA(DisplayName = "CirclePoint"),
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

    /**
     * 【v54.2 移除 bDetectNeutrals】项目内只有 Offense/Defense 二元阵营, 没有中立单位
     *   旧 (v53 之前): bDetectNeutrals=true 时 AI 把中立单位当敌人攻击
     *   新 (v54.2 之后): 字段删除 — 大厂原则: 删除"无用字段" 比"硬编码 false" 更彻底
     *                    BaseAIController.cpp 已显式设 SightConfig->DetectionByAffiliation.bDetectNeutrals=false
     */

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

    /**
     * 【v40.9 2026.07.13 大厂架构 — 攻击后环绕拟人化】
     *
     * 业务需求:
     *   "每次 AI 攻击完敌人后,需要围绕此敌人走几步,再攻击,让 AI 攻击更拟人化"
     *
     * 设计 (大厂原则 — BT 决策 + C++ 原子能力):
     *   - 行为树 Sequence: Attack → 击完 → 选环绕点 → MoveTo → 停顿 → 循环
     *   - 这 3 个参数分别给 BTTask_PickCirclePoint / BTTask_MoveToCirclePoint / BTTask_WaitCirclePause 读
     *   - 单一真理源: DataAsset → AIRuntimeConfigComponent → BTTask 读 (跟其他参数同一链路)
     *
     * 详细字段:
     *   - bEnableCircle: 总开关 (策划可以关掉 → 退化回原版"打 → 打 → 打")
     *   - StrafeRadius: 环绕半径 (cm) — 选点时绕目标多远
     *   - CirclePauseSeconds: 选完点站多久 (秒) — 拟人感来自这个停顿
     */
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Circle", meta = (
        ToolTip = "Enable attack-then-circle behavior. false = fall back to original 'attack-attack-attack'."))
    bool bEnableCircle = true;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Circle", meta = (ClampMin = "50.0", ClampMax = "500.0",
        ToolTip = "Distance (cm) from target where circle point is selected. 80~150 recommended."))
    float StrafeRadius = 120.f;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Circle", meta = (ClampMin = "0.0", ClampMax = "3.0",
        ToolTip = "Seconds to stand still after reaching circle point. Mimics human 'recover' rhythm."))
    float CirclePauseSeconds = 0.4f;
};

/**
 * 移动参数 - 后续 NavMesh / 寻路统一入口
 */
USTRUCT(BlueprintType)
struct FAIMovementParams
{
	GENERATED_BODY()

	/**
	 * 【v42 2026.07.14】AI 移动速度 (cm/s) — 唯一速度参数
	 *
	 * 设计: AI 所有移动 (前进/后退/追逐) 统一走此参数.
	 *       不再区分 WalkSpeed/RunSpeed 两个字段.
	 *
	 * 配置:
	 *   - 在 DA_AIBehaviorConfig_XXX → Movement → WalkSpeed 配置
	 *   - 典型值: 200~600 cm/s
	 *
	 * 调用方:
	 *   - ABaseAIController::OnPossess: 初始化 MaxWalkSpeed
	 *   - ABaseAIController::LockMovementForCooldown: Unlock 时恢复
	 *   - ABaseAIController::ComputeArrivalDecision: 前进/后退都用此速度
	 *
	 * 【零兜底】如果 WalkSpeed <= 0, AI 不移动, Log Error 强制修复配置
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float WalkSpeed = 250.f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float AcceptanceRadius = 60.f;

	/**
	 * 【v40.8 2026.07.13 漫游支持】漫游半径 (cm)
	 * 用途: 无目标时 AI 在以 WanderHome 为中心的半径范围内随机选点漫游
	 * 数据源: DataAsset (单一真理源), 通过 AIRuntimeConfigComponent.GetScaledMovement() 派生
	 * BT 节点 BTTask_FindRandomLocation 读此值 → 调 NavMesh.GetRandomReachablePointInRadius
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = "100.0", ClampMax = "5000.0"))
	float WanderRadius = 800.f;
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
 * 【2026.07.11 v28 大厂架构重构】大厅阶段 AI 占位
 *
 * 设计意图:
 *   旧实现 AddAIToRoom 立刻生成 Pawn + AIController, 但:
 *     1. AIController 默认没有 PlayerState → UI (Box_AttackTeam/Box_DefenseTeam) 看不到 AI
 *     2. 大厅阶段玩家没在场景里, AI 站在场景里空摆 → 浪费内存 + 容易误判"复活失败"
 *     3. 与新业务规则"开始游戏在阵营复活点生成"冲突
 *
 *   新实现 (v28):
 *     1. 大厅阶段: 房主 UI 点击"添加 AI [攻方 x3]" → QueueAIForBattleSpawn 只把请求入队
 *     2. URoomStateService 读 GameMode.PendingAIQueue → 渲染到 Box_AttackTeam (作为占位)
 *     3. 开始游戏: SpawnAllPlayersIntoBattle 消费 PendingAIQueue → 真正 Spawn AIController + Pawn
 *     4. 战斗阶段: 死而复生复用同一 AIController (v24 不销毁 Controller),不影响 UI 显示 (Box 已隐藏)
 *
 * 大厂原则:
 *   - 显式意图: Queue 入队是 "预订", Spawn 入场是 "兑现", 两个动作完全分离
 *   - 单一真理源: PendingAIQueue 只在 GameMode, QueueAIForBattleSpawn/SpawnAllPlayersIntoBattle 是唯一读写入口
 *   - 零兜底: 字段全部 BlueprintReadOnly (外部只能读, 不能直接改 Queue)
 */
USTRUCT(BlueprintType)
struct FPendingAIEntry
{
    GENERATED_BODY()

    /**
     * AI 唯一标识 — 用于 UI 显示 (例如 "AI_GruntAI_1")
     * 【大厂原则】一旦入队, DisplayName 不可变, AI 复活时也用同一个名字
     */
    UPROPERTY(BlueprintReadOnly, Category = "PendingAI")
    FString DisplayName;

    /**
     * 【单一真理源】AI 阵营 — FGameplayTag (Faction.Offense / Faction.Defense)
     * 大厂原则: 阵营在 Queue 阶段就确定, 战斗 Spawn 时直接读这个值, 不再从 Profile 反推
     */
    UPROPERTY(BlueprintReadOnly, Category = "PendingAI")
    FGameplayTag FactionTag;

    /**
     * 【v54 大厂架构重构 — 删除 ProfileTag 字段】
     *
     * 历史 (v49-v53):
     *   - 旧字段保留兼容, 原注释: "由 AIPawnClass 反查 Profile 替代"
     *
     * v54 重构:
     *   - UAIProfileAsset 已删除, ProfileTag 没有意义
     *   - 关卡预放 AI 走 ConfigSO (DA_AIBehaviorConfig_*)
     *   - 大厅入队 AI 的 Profile 信息全部由 Pawn->GetClass() + ConfigSO 决定 (Pawn 实际 Class 决定用哪份 ConfigSO)
     */

    /**
     * 【v49 重构】角色 RowName (DT_CharacterInfo)
     *
     * UI 选中的角色在 DT_CharacterInfo 的 RowName
     * SpawnAIInternal 用这个查 DT_CharacterInfo → 拿 CharacterBlueprint (真实 Pawn Class)
     * Pawn Class 持 ConfigSO (v54 直接接 ConfigSO, 不再反查 DA_AIProfile_*)
     */
    UPROPERTY(BlueprintReadOnly, Category = "PendingAI")
    FName CharacterInfoRowName = NAME_None;

    /**
     * 【v51 大厂架构重构 — 单一真理源】AI Pawn Class 强类型
     *
     * UI 选中的角色反查 DT_CharacterInfo.CharacterBlueprint 拿到的 Pawn Class
     * (例如 BP_GruntAI_C). BuildSpawnRequestFromPending 时直接拷给 Request.AIPawnClass.
     *
     * 这是真理源, 不需要再从 CharacterInfoRowName 反查 (v50 之前是 SpawnAIInternal 反查).
     */
    UPROPERTY(BlueprintReadOnly, Category = "PendingAI")
    TSubclassOf<class ABaseCharacter> AIPawnClass;

    /** 武器 ID (DT_WeaponInfo 的 RowName) — Spawn 时传给 Pawn */
    UPROPERTY(BlueprintReadOnly, Category = "PendingAI")
    FString WeaponID;

    /** 模式 (Melee / Zombie) — Spawn 时传给 SpawnAIInternal */
    UPROPERTY(BlueprintReadOnly, Category = "PendingAI")
    ERoomMatchMode Mode = ERoomMatchMode::None;

    /**
     * 【v55.1 大厂架构修复】Config 字段必须透传
     *
     * 根因 (v55 Bug):
     *   - FPendingAIEntry 缺少 Config 字段
     *   - BuildSpawnRequestFromPending 不拷贝 Config
     *   - SpawnAIInternal 收到 Config=null → 走 ModeRules.BehaviorTree
     *   - BP 里 ModeRules.BehaviorTree 为空 → 拒绝 Spawn
     *
     * 修复:
     *   - FPendingAIEntry 新增 Config 字段 (BlueprintReadOnly)
     *   - QueueAIForBattleSpawn 填入 Config
     *   - BuildSpawnRequestFromPending 拷贝 Config
     *
     * 真理源: DT_CharacterInfo[RowName].ConfigSoftRef (UI 入队时反查)
     */
    UPROPERTY(BlueprintReadOnly, Category = "PendingAI")
    TObjectPtr<class UAIBehaviorConfigSO> Config;

    /**
     * 队列序号 — 用于 UI 显示顺序 (Box_AttackTeam 里从上到下)
     * 大厂原则: 用入队顺序而不是 ListIndex (ListIndex 在 RemoveAt 时会跳)
     */
    UPROPERTY(BlueprintReadOnly, Category = "PendingAI")
    int32 SequenceID = 0;
};

/**
 * 【Phase 2 模式化】AI Spawn 参数包
 * 设计:
 *   - 替代 RoomGameMode::AddAIToRoom 里的散参数 (bToAttackTeam, CharacterName, Count)
 *   - 加 spawn 模式 + 角色标签 + 数量
 *   - 由 GameMode 内部构造, 调用方只填业务字段
 *
 * 【2026.07.10 P0 重构】Team 字段从 ERoomTeam 改为 FGameplayTag
 *   - 业务上只有"攻方/守方"两态, 用 FGameplayTag 表达 (Faction.Offense / Faction.Defense)
 *   - 阵营 Tag 由调用方从 ModeRulesByMode 中取 (不再有全局"哪个 Tag 是攻方"的硬编码)
 *
 * 【v49 大厂架构重构 — 单一真理源】
 *   - 删除 `CharacterRowName` (FName): 之前用 "斯沃特AI" 这个 UI 显示名反查 Profile, 错位且脆弱
 *     替代方案: UI 选中的角色名直接用作查 DT_CharacterInfo 的 RowName,
 *               SpawnAIInternal 拿 FCharacterInfo.CharacterBlueprint → 拿实际 Pawn Class
 *               Pawn Class 就是 Profile 查找的单一真理源
 *   - 删除 Profile 反查路径: 不再通过 "CharacterRowName == Profile.CharacterRowName" 找 Profile
 *     替代方案: SpawnAIInternal 通过 AIPawnClass → 反查 DT_CharacterInfo.CharacterBlueprint 匹配
 */
USTRUCT(BlueprintType)
struct FAISpawnRequest
{
	GENERATED_BODY()

	/** 来源模式 - 决定用哪条 Profile */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Spawn")
	ERoomMatchMode Mode = ERoomMatchMode::None;

	/**
	 * 【2026.07.10 P0 重构】目标阵营 Tag — 替代 ERoomTeam
	 * 有效值: Faction.Offense / Faction.Defense
	 * 由调用方从 ModeRulesByMode[Request.Mode] 取 AttackTeamFaction / DefenseTeamFaction
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Spawn")
	FGameplayTag FactionTag;

    /**
     * 【v49 新增】角色 RowName (DT_CharacterInfo 的 RowName)
     *
     * 这是 UI 选中的角色在 DT_CharacterInfo 中的 RowName (例如 "MeleeGruntAI001")
     * 通过查 DT_CharacterInfo.CharacterBlueprint 拿到真实 Pawn Class (BP_GruntAI_C)
     *
     * 必须显式填写 (零兜底): 调用方必须从 ComboBox 选中后, 反查 RowName 传入
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Spawn")
    FName CharacterInfoRowName = NAME_None;

    /**
     * 【v51 大厂架构重构 — 单一真理源】AI Pawn Class 强类型
     *
     * 真理源链 (BP 强类型, 不再依赖字符串反查):
     *   UI ComboBox_AICharacter 选 CharacterName
     *      ↓ 反查 DT_CharacterInfo[CharacterName]
     *   RowName + CharacterBlueprint (TSoftClassPtr<ABaseCharacter>)
     *      ↓ LoadSynchronous → AIPawnClass (BP_GruntAI_C)
     *   传给 SpawnAIInternal → 直接 Spawn(AIPawnClass)
     *
     * 删除的旧路径 (v50 之前):
     *   - SpawnAIInternal 内部 DT_CharacterInfo 反查 PawnClass (重复架构 + 反查失败静默)
     *   - SpawnAllPlayersIntoBattle 外面再做一次反查 (重复架构)
     *
     * 单一真理源 (大厂原则 v51 落地):
     *   - AIPawnClass 由 UI 反查时直接拿到, 一路传到 Spawn (不再重复)
     *   - DT_CharacterInfo 反查 PawnClass 在 UI 阶段完成一次, 不在 Spawn 链路再查
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Spawn")
    TSubclassOf<class ABaseCharacter> AIPawnClass;

    /**
     * 【v55 大厂架构重构 — 标记为已废弃】
     *
     * 历史 (v51-v54):
     *   - 曾作为 AI AIController Class 强类型的来源 (UI 显式传入)
     *
     * v55 重构 (用户决策 2026.07.16):
     *   - 大厅入队 AI 改走 ModeRulesByMode[Mode].AIControllerClass (单一真理源)
     *   - Request.AIControllerClass 不再被 SpawnAIInternal 读取
     *
     * 大厂原则 (零兜底):
     *   - 保留字段 (不删除, 避免破坏序列化)
     *   - 移除 BlueprintReadOnly (废弃字段不应暴露给蓝图)
     *   - SpawnAIInternal 内部不再读取此字段
     */
    TSubclassOf<class AAIController> AIControllerClass_DEPRECATED;

    /**
     * 【v47 大厂架构新增】武器 ID (查 DT_WeaponInfo 的 RowName, 例如 "WQ001")
     *
     * UI 选中的武器显示名 (FWeaponInfo.WeaponName) 反查 RowName 传入
     *
     * 大厂原则 (v51 落地 — 单一真理源):
     *   - WeaponID 是真理源 (字符串, 与 DT_WeaponInfo RowName 一致)
     *   - WeaponAttachmentComponent 内部走 DT_WeaponInfo[WeaponID] → WeaponBlueprint (Class)
     *   - 再用 Class + Owner->GetClass() → DT_WeaponAttachmentConfig[TargetCharacter, WeaponBlueprint]
     *   - UI 不需要直接拿 Class — 真理源仍然是字符串 WeaponID, 反查在武器挂载时自然完成
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Spawn")
    FString WeaponID;

    /**
     * 【v54.2 大厂架构新增 — 真理源完整传递】AI 行为配置 DataAsset
     *
     * 用户原话 2026.07.16:
     *   "这个字段应该是所有ai的复活无敌期, 不管是否预放还是生成"
     *   → 所有 AI 路径 (关卡预放 / 大厅入队 / 复活) 走同一个真理源 ConfigSO.SpawnInvincibilitySeconds
     *   → 大厅入队 AI 也必须显式填 Config (RoomService 反查 DT_CharacterInfo.ConfigSoftRef 拿到)
     *
     * 真理源链:
     *   - DT_CharacterInfo[CharacterName] 必有一行, 行内有 ConfigSoftRef 字段
     *   - RoomService 反查时 LoadSynchronous 拿到 ConfigSO 实例
     *   - 写入 Request.Config 透传给 SpawnAIInternal
     *   - 关卡预放 AI 走另一条路径 (BaseAIC.GetConfig() 在 Possess 之后注入)
     *
     * 零兜底:
     *   - 字段不许为 nullptr (RoomService 拿不到就 Log Error + 拒绝入队)
     *   - SpawnAIInternal: Config=null 时, 仅允许关卡预放路径 (旧 AI 复活场景), 大厅 AI 必须有 Config
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Spawn")
    TObjectPtr<class UAIBehaviorConfigSO> Config;

    // 【v54 大厂架构重构 — 删除 ProfileTag 字段】
    // 历史 (v49-v53):
    //   - 这是 UI 选中的 AI 类型 (FGameplayTag, 例如 AI.Profile.MeleeGrunt)
    //   - 通过查 ProfilesByMode[Request.Mode].Profiles[ProfileTag] 直接拿到 Profile 资产
    //   - Profile 是 AIController 配置 (感知+BT+阵营), 与武器挂载链 (Class 强类型) 完全分离
    //
    // v54 重构 (用户决策 2026.07.16):
    //   - UAIProfileAsset 整个类已删除, ProfileTag 没有意义
    //   - 关卡预放 AI 走 ConfigSO (DA_AIBehaviorConfig_*.uasset)
    //   - 大厅入队 AI 走 Request.AIPawnClass + Request.WeaponID + Request.FactionTag (UI 直接给)
    //   - Request 不再传 ProfileTag, 也不需要 Profile 反查

	/** 生成数量 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Spawn", meta = (ClampMin = "1"))
	int32 Count = 1;

	/** 是否覆盖出生点 (true=用系统默认, false=按队伍分配) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Spawn")
	bool bUseTeamSpawnPoint = true;
};

/**
 * 【v54 大厂架构重构 — 删除】FAIProfileRegistry
 *
 * 历史 (v53 之前):
 *   - 持有 TMap<FGameplayTag, TSoftObjectPtr<UAIProfileAsset>> Profiles
 *   - GameMode 通过 ProfilesByMode[Mode].Profiles[ProfileTag] 反查 Profile
 *   - 这是 Profile 中间层的一部分 (RoomSpawnSubsystem::ResolveProfileExact 链路)
 *
 * v54 重构 (用户决策 2026.07.16):
 *   - UAIProfileAsset 整个类已删除
 *   - 既然没有 Profile 资产, Profile 注册表也没必要存在
 *   - 关卡预放 AI 直接从 ConfigSO 读默认武器/AIController
 *   - 大厅 AI 不需要 Profile 注册表 (Request 直接传所有需要的参数)
 *   - FAIProfileRegistry 整个 USTRUCT 已删除
 *
 * 【v54 删除】原来 FAIModeRules.DefaultProfiles 字段也删除 (依赖此 USTRUCT)
 */

    /**
     * 【v55 大厂架构重构 — FAIModeRules 新增 AIControllerClass 字段】
     *
     * 重构内容:
     *   - 新增 AIControllerClass 字段 (大厅 AI 的 Controller 类型)
     *   - DefaultControllerClass 从 RoomGameMode.h 删除 (GM 全局默认值违反"按模式配置"原则)
     *
     * 【Phase 2 模式化】模式专属规则配置
     * 设计:
     *   - GameMode 持 TMap<ERoomMatchMode, FAIModeRules>
     *   - 大厅 AI Spawn: SpawnAIInternal 从这里拿 AIControllerClass + BehaviorTree (按游戏模式决定)
     *   - 关卡预放 AI: ConfigSO.LevelPlacedAIControllerClass 决定 (见 UAIBehaviorConfigSO)
     *   - 这样关卡预放 AI 和大厅 AI 的 Controller 来源完全分离
     */
USTRUCT(BlueprintType)
struct FAIModeRules
{
    GENERATED_BODY()

    /**
     * 【v55 大厂架构重构】本模式大厅 AI 的默认 Controller 类
     *
     * 职责边界:
     *   - 大厅入队 AI (房主从 WBP_LANRoomPage UI 添加): 从这里拿 AIControllerClass
     *   - 关卡预放 AI (Level 里拖的 BP): 不走这里, 走 UAIBehaviorConfigSO.LevelPlacedAIControllerClass
     *
     * 配置路径 (UE 编辑器):
     *   GM_RoomGameMode → Class Defaults → ModeRulesByMode →
     *   → Melee: AIControllerClass = BP_MeleeAIController_C
     *   → Zombie: AIControllerClass = BP_ZombieAIController_C
     *
     * 大厂原则 (零兜底):
     *   - 大厅 AI Spawn 时 AIControllerClass 为空 → Log Error + 拒绝 Spawn (强制配置)
     *   - 关卡预放 AI 不走这里, ConfigSO.LevelPlacedAIControllerClass 缺失同样报错
     *   - 禁止从 Request.AIControllerClass 兜底 (那是旧版设计, 已废弃)
     *   - 禁止从 GM.DefaultControllerClass 兜底 (已删除 — 大厂原则: 每个模式的 Controller 必须显式配置)
     */
    UPROPERTY(EditDefaultsOnly, Category = "Mode|Controller",
        meta = (DisplayName = "AIControllerClass (大厅 AI 默认 Controller)"))
    TSubclassOf<class AAIController> AIControllerClass;

    /**
     * 【v54.4 大厂架构重构】本模式大厅 AI 的默认行为树
     *
     * 职责边界:
     *   - 大厅 AI (房主从 WBP_LANRoomPage UI 添加): 从这里拿 BT
     *   - 关卡预放 AI (Level 里拖的 BP): 不走这里, 走 UAIBehaviorConfigSO.LevelPlacedBehaviorTree
     *
     * 配置路径 (UE 编辑器):
     *   GM_RoomGameMode → Class Defaults → ModeRulesByMode →
     *   → Melee: BehaviorTree = BT_MeleeAI
     *   → Zombie: BehaviorTree = BT_ZombieAI (生化模式专用, 未来创建)
     *
     * 零兜底约定:
     *   - 大厅 AI Spawn 时 BehaviorTree.IsNull() → Log Error + 拒绝 Spawn (强制配置)
     *   - 关卡预放 AI 不走这里, ConfigSO.LevelPlacedBehaviorTree 缺失同样报错
     */
    UPROPERTY(EditDefaultsOnly, Category = "Mode|BehaviorTree",
        meta = (AllowedClasses = "/Script/AIModule.BehaviorTree",
                DisplayName = "BehaviorTree (大厅 AI 默认行为树, 按游戏模式配置)"))
    TSoftObjectPtr<UBehaviorTree> BehaviorTree;

    /**
     * 【2026.07.10 P0 重构】攻方阵营 Tag — 默认 Faction.Offense
     * 大厂原则: 默认值即业务首选值, 单一真理源 FFactionTags::Offense()
     * 调用方应从 ModeRulesByMode 读取本模式特定值, 此处仅作 fallback
     */
    UPROPERTY(EditDefaultsOnly, Category = "Mode|Faction",
        meta = (Categories = "Faction"))
    FGameplayTag AttackTeamFaction;

    /**
     * 【2026.07.10 P0 重构】守方阵营 Tag — 默认 Faction.Defense
     * 同上, 默认值即业务首选值
     */
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

    // 【v40.8 2026.07.13 漫游支持】
    constexpr const TCHAR* WanderTarget    = TEXT("WanderTarget"); // BTTask_FindRandomLocation 写入
    constexpr const TCHAR* WanderHome      = TEXT("WanderHome");   // Possess 时写入, 漫游中心

    // 【v40.9 2026.07.13 拟人化环绕】
    constexpr const TCHAR* CirclePoint     = TEXT("CirclePoint");  // BTTask_PickCirclePoint 写入, 攻击后选环绕点

    // 【v22】精简: AttackRangeMin / AttackRangeMax 已从 BB 移除, 决策改由 C++ Decorator 内计算
}
