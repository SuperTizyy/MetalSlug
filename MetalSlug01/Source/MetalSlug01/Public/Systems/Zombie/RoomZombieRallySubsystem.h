// Copyright (c) 2026. All Rights Reserved.
//
// 【v107 2026.07.28 生化模式 AI】集合点账本 + 服务器权威选点 Subsystem
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
// 业务规则 (用户 2026.07.28 明确):
//   - 初始选点: 人类 AI 选择"最近集合点"并永久锁定 (一局内只选一次)
//   - 当 AliveMotherCount > AliveHumanCount 时, 改用"当前人类最多集合点"
//   - 已锁定 AI 不迁移 (写完 LockedRallyByAI 后永不重新选)
//   - 并列时按距离最近, 再按 PointID 字典序稳定排序
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
	 * 锁定一个集合点给指定 AI Controller
	 *
	 * @param Controller AI Controller (玩家或 AI 都行, 用 Weak 引用防 GC)
	 * @param PointID 集合点 ID (账本 key)
	 * @return 锁定成功 (false = PointID 不在账本里)
	 *
	 * 大厂原则:
	 *   - 幂等: 同一 Controller 锁同一 Point → 成功 (不重复写)
	 *   - 不允许 Controller 锁不同点 (一局内只选一次, 拒绝迁移)
	 *     第二次锁不同点 → Log Error + return false (强制策划修 BT — 应当 Decorator 拒判而不是重选)
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
};