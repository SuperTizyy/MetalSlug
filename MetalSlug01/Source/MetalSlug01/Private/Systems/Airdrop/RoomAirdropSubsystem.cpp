// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/Airdrop/RoomAirdropSubsystem.h"

// Engine includes
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h" // 【v117.2】TActorIterator
#include "GameFramework/Actor.h"

// Project includes
#include "Systems/RoomGameMode.h"
#include "Systems/RoomGameState.h"
#include "Pickups/AirdropPickup.h"

URoomAirdropSubsystem* URoomAirdropSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	if (UWorld* World = WorldContextObject->GetWorld())
	{
		return World->GetSubsystem<URoomAirdropSubsystem>();
	}
	return nullptr;
}

bool URoomAirdropSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// 大厂原则 — 镜像 v31.5 RoomLifecycleSubsystem / RoomSpawnSubsystem 风格
	//   - 只在 GameWorld 创建 (Editor Preview / PIE 不会创建)
	//   - server-only (clients 拿不到, 业务在服务器跑)
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	// 必须有 World
	if (!Cast<UWorld>(Outer))
	{
		return false;
	}

	return true;
}


// ==========================================
// 【v2xx 大厂架构重构】单一职责 — 在所有预设点生成新空投
// ==========================================
//
// 业务规则 (用户 2026.08.03):
//   - 遍历 GameMode.AirDropPointTags 匹配的所有 Actor (= BP_AirDropPoint 关卡实例)
//   - 在每个 PointActor 世界坐标 + (0, 0, AirDropPickupDropHeight) 处 SpawnActor
//   - 账本 = TrackedPickups (本 Subsystem 唯一真理源)
//   - 通过 Multicast_PlayDropSound 推客户端音效
//
// 大厂原则 — 单一职责 (v2xx 修复):
//   - 本函数只 Spawn, 不清理 (旧版 v117-v201 反模式已修复)
//   - 清理 = 调用方决策 (集中调度, 不耦合逻辑)
//   - 调用方: URoomLifecycleSubsystem::OnAirdropIntervalExpired (空投降临轮换)
//
// 大厂原则 — 零兜底:
//   - 配置缺 → Log Error + return 0 (强制策划修复)
//   - SpawnActor 失败 → Log Error + continue (不阻塞后续点位)
//   - DropSound 字段为空 → Log Warning + 跳过 (业务可容忍)
//
// ==========================================

int32 URoomAirdropSubsystem::SpawnAirdropAtAllPoints()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomAirdrop] SpawnAirdropAtAllPoints: World 为空, 拒绝生成."));
		return 0;
	}

	ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>();
	if (!RoomGS)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomAirdrop] SpawnAirdropAtAllPoints: GameState 为空, 拒绝生成. "
			     "【修复】检查 PerformGameStart 调用顺序."));
		return 0;
	}

	// 大厂原则 — 业务守卫: 仅生化模式生成空投
	if (RoomGS->CurrentMatchMode != ERoomMatchMode::Zombie)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[RoomAirdrop] SpawnAirdropAtAllPoints: 当前模式=%d, 非生化模式, 跳过."),
			static_cast<int32>(RoomGS->CurrentMatchMode));
		return 0;
	}

	// 大厂原则 — 仅服务器生成 (HasAuthority 守卫, 防止客户端被 RPC 误调)
	if (!World->GetAuthGameMode())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomAirdrop] SpawnAirdropAtAllPoints: AuthGameMode 为空, 当前不是服务器. 拒绝生成."));
		return 0;
	}

	ARoomGameMode* GameMode = Cast<ARoomGameMode>(World->GetAuthGameMode());
	if (!GameMode)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomAirdrop] SpawnAirdropAtAllPoints: GameMode 不是 ARoomGameMode, 拒绝生成."));
		return 0;
	}

// 大厂原则 — 零兜底: 缺配置显式化, 强制策划修复
	if (GameMode->AirDropPickupClass == nullptr)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomAirdrop] SpawnAirdropAtAllPoints: GameMode.AirDropPickupClass 未配置! "
			     "【修复】在 BP_GM_RoomGameMode.uasset → Class Defaults → Room|Match|Airdrop → AirDropPickupClass 拖入 BP_AirdropPickup 蓝图."));
		return 0;
	}

	if (GameMode->AirDropPointTags.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomAirdrop] SpawnAirdropAtAllPoints: GameMode.AirDropPointTags 为空 (策划未配置 Tag). 本次不生成新空投. "
			     "【业务可禁用 — 静默】"));
		return 0;
	}

	// ==========================================
	// 【v117.2 修复】扫描 World 中所有匹配 Tag 的 Actor 当空投点位
	// ==========================================
	//
	// 根因 (UE 5.6):
	//   - BP_GM_RoomGameMode 在 /Game/UI/ 路径, BP_AirDropPoint 关卡实例在
	//     /Game/Japanese_Temple/maps/Japanese_Temple_Demo/PersistentLevel
	//   - UE 5.6 禁止 Class Default 资产引用关卡 Place Actor (跨 Outer 引用)
	//   - 报错: "Illegal TEXT reference to a private object in external package"
	//   - 修复: GameMode 只持有 FName 字符串 (Tag), 运行时用 TActorIterator 扫描 World
	//
	// 大厂原则 — UE 5.6 标准做法:
	//   - 这是 LYRA / Fortnite 都在用的"Tag-based 配置"模式
	//   - 策划工作流: 关卡 BP_AirDropPoint (继承 ATargetPoint) → Details → Actor → Tags
	//     → 添加 "AirdropPoint" (FName)
	//   - BP_GM_RoomGameMode.AirDropPointTags 填 "AirdropPoint"
	//   - 运行时扫描, 匹配所有 ActorHasTag(AirdropPoint) 的 Actor
	//
	// 大厂原则 — 去重: 同一 Actor 可能匹配多个 Tag (策划在 AirdropPointTags 填了多个),
	//   必须去重, 否则同一空投点位生成多个空投
	TArray<AActor*> MatchedPoints;
	MatchedPoints.Reserve(8); // 预分配, 避免多次 Resize

	// 大厂原则 — SawActor 标记: TActorIterator 同帧扫描同一 Actor 多次时只计入一次
	//   实际上 TActorIterator 本身只走一次, 但跨多个 Tag 扫描时同一 Actor 会重复匹配
	//   → 用 TSet 去重是最简单的方案
	for (const FName& TagName : GameMode->AirDropPointTags)
	{
		bool bAnyMatch = false;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}

			if (Actor->ActorHasTag(TagName))
			{
				MatchedPoints.AddUnique(Actor);
				bAnyMatch = true;
			}
		}

		if (!bAnyMatch)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[RoomAirdrop] SpawnAirdropAtAllPoints: Tag '%s' 在 World 中没匹配任何 Actor. "
				     "【修复】关卡 BP_AirDropPoint → Details → Actor → Tags → 添加 '%s'."),
				*TagName.ToString(), *TagName.ToString());
		}
	}

	if (MatchedPoints.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomAirdrop] SpawnAirdropAtAllPoints: 所有 Tag 都没匹配到 Actor (共 %d 个 Tag). 本次不生成新空投. "
			     "【修复】检查关卡里 BP_AirDropPoint 是否打了 Actor Tag)."),
			GameMode->AirDropPointTags.Num());
		return 0;
	}

	// ==========================================
	// 【v2xx 大厂架构重构】单一职责 — 本函数只 Spawn, 不清理
	// ==========================================
	//
	// 旧版 (v117-v201) 反模式:
	//   - SpawnAirdropAtAllPoints 内部 Step 1 调 DestroyAllExistingPickups()
	//   - 看似"原子化"实则职责错位 (SRP 违反) — Spawn 函数偷偷清理
	//   - 隐藏清理时机 — 调用方以为只是 Spawn, 不知空投被清了
	//   - 注释声称的"HandleZombieRoundEnd 兜底清理 / GameMode::HandleMatchTimeOut 兜底清理"
	//     实际调用方根本不存在 → 小局结束空投残留 → 用户报告 bug
	//
	// 新版 (v2xx) 单一职责 + 显式优于隐式:
	//   - 本函数只 Spawn 新空投
	//   - 清理 = 调用方决策 (目前 2 个调用方):
	//     1. FinishZombieRound (小局结束, 走 URoomLifecycleSubsystem)
	//     2. OnAirdropIntervalExpired (空投降临轮换, 走 URoomLifecycleSubsystem)
	//   - 清理函数: DestroyAllExistingPickups (账本统一管理, 单一真理源)
	//
	// 大厂原则 — 零重复架构:
	//   - 不在 Spawn 函数内耦合清理 (那会变成"逻辑炸弹")
	//   - 不需要新增"强制清理" / "可选清理" 等新 API (避免接口膨胀)
	//
	// 大厂原则 — 调用方职责完整:
	//   - 调用方必须按业务需要决定是否先调 DestroyAllExistingPickups
	//   - 当前 OnAirdropIntervalExpired 路径: 清理 + 生成 连续执行 (v2xx 注释说明)
	//
	// 性能口径: 移除内部清理后, 本函数纯 Spawn 循环, 性能不变

	// 遍历所有匹配点位生成新空投
	const float DropHeight = GameMode->AirDropPickupDropHeight;
	int32 SuccessCount = 0;
	int32 FailCount = 0;

	for (int32 Index = 0; Index < MatchedPoints.Num(); ++Index)
	{
		AActor* PointActor = MatchedPoints[Index];
		if (!PointActor)
		{
			++FailCount;
			continue;
		}

		// 计算生成坐标 — 点位世界坐标 + (0, 0, DropHeight) 营造下落感
		const FVector SpawnLocation = PointActor->GetActorLocation() + FVector(0.0f, 0.0f, DropHeight);
		const FRotator SpawnRotation = FRotator::ZeroRotator;
		const FTransform SpawnTransform(SpawnRotation, SpawnLocation);

		FActorSpawnParameters SpawnParams;
		// 【v117.6 物理场景 Spawn】禁用碰撞自适应 — 物理模拟开启后, AdjustIfPossible 会让物理引擎瞬移
		//   - 旧版 (v117) AdjustIfPossibleButAlwaysSpawn: 重叠时微调位置, 但物理会"反弹"导致抖动
		//   - 新版: AlwaysSpawn — 强制在指定位置生成, 即使与其他 Actor 重叠
		//   - 物理引擎会自然把空投推开 (弹性碰撞), 这是 UE 5.6 标准物理行为
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.Owner = nullptr;
		SpawnParams.Instigator = nullptr;

		AAirdropPickup* NewPickup = World->SpawnActor<AAirdropPickup>(
			GameMode->AirDropPickupClass,
			SpawnTransform,
			SpawnParams);

		if (!NewPickup)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomAirdrop] SpawnAirdropAtAllPoints: SpawnActor 失败! Point=%s Location=%s Class=%s. "
				     "【修复】检查 BP_AirdropPickup 蓝图是否继承 AAirdropPickup, 或 Class 设置是否正确."),
				*PointActor->GetName(),
				*SpawnLocation.ToString(),
				*GameMode->AirDropPickupClass->GetName());
			++FailCount;
			continue;
		}

		// 大厂原则 — 配置回填: Actor 反向持 Subsystem 引用用于回调
		NewPickup->OwningSubsystem = this;

		// 写入账本 (账本是 Subsystem 权威状态, 不复制到客户端)
		TrackedPickups.Add(NewPickup);

		// 【v118 大厂架构新增】播放空投生成音效
		//   - 大厂原则 — 服务器权威 + Multicast 推送: 服务器 SpawnActor 成功 → 立即调 RPC
		//   - RPC Implementation 内每个客户端本地 PlaySoundAtLocation
		//   - 配错防护: DropSound 为空 → Log Warning + 跳过 (不强制, 配置错可容忍一次)
		if (NewPickup->DropSound)
		{
			NewPickup->Multicast_PlayDropSound(
				NewPickup->DropSound,
				NewPickup->DropSoundVolumeMultiplier,
				NewPickup->DropSoundPitchMultiplier);
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[RoomAirdrop] SpawnAirdropAtAllPoints: 空投 %s DropSound 字段为空, 跳过音效播放. "
				     "【修复】在 BP_AirdropPickup 蓝图 → Class Defaults → Airdrop → Drop Sound 字段拖入 Sound 资产."),
				*NewPickup->GetName());
		}

		UE_LOG(LogTemp, Display,
			TEXT("[RoomAirdrop] SpawnAirdropAtAllPoints: 生成空投[%d/%d] %s @ %s (Point=%s, DropHeight=%.1fcm)"),
			Index + 1,
			MatchedPoints.Num(),
			*NewPickup->GetName(),
			*SpawnLocation.ToString(),
			*PointActor->GetName(),
			DropHeight);

		++SuccessCount;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[RoomAirdrop] SpawnAirdropAtAllPoints: 本轮共生成 %d 个空投 (失败 %d 个, 匹配到 %d 个点位, 配置 %d 个 Tag)."),
		SuccessCount, FailCount, MatchedPoints.Num(), GameMode->AirDropPointTags.Num());

	return SuccessCount;
}


// ==========================================
// 【v117 单一入口】清理当前所有未消化的空投
// ==========================================

void URoomAirdropSubsystem::DestroyAllExistingPickups()
{
	// 【v117.5 大厂架构修复】先收集待销毁清单 → 清空账本 → 销毁 — 顺序不可错
	//
	// 根因 (Session 2026.08.03 断言崩):
	//   旧版 (v117) 在 for 循环里直接 Pickup->Destroy() → 触发 AAirdropPickup::EndPlay
	//     → NotifyPickupDestroyed → TrackedPickups.RemoveAll(...) ← 同步修改账本
	//     → 但外层 for 还在按原始 Index 倒序遍历
	//     → 当 Index=0 销毁后账本 size 已变 0, 下一次循环 Index=1 访问 TrackedPickups[1] 越界
	//     → Assertion: "Array index out of bounds: 1 into an array of size 1"
	//
	// 大厂原则 — 账本真理源分离:
	//   - 账本 (TrackedPickups) 是 Subsystem 数据真理, 不能让销毁流程反过来控制账本
	//   - 销毁流程是单向的: 读账本 → 拷贝待销毁清单 → 清账本 → 销毁
	//   - 即使 Actor::EndPlay 回调再 RemoveAll, 账本已空, no-op
	//
	// 大厂原则 — 拷贝而非引用:
	//   - 用 TWeakObjectPtr 拷贝避免悬空引用
	//   - AActor::Destroy() 同步触发 EndPlay, AActor 指针可能立即失效
	//   - 但 TWeakObjectPtr 在 Destroy 后 IsValid()=false, 自然跳过

	int32 DestroyedCount = 0;

	// Step 1: 拷贝待销毁清单 (弱引用快照, 防止原账本被修改后索引失效)
	TArray<TWeakObjectPtr<AAirdropPickup>> PickupsToDestroy = TrackedPickups;

	// Step 2: 立即清空账本 — 这是单一真理源
	//   - 即使 EndPlay 回调再调 NotifyPickupDestroyed, RemoveAll 在空数组上 no-op
	//   - 后续 Spawn 调用 AddUnique 也能正常运作
	TrackedPickups.Empty();

	// Step 3: 遍历待销毁清单, 调用 Destroy
	//   - 这里对账本的修改 (RemoveAll by EndPlay 回调) 已经 no-op (空数组)
	//   - 所以不再有越界风险
	for (const TWeakObjectPtr<AAirdropPickup>& WeakPickup : PickupsToDestroy)
	{
		AAirdropPickup* Pickup = WeakPickup.Get();
		if (!Pickup)
		{
			// 弱引用已失效 (GC 清掉的) — 跳过
			continue;
		}

		// 大厂原则 — IsPendingKill 守卫: 防止 UE 帧末销毁标记的 Actor 二次销毁
		if (Pickup->IsActorBeingDestroyed())
		{
			continue;
		}

		// 大厂原则 — OwningSubsystem 在 EndPlay 内会置 null, 这里先置避免 EndPlay 内 NotifyPickupDestroyed 二次触发
		//   - 不, 这里保留: 我们就是要 NotifyPickupDestroyed 走账本清理 (虽然已空, 也没问题)
		Pickup->Destroy();
		++DestroyedCount;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[RoomAirdrop] DestroyAllExistingPickups: 已清理 %d 个旧空投 (账本已清空)."),
		DestroyedCount);
}


// ==========================================
// 【v117 账本同步接口】Actor 死亡/销毁时回调, 把自己从账本里移除
// ==========================================

void URoomAirdropSubsystem::NotifyPickupDestroyed(AAirdropPickup* Pickup)
{
	if (!Pickup)
	{
		return;
	}

	const int32 RemovedCount = TrackedPickups.RemoveAll(
		[Pickup](const TWeakObjectPtr<AAirdropPickup>& WeakPickup)
		{
			// 弱引用 == 入参 或 弱引用失效 (GC 清掉的也清账本, 防止弱指针残留)
			AAirdropPickup* Resolved = WeakPickup.Get();
			return Resolved == Pickup || Resolved == nullptr;
		});

	if (RemovedCount > 0)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[RoomAirdrop] NotifyPickupDestroyed: 已从账本移除空投 %s (移除 %d 条)."),
			*Pickup->GetName(), RemovedCount);
	}
}