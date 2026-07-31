// Copyright (c) 2026. All Rights Reserved.
//
// 【v107 2026.07.28 生化模式 AI】集合点账本 + 服务器权威选点 Subsystem
//
// 【v125 2026.08.01 v125 改动】新增"访问历史账本" + 反射式换点冷却 — 解决 A↔B 来回切换抖动
//
// 业务背景 (用户 2026.08.01 反馈 "AI 在 TriggerRadius 内 + 母体在 ThreatRadius 内, LockedRallyPoint 黑板键来回切换"):
//   v122-v124 实现的"反射式换点"逻辑没问题, 但 AI 在 A 集合点 ≤ TriggerRadius, 母体在 ThreatRadius
//   → 触发换点 → 选 B → AI 跑到 B 期间还在 A 附近 → 又触发换点 → 选 A → 死循环 A↔B 来回跳
//
// v125 修复 (大厂 - visited set + 反射式换点冷却):
//   1. VisitHistoryByController 账本 — 记录每个 AI 去过哪些集合点 + 上次访问时间
//   2. RecordRallyVisit() — 锁点成功后写入历史
//   3. SelectRallyPoint_Nearest_ReflexChange() — v124 升级版:
//      - ExcludePointID 必须是 BB.LockedRallyPoint (零值原则)
//      - 冷却窗口: ExcludePointID 上次锁定时间 < MinLockDurationBeforeReflexChange → 拒绝换点
//      - visited set: 已访问过的点加权 (距离 * VisitBiasMultiplier), 越久远惩罚越轻
//      - 稳定排序: 并列用 PointID 字典序
//   4. MinLockDurationBeforeReflexChange 默认 8s — 反射式换点最小停留时间
//   5. VisitBiasMultiplier 默认 4.0 — 已访问过的点距离惩罚 4 倍
//
// 设计原则 (大厂架构 — 单一职责 + 单一真理源 + 零兜底):
//   - 唯一职责: 集合点注册 + 锁点账本 + 人数统计 + 选点策略
//   - 真理源:
//     * RegisterRallyPoint() 写入时建账本 (Key = PointID, Value = TWeakObjectPtr<RallyPoint>)
//     * 锁定账本 LockedRallyByAI (Key = TWeakObjectPtr<Controller>, Value = PointID)
//   - 不复用 PlayerStart 账本 (职责分离 — 出生点 ≠ 集合点)
//   - 不持有渲染/UI (UI 走 URoomStateService 同思路)
//   - 不读 BB — BT 业务链在 BTTask_SelectZombieRallyPoint / BTService_UpdateZombieState
//
// 业务规则 (用户 2026.07.28 明确 / v117 2026.08.01 修订):
//   - 初始选点: 人类 AI 选择"最近集合点"并锁定账本 (账本 = 记录当前选点)
//   - 当 AliveMotherCount > AliveHumanCount 时, 改用"当前人类最多集合点"
//   - 默认 PeriodicReselect = 每 0.25s 重新选点 (账本同步, AI 出生即可移动)
//   - 旧 LockOnce 模式保留为选项 (BT 编辑器可选)
//   - 失败: Log Error + 返回失败标志 (不允许默认分配当前位置 / 不允许随机兜底)
//
// 调用方:
//   - BTTask_SelectZombieRallyPoint (选点+锁定, BT 决策)
//   - URoomMotherMutationSubsystem (人数快照, 已实现在 GetAliveHumanCount)
//   - 关卡预放: BeginPlay 时 Actor 自动注册 (本 Subsystem 提供 Register API)

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "RoomZombieRallySubsystem.generated.h"

class AZombieRallyPoint;
class ABaseCharacter;
class AController;
class UAIBehaviorConfigSO;

/**
 * 【v125 2026.08.01】单条 AI 访问历史记录 — USTRUCT
 *
 * 字段:
 *   - LastVisitTimeByPoint: PointID → 上次访问时间 (World->GetTimeSeconds())
 *
 * 用途:
 *   - 冷却窗口: SelectRallyPoint_Nearest_ReflexChange 查 ExcludePointID 的时间戳
 *   - visited set: 选点时给"已访问过"的点加权
 *
 * ★ 必须定义在 URoomZombieRallySubsystem class 之前 — UHT 要求 UPROPERTY 引用的
 * USTRUCT 在生成代码时已注册(hash 已计算)。class 后定义 = 0 hash = UHT 编译错。
 * (与 v31.5 同样的 .generated.h include 严格性原则一致)
 */
USTRUCT()
struct FRallyPointVisitRecord
{
	GENERATED_BODY()

	/** PointID → 上次访问时间戳 (World->GetTimeSeconds()) */
	UPROPERTY()
	TMap<FString, float> LastVisitTimeByPoint;
};

/**
 * 【v107 2026.07.28】僵尸模式集合点账本 + 选点 Subsystem
 *
 * 单一职责:
 *   - 注册集合点 (RegisterRallyPoint, 关卡预放 Actor 自动调)
 *   - 锁定账本 (LockRallyPointForAI / UnlockRallyPointForAI / GetLockedRallyPoint)
 *   - 人数统计 (CountAliveHumanNearPoint, 选"人类最多点"用)
 *   - 选点策略 (SelectRallyPoint_Nearest / SelectRallyPoint_MostPopulated)
 *
 * 不做:
 *   - 不持有渲染/UI (UI 走 URoomStateService)
 *   - 不读 BB (BT Service/Task 写 BB)
 *   - 不动 Pawn (移动归 BT 原生 MoveTo)
 *   - 不写文件/不网络复制 (账本服务器本地用即可, 客户端不读)
 */
UCLASS()
class METALSLUG01_API URoomZombieRallySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * 标准 UE Subsystem 访问入口
	 */
	static URoomZombieRallySubsystem* Get(const UObject* WorldContextObject);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	// ==========================================
	// 注册 (关卡预放 Actor 自动调)
	// ==========================================

	/**
	 * 注册一个集合点到账本
	 *
	 * @param Point 集合点 Actor (必须有效, 非空)
	 * @return 注册成功 (true=加入账本, false=被拒, 已记录错误)
	 *
	 * 大厂原则 — 零兜底:
	 *   - PointID 为空 → Log Error + 返回 false
	 *   - PointID 重复 (账本里已有) → Log Error + 返回 false (拒绝, 不覆盖)
	 *   - Point->PopulationRadius <= 0 → Log Error + 返回 false
	 *   - 全通过 → 加入账本 (TWeakObjectPtr<RallyPoint>)
	 *
	 * 调用方:
	 *   - AZombieRallyPoint::BeginPlay 自动调 (关卡预放自注册)
	 *   - 运行时动态生成 (将来扩展: 关卡脚本生成新集合点)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Zombie|Rally")
	bool RegisterRallyPoint(AZombieRallyPoint* Point);

	/**
	 * 注销一个集合点 (Point 销毁前调)
	 *
	 * 大厂原则:
	 *   - 不允许注销被 AI 锁定的点 (返回 false + Log Error)
	 *   - 这是大厂"账本完整性" — 锁点不允许凭空消失
	 *
	 * 调用方:
	 *   - AZombieRallyPoint::EndPlay 自动调
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Zombie|Rally")
	bool UnregisterRallyPoint(AZombieRallyPoint* Point);

	// ==========================================
	// 锁定账本 (BTTask_SelectZombieRallyPoint 调)
	// ==========================================

	/**
	 * 【v117 2026.08.01】将一个集合点登记到账本给指定 AI Controller (账本语义)
	 *
	 * @param Controller AI Controller (玩家或 AI 都行, 用 Weak 引用防 GC)
	 * @param PointID 集合点 ID (账本 key)
	 * @return 登记成功 (true=账本已更新, false=PointID 不在账本里)
	 *
	 * 大厂原则 (v117 修订):
	 *   - 幂等: 同一 Controller 锁同一 Point → 成功 (不重复写, Verbose Log)
	 *   - 允许迁移: PeriodicReselect 模式下 Controller 锁不同 Point → 覆盖旧记录 (账本同步)
	 *   - 业务上的"一局只锁一次"约束, 由 BTTask_SelectZombieRallyPoint 节点的 ReselectPolicy = LockOnce 节点决定
	 *   - 账本登记语义 = "记录当前选点", 不含"不让改"的强约束
	 *
	 * 历史 (v107 旧设计):
	 *   - Controller 锁不同点 → Log Error + return false (大厂"一局内只选一次"约束)
	 *   - v117 修订: PeriodicReselect 模式下这是合规操作, 改为覆盖
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Zombie|Rally")
	bool LockRallyPointForAI(AController* Controller, const FString& PointID);

	/**
	 * 查询 AI 锁定的集合点
	 *
	 * @return 锁定 PointID (空 = 未锁定, BT 应先去 BTTask_SelectZombieRallyPoint)
	 */
	UFUNCTION(BlueprintPure, Category = "Room|Zombie|Rally")
	FString GetLockedRallyPointID(AController* Controller) const;

	/**
	 * 解除锁定 (AI 死亡/换阵营时调, 可选 — 当前业务不需要)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Zombie|Rally")
	bool UnlockRallyPointForAI(AController* Controller);

	// ==========================================
	// 人数统计 (选点策略用)
	// ==========================================

	/**
	 * 统计某个集合点附近 (PopulationRadius) 的存活人类数
	 *
	 * @param Point 集合点
	 * @return 存活人类数 (>=0, Point 无效返回 0)
	 *
	 * 大厂原则:
	 *   - 严格定义: !IsDead() && !bIsMother (镜像 GetAliveHumanCount)
	 *   - 距离算法: 平面距离 (Z 轴忽略) — 楼层差异不应影响"附近"判定
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Room|Zombie|Rally")
	int32 CountAliveHumanNearPoint(AZombieRallyPoint* Point) const;

	// ==========================================
	// 选点策略 (BTTask_SelectZombieRallyPoint 调)
	// ==========================================

	/**
	 * 选点: 最近集合点 — 人类初始选点策略
	 *
	 * @param QueryingCharacter 选点查询方 (用于计算距离)
	 * @return 选中集合点 (nullptr = 无可用, BTTask 应 Log Error + Failed)
	 *
	 * 大厂原则:
	 *   - 距离算法: 平面距离 (Z 轴忽略)
	 *   - 并列最近时: 按 PointID 字典序升序 (稳定排序, 避免来回切换)
	 *   - 账本空 → return nullptr + 调用方负责 Log Error
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Zombie|Rally")
	AZombieRallyPoint* SelectRallyPoint_Nearest(ABaseCharacter* QueryingCharacter) const;

	/**
	 * 选点: 当前人类最多集合点 — 当 AliveMotherCount > AliveHumanCount 时的策略
	 *
	 * @param QueryingCharacter 选点查询方 (并列时用距离破局)
	 * @return 选中集合点 (nullptr = 无可用)
	 *
	 * 大厂原则:
	 *   - 人数统计: 严格 CountAliveHumanNearPoint(每个点)
	 *   - 并列人数多: 距离最近 (同 SelectRallyPoint_Nearest 破局)
	 *   - 并列距离: PointID 字典序升序
	 *   - 账本空 → nullptr
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Zombie|Rally")
	AZombieRallyPoint* SelectRallyPoint_MostPopulated(ABaseCharacter* QueryingCharacter) const;

	/**
	 * 【v120 2026.08.01】选点: 最近集合点 (排除指定 PointID) — 反射式换点专用
	 *
	 * @param QueryingCharacter 选点查询方 (用于计算距离)
	 * @param ExcludePointID 必须排除的集合点 ID (例如当前 LockedRallyPoint)
	 * @return 选中集合点 (nullptr = 账本空 / 排除后无其他点 / 唯一集合点被排除)
	 *
	 * 业务背景:
	 *   BTService_ReflexRallyChange 在"AI 距离集合点 100cm 内 + 母体在 50cm 内"时触发换点
	 *   - 选出来的必须是"另一个"集合点, 否则换点无意义
	 *   - 复用 SelectRallyPoint_Nearest 的距离算法 + 稳定排序 (PointID 字典序)
	 *
	 * 大厂原则 (与 SelectRallyPoint_Nearest 对称):
	 *   - 距离算法: 平面距离 (Z 轴忽略)
	 *   - 并列最近: 按 PointID 字典序升序 (稳定排序)
	 *   - 账本空 → nullptr
	 *   - 排除后空 → nullptr (调用方负责 Log Error 提示"地图集合点不足")
	 *
	 * 调用方:
	 *   - BTService_ReflexRallyChange (v120 新增)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Zombie|Rally")
	AZombieRallyPoint* SelectRallyPoint_Nearest_Excluding(ABaseCharacter* QueryingCharacter, const FString& ExcludePointID) const;

	/**
	 * 【v125 2026.08.01】反射式换点专用 — v124 升级版 (visited set + 冷却窗口)
	 *
	 * @param QueryingCharacter 选点查询方 (用于计算距离)
	 * @param Controller 当前 AI Controller (用于查/写访问历史)
	 * @param ExcludePointID 必须排除的集合点 ID (BB.LockedRallyPoint.PointID)
	 * @param VisitBiasMultiplier 已访问过的点距离加权倍数 (推荐 4.0, 1.0 = 无惩罚)
	 * @return 选中集合点 (nullptr = 冷却中 / 账本空 / 排除后无其他点)
	 *
	 * v125 vs v124 主要差异:
	 *   1. 冷却窗口: ExcludePointID 在 VisitHistory 中 < MinLockDurationBeforeReflexChange 秒 → return nullptr
	 *      (防止 A↔B 来回切换的根因: AI 锁 A, 0.1s 后又到 A 附近,又触发换点选 B)
	 *   2. visited set: 已访问过的点距离 × VisitBiasMultiplier 倍 Score
	 *      - 越久远 (AgeFactor→1.0) → bias 越接近 1.0 → 越不惩罚
	 *      - 越近 (AgeFactor→0.0) → bias = VisitBiasMultiplier → 大幅惩罚
	 *   3. 稳定排序: 并列 Score 用 PointID 字典序
	 *
	 * 大厂原则 (与 SelectRallyPoint_Nearest_Excluding 对称):
	 *   - 距离算法: 平面距离 (Z 轴忽略)
	 *   - 账本空 → nullptr
	 *   - 排除后空 → nullptr
	 *   - 冷却中 → nullptr (返回时内部 Log Verbose, 不刷屏)
	 *
	 * 调用方:
	 *   - BTService_ReflexRallyChange (v125 替换 v124 调用)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Zombie|Rally")
	AZombieRallyPoint* SelectRallyPoint_Nearest_ReflexChange(
		ABaseCharacter* QueryingCharacter,
		AController* Controller,
		const FString& ExcludePointID,
		float VisitBiasMultiplier) const;

	/**
	 * 【v125 2026.08.01】记录 AI 访问某个集合点 (visited set 写入)
	 *
	 * @param Controller AI Controller
	 * @param PointID AI 刚锁定的集合点 ID
	 *
	 * 写入时机:
	 *   - BTService_ReflexRallyChange::PerformReflexChange 写 BB 成功后立即调
	 *   - 第一次"出生即选"也算 (BTTask_SelectZombieRallyPoint 锁账本后调)
	 *
	 * 大厂原则:
	 *   - 幂等: 重复写入同一 PointID → 更新时间戳 (用于冷却窗口判定)
	 *   - 失效保护: Controller 销毁 → TWeakObjectPtr 自动失效 → 后续访问被清理
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Zombie|Rally")
	void RecordRallyVisit(AController* Controller, const FString& PointID);

	/**
	 * 【v125 2026.08.01】公共访问历史权重 — 已访问过的点距离倍率 (推荐 4.0)
	 *
	 * 大厂原则:
	 *   - 1.0 = 无惩罚 (纯按距离)
	 *   - 大于 1.0 = 越近访问过的点越不优先
	 *   - 默认 4.0: 已访问过的点距离视为实际 2 倍 (平方后是 4 倍 Score)
	 *
	 * 为什么是 public (不是 protected):
	 *   - 这是"配置真理源", 外部 BTService_ReflexRallyChange 需要读 (0 = 用本字段)
	 *   - BTService 编辑器面板上的 VisitBiasMultiplier 字段 override 本字段
	 *   - public 是大厂标准 — Config / Settings 字段都要 public, 业务方才能读取
	 *
	 * 与 MinLockDurationBeforeReflexChange 区别:
	 *   - DefaultVisitBiasMultiplier 是 public (外部读, 0=用本字段)
	 *   - MinLockDurationBeforeReflexChange 是 protected (Subsystem 内部节流参数, 不允许外部 override)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Zombie|Rally", meta = (ClampMin = "1.0"))
	float DefaultVisitBiasMultiplier = 4.0f;

	/**
	 * 查询所有已注册集合点 (只读视图, 调试用)
	 *
	 * 大厂原则: 返回 TArray 副本 (避免外部修改账本)
	 */
	UFUNCTION(BlueprintPure, Category = "Room|Zombie|Rally")
	TArray<AZombieRallyPoint*> GetAllRallyPoints() const;

	/**
	 * 查集合点是否已注册
	 */
	UFUNCTION(BlueprintPure, Category = "Room|Zombie|Rally")
	bool IsRallyPointRegistered(const FString& PointID) const;

	/**
	 * 通过 PointID 拿集合点 Actor (BTTask 拿到锁点后要拿 Actor 写入 BB.Object)
	 */
	UFUNCTION(BlueprintPure, Category = "Room|Zombie|Rally")
	AZombieRallyPoint* GetRallyPointByID(const FString& PointID) const;

protected:
	/**
	 * 集合点账本 — PointID → TWeakObjectPtr
	 *
	 * 真理源 (服务器本地):
	 *   - RegisterRallyPoint 写入
	 *   - UnregisterRallyPoint 删除 (但锁定账本还有引用时拒绝)
	 *
	 * 大厂原则 — 弱引用:
	 *   - TWeakObjectPtr 自动失效 (Actor 销毁后 Get() 返回 nullptr)
	 *   - 选点策略遍历时跳过失效项
	 */
	UPROPERTY()
	TMap<FString, TWeakObjectPtr<AZombieRallyPoint>> RallyPointsByID;

	/**
	 * 锁定账本 — Controller → PointID
	 *
	 * 真理源 (服务器本地):
	 *   - LockRallyPointForAI 写入
	 *   - UnlockRallyPointForAI 删除 (业务一般不调, 一局锁定)
	 *
	 * 大厂原则:
	 *   - TWeakObjectPtr<AController> 自动失效 (Controller 销毁后 Get() 返回 nullptr)
	 *   - 选点/查账时跳过失效项
	 *   - Key 用 TWeakObjectPtr 而不是裸指针, 防 GC 后悬挂
	 */
	UPROPERTY()
	TMap<TWeakObjectPtr<AController>, FString> LockedRallyByAI;

	/**
	 * 【v125 2026.08.01】AI 访问历史账本 — 集合点选点偏好 (visited set)
	 *
	 * 真理源 (服务器本地):
	 *   - RecordRallyVisit 写入
	 *   - 自动清理: TWeakObjectPtr<Controller> 失效后, 访问历史也随之失效
	 *
	 * 大厂原则:
	 *   - TWeakObjectPtr 作为 Key → Controller 销毁后自动失效, 不会悬挂
	 *   - LastVisitTimeByPoint 时间戳 → 形如 (RallyA → 12.3s, RallyB → 5.1s)
	 *   - 冷却窗口: SelectRallyPoint_Nearest_ReflexChange 查 ExcludePointID 的时间戳
	 *   - visited set: 选点时给"已访问过"的点加权 (距离 × VisitBiasMultiplier)
	 *
	 * 业务决策 (用户 2026.08.01 反馈):
	 *   "就算之前呆过的也行, 优先没呆过的" → 用 TimeSince 体现偏好度
	 *   (TimeSince 越近 → bias 越大 → 越不优先)
	 */
	UPROPERTY()
	TMap<TWeakObjectPtr<AController>, FRallyPointVisitRecord> VisitHistoryByController;

	/**
	 * 【v125 2026.08.01】反射式换点冷却窗口 (秒) — 同一集合点锁定后 N 秒内禁止再换走
	 *
	 * 大厂原则:
	 *   - EditAnywhere: 策划可在 Subsystem CDO 调, 也可在派生 BP 调
	 *   - 抖动防护: 防止 A↔B 来回切换的核心 — Service 0.2s Tick 内不会因冷却期内反复触发换点
	 *   - 默认 8s: 给 AI 足够时间"跑到"新点 + 母体移动 + 状态稳定
	 *
	 * 业务背景:
	 *   旧 (v122-v124) 没设冷却 → BTService 0.2s Tick, 条件一直满足 → 0.2s 换一次 → 来回切换
	 *   新 (v125): 刚锁 B → 8s 内 Service 不会触发"从 B 换走" → 不会 A↔B 来回
	 *
	 * 为什么是 protected:
	 *   - 这是 Subsystem 内部配置, 外部不直接读 (与 DefaultVisitBiasMultiplier 区别: 后者是"公共真理源", 前者是"内部节流参数")
	 *   - 冷却时长由 Subsystem 自身和 SelectRallyPoint_Nearest_ReflexChange 决定, 外部 BTService 不应该绕开
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Zombie|Rally", meta = (ClampMin = "0.0"))
	float MinLockDurationBeforeReflexChange = 8.0f;
};