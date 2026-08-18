// ==========================================
// 【v117 大厂架构新增】URoomAirdropSubsystem
//
// ==========================================
// 【生化模式】空投系统 — 单一权威调度
// ==========================================
//
// 业务规则 (用户 2026.08.03 明确):
//   - 每小局空投降临倒计时结束后, 把场景中"还没被人类吃掉的空投"全部销毁
//   - 然后在 GameMode 预设的 N 个空投点位处生成新空投
//   - 生成位置 = 点位位置 + (0, 0, AirDropPickupDropHeight), 营造下落感
//   - 只有人类可以吃掉空投, 母体走过去无视 (业务层判定, 不污染 CollisionProfile)
//   - 被吃掉的人类玩家主武器所有弹药恢复全满 (弹夹 + 总子弹容量)
//
// 业务规则 (用户 2026.08.17 明确):
//   - 每小局结束, 场景里没被吃掉的空投必须销毁 (v2xx 修复)
//   - 防止"空投残留"干扰下一小局视觉 / 玩家走位
//
// 大厂原则 — 单一真理源:
//   - 空投点位:        ARoomGameMode::AirDropPoints              (策划在 BP 配置)
//   - 空投蓝图类:      ARoomGameMode::AirDropPickupClass         (策划在 BP 配置)
//   - 下落高度:        ARoomGameMode::AirDropPickupDropHeight     (策划在 BP 配置, 默认 100cm)
//   - 弹药真理源:      UWeaponFireComponent::MagazineSize + InitialReserveAmmo (单一 DT 行, v117 新增)
//   - 倒计时触发:      URoomLifecycleSubsystem::NotifyAirdropArrivalCompleted → SpawnAirdropAtAllPoints
//
// 大厂原则 — 职责分层:
//   - URoomAirdropSubsystem:   管"何时/在哪/生成多少" + 账本管理 (服务器权威)
//   - AAirdropPickup:          管"碰撞响应/被谁吃/RPC 自销毁" (单一 Actor 自治)
//   - UWeaponFireComponent:    管"弹药全满" (单一真理源, v117 新增 Server_RefillAmmo)
//   - URoomLifecycleSubsystem: 管"倒计时到期 = 触发空投降临" (v117 新增 AirdropIntervalTimerHandle)
//   - URoomLifecycleSubsystem: 管"小局结束 = 清理未消化空投" (v2xx 新增, FinishZombieRound 集中调度)
//
// 大厂原则 — 单一职责 (v2xx 重构):
//   - SpawnAirdropAtAllPoints: 只 Spawn, 不清理 (旧版 v117 反模式已修复)
//   - DestroyAllExistingPickups: 只清理, 不生成 (账本 = 单一真理源, 调用方决策时机)
//   - 调用方决策:
//     - FinishZombieRound (小局结束, 用户 2026.08.17 业务规则)
//     - OnAirdropIntervalExpired (空投降临轮换, 用户 2026.08.03 业务规则)
//
// 大厂原则 — 零兜底:
//   - 缺空投点位/类/高度配置 → Log Error + 拒绝 Spawn (强制策划修复)
//   - 母体碰到 → Verbose 日志 + 跳过 (业务规则, 不是兜底)
//   - 死亡人类碰到 → Verbose + 跳过 (死人不能吃)
//   - 没主武器/没 FireComp → Log Error + 不静默吃空投 (强制修复武器 Spawn 链路)
//   - AirdropSubsystem 不可用 → 上游 (LifecycleSubsystem) Log Error + 显式化
//
// 大厂原则 — 不破坏刀战:
//   - ShouldCreateSubsystem 镜像 v31.5 风格 — 不在刀战模式 (EWorldType 兼容 + GameWorld)
//   - GameMode.AirDropPoints / AirDropPickupClass 留空 → 整个 Subsystem 等同不工作
//   - 刀战模式永不调用本 Subsystem (LifecycleSubsystem 模式守卫)
//
// 调用链:
//   LifecycleSubsystem::StartAirdropCountdown → SetTimer(AirdropInterval) → 到期调
//   LifecycleSubsystem::OnAirdropIntervalExpired
//     ├─ [v2xx] DestroyAllExistingPickups (清理, 显式)
//     ├─ [v2xx] SpawnAirdropAtAllPoints (生成, 显式)
//     └─ NotifyAirdropArrivalCompleted 继续下一轮倒计时
//
//   LifecycleSubsystem::FinishZombieRound (小局结束)
//     └─ [v2xx 新增] DestroyAllExistingPickups (清理, 单一入口)
//
// ==========================================

#pragma once

// UE 引擎基础
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

// 大厂注意 (UE 5.6 UHT 严格模式): .generated.h 必须用裸文件名
#include "RoomAirdropSubsystem.generated.h"

// 前向声明 — AAirdropPickup 完整类型在 cpp 内 include (避免循环依赖)
class AAirdropPickup;

/**
 * @file RoomAirdropSubsystem.h
 * @brief 生化模式空投子系统 — 服务器权威调度 (大厂架构)
 *
 * @class URoomAirdropSubsystem
 * @brief 生化模式空投系统 — 服务器权威单一调度
 *
 * 设计原则:
 *   - server-only WorldSubsystem (镜像 URoomLifecycleSubsystem 风格, v31.5)
 *   - 不持有 Actor 引用缓存以外的状态 (账本数组除外)
 *   - 账本 TrackedPickups 是"服务器权威真理源", 不复制到客户端 (客户端通过 Actor 自身 Replicated 看到)
 */
UCLASS()
class METALSLUG01_API URoomAirdropSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * 标准 UE Subsystem 访问入口 (大厂原则)
	 * @param WorldContextObject 任何能拿到 World 的 UObject (通常传 this)
	 * @return 当前 World 的 AirdropSubsystem 实例 (server-only)
	 */
	static URoomAirdropSubsystem* Get(const UObject* WorldContextObject);

	// UWorldSubsystem 接口
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/**
	 * 【v2xx 大厂架构重构】服务器公开 API — 在所有预设点生成新一批空投
	 *
	 * 调用方 (唯一, 1 个, 大厂原则 — 集中调度):
	 *   - URoomLifecycleSubsystem::OnAirdropIntervalExpired (空投降临倒计时到期)
	 *     → 走 AirdropIntervalTimer 到期 → 调本函数生成新空投
	 *
	 * 单一职责 (v2xx 大厂原则):
	 *   - 本函数只 Spawn 新空投, 不清理旧空投
	 *   - 旧版 (v117-v201) 反模式: 内部 Step 1 调 DestroyAllExistingPickups → 职责错位
	 *   - 新版: 清理 = 调用方决策 (OnAirdropIntervalExpired / FinishZombieRound 各自决定)
	 *   - 调用方示例 (OnAirdropIntervalExpired):
	 *       AirdropSys->DestroyAllExistingPickups();   // 清旧
	 *       AirdropSys->SpawnAirdropAtAllPoints();     // 生新
	 *
	 * 业务规则 (用户 2026.08.03):
	 *   - 遍历 GameMode.AirDropPoints 匹配 Tag 的 Actor 列表
	 *   - 生成坐标 = 点位 Actor 世界坐标 + (0, 0, AirDropPickupDropHeight)
	 *   - 账本 = TrackedPickups (本 Subsystem 唯一真理源)
	 *   - 生成后通过 Multicast_PlayDropSound 推客户端音效
	 *
	 * 大厂原则 — 零兜底:
	 *   - World / GameState / GameMode 为空 → Log Error + return 0
	 *   - 非生化模式 → 静默 Log (业务禁用, 不报错)
	 *   - AirDropPoints 为空数组 → Log Warning + return 0 (业务禁用, 不静默)
	 *   - AirDropPickupClass 未配 → Log Error + return 0
	 *   - 单个点位 SpawnActor 失败 → Log Error + continue (不阻塞后续点位)
	 *   - DropSound 字段为空 → Log Warning + 跳过音效 (业务可容忍)
	 *
	 * @return 成功生成数量 (供调用方日志)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Airdrop")
	int32 SpawnAirdropAtAllPoints();

	/**
	 * 【v2xx 大厂架构重构】清理当前所有未消化的空投
	 *
	 * 调用方 (唯一, 2 个, 大厂原则 — 集中调度):
	 *   1. URoomLifecycleSubsystem::FinishZombieRound (小局结束, 用户 2026.08.17 业务规则)
	 *      → 走 OnMatchTimerTick (倒计时归零 / 提前结束) → 触发小局结算
	 *   2. URoomLifecycleSubsystem::OnAirdropIntervalExpired (空投降临轮换)
	 *      → 走 AirdropIntervalTimer 到期 → 清理旧空投 + 生成新空投
	 *
	 * 大厂原则 — 单一真理源:
	 *   - 清理 = 单一入口, 不允许"顺带清理"嵌入 Spawn 内部 (v117-v201 反模式)
	 *   - 业务调用方决策清理时机, AirdropSubsystem 只负责执行
	 *   - 跨越账号者 AirdropSubsystem 内部, 账本 = TrackedPickups (弱引用)
	 *
	 * 大厂原则 — 显式清理:
	 *   - 拷贝待销毁清单 → 清空账本 → 逐个 Destroy (防 v117 断言崩)
	 *   - 不依赖 GC, 立即销毁
	 *   - 不静默容忍 IsActorBeingDestroyed (同帧二次 Destroy 跳过)
	 *
	 * 大厂原则 — 零兜底:
	 *   - AirdropSubsystem 不可用 (World 为空等) → 已在调用方日志化, 本函数不重复防御
	 *   - 账本为空 → 清理是 no-op (有 Log 体现, 不算静默)
	 *
	 * 不破坏既有调用方:
	 *   - 旧版 (v117) 的"HandlerZombieRoundEnd 兜底清理 / GameMode::HandleMatchTimeOut 兜底清理"
	 *     注释完全是谎言 (调用方根本不存在, 导致小局结束空投残留)
	 *   - v2xx 改为"集中调度 + 显式清理", 注释与实现 100% 对齐
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Airdrop")
	void DestroyAllExistingPickups();

	/**
	 * 【v117 账本同步接口】AAirdropPickup 被吃掉 / 销毁时回调, 把自己从账本里移除
	 *
	 * 调用方: AAirdropPickup::Handle_PickedUp / AAirdropPickup::EndPlay
	 *
	 * 大厂原则 — 账本自维护:
	 *   - Actor 自己负责告诉 Subsystem "我不再活着"
	 *   - Subsystem 不主动 IsValid 检查 (避免重复扫描)
	 *   - 重复 RemoveUnique 幂等
	 */
	void NotifyPickupDestroyed(AAirdropPickup* Pickup);

	/**
	 * 【v117 查询】当前空投账本 (供 GameMode 调试 / 测试用)
	 */
	const TArray<TWeakObjectPtr<AAirdropPickup>>& GetTrackedPickups() const { return TrackedPickups; }

protected:
	/**
	 * 【v117 账本】服务器权威持有 — 场景中所有当前活跃空投的弱引用
	 *
	 * 大厂原则 — 弱引用 (TWeakObjectPtr):
	 *   - UE GC 清理 Actor 时自动失效, IsValid() 返回 false
	 *   - 不阻止 Actor 销毁, 不破坏 UE 生命周期
	 *   - 镜像 URoomSpawnSubsystem::OccupiedSpawnByController 的弱引用模式
	 *
	 * 不复制:
	 *   - 客户端不需要账本, 它们直接看到 World 里的 Actor (Actor 自身 Replicated)
	 */
	TArray<TWeakObjectPtr<AAirdropPickup>> TrackedPickups;
};