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
//
// 大厂原则 — 零兜底:
//   - 缺空投点位/类/高度配置 → Log Error + 拒绝 Spawn (强制策划修复)
//   - 母体碰到 → Verbose 日志 + 跳过 (业务规则, 不是兜底)
//   - 死亡人类碰到 → Verbose + 跳过 (死人不能吃)
//   - 没主武器/没 FireComp → Log Error + 不静默吃空投 (强制修复武器 Spawn 链路)
//
// 大厂原则 — 不破坏刀战:
//   - ShouldCreateSubsystem 镜像 v31.5 风格 — 不在刀战模式 (EWorldType 兼容 + GameWorld)
//   - GameMode.AirDropPoints / AirDropPickupClass 留空 → 整个 Subsystem 等同不工作
//
// 调用链:
//   LifecycleSubsystem::StartAirdropCountdown → SetTimer(AirdropInterval) → 到期调
//   LifecycleSubsystem::OnAirdropIntervalExpired → AirdropSubsystem::SpawnAirdropAtAllPoints
//     ├─ DestroyAllExistingPickups() (账本里的 + World 里残留的)
//     ├─ 遍历 AirDropPoints 生成 Pickup
//     └─ NotifyAirdropArrivalCompleted 继续下一轮倒计时
//
// ==========================================

#pragma once

// UE 引擎基础
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

// 大厂注意 (UE 5.6 UHT 严格模式): .generated.h 必须用裸文件名
#include "RoomAirdropSubsystem.generated.h"

class AAirdropPickup;

/**
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
	 * 【v117 单一入口】服务器公开 API — 在所有预设点生成新一批空投
	 *
	 * 调用方: URoomLifecycleSubsystem::OnAirdropIntervalExpired (SetTimer 到期回调)
	 *
	 * 业务规则 (用户 2026.08.03):
	 *   1. 先 DestroyAllExistingPickups() 把上一轮空投清掉
	 *   2. 然后遍历 GameMode.AirDropPoints 生成新空投
	 *   3. 生成坐标 = 点位 Actor 的世界坐标 + (0, 0, AirDropPickupDropHeight)
	 *
	 * 大厂原则 — 零兜底:
	 *   - World / GameState / GameMode 为空 → Log Error + return
	 *   - AirDropPoints 为空数组 → Log Warning + return (业务禁用, 不静默)
	 *   - AirDropPickupClass 未配 → Log Error + return
	 *   - 单个点位 SpawnActor 失败 → Log Error + continue (不阻塞后续点位)
	 *
	 * @return 成功生成数量 (供调用方日志)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Airdrop")
	int32 SpawnAirdropAtAllPoints();

	/**
	 * 【v117 单一入口】清理当前所有未消化的空投
	 *
	 * 调用方:
	 *   - SpawnAirdropAtAllPoints 第 1 步 (新空投生成前)
	 *   - HandleZombieRoundEnd (本局结束, 兜底清理)
	 *   - GameMode::HandleMatchTimeOut (整场比赛结束, 兜底清理)
	 *
	 * 大厂原则 — 显式清理:
	 *   - 遍历 TrackedPickups (账本) 调 Destroy()
	 *   - 清空账本
	 *   - 不依赖 GC, 立即销毁
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