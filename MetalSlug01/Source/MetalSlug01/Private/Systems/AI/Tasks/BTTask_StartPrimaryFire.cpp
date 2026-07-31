// Copyright (c) 2026.
//
// 【v109 2026.07.30 生化模式 AI 大厂镜像方案】BTTask — 主武器开火
//
// 职责:
//   - 调 CurrentWeapon->Server_StartFire (Actor RPC,镜像玩家 IA_Fire 链路)
//
// 大厂原则 — 与玩家 BP_BaseCharacter 镜像:
//   - 玩家: IA_Fire → BaseCharacter::OnFirePressed → Weapon->Server_StartFire (RPC 序列化射线)
//   - AI  :  BT → GetAimRayFromCrosshairOrEyes (AI 路径 IsLocallyControlled=false)
//            → Weapon->Server_StartFire (BT 就在 Server 跑,RPC 本地直执行)
//   - 两条路径最终都汇聚到 ABaseWeapon::Server_StartFire_Implementation → WeaponFireComponent::StartFire
//   - 单一入口,玩家/AI 调用栈完全一致 — 这才是真正的"AI 镜像玩家" 的大厂级实现
//
// 历史 (按时间累积):
//   - v107: BT 显式算 (Target-Self) 方向 + Self.Location 起点 → 起点不对(脚底)
//   - v108: 改传 ZeroVector 让 Strategy 内部算 → Strategy 嵌套 if/else 死代码,从未真正跑
//   - v109: 用 BaseCharacter::GetAimRayFromCrosshairOrEyes 算射线 + Weapon->Server_StartFire
//           → AI 跟玩家走完全相同的管线(只是"算射线" 的人不同)
//
// 配套:
//   - BTTask_StopPrimaryFire (同模块, 用于全自动武器的循环停火)
//   - BTTask_ReloadPrimaryWeapon (同模块, 用于换弹入口)

#include "Systems/AI/Tasks/BTTask_StartPrimaryFire.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"  // 【v130 新增】BB 读取 TargetActor
#include "GameFramework/Pawn.h"

#include "Characters/BaseCharacter.h"
#include "Weapons/BaseWeapon.h"
#include "Components/WeaponFireComponent.h"

UBTTask_StartPrimaryFire::UBTTask_StartPrimaryFire()
{
	NodeName = TEXT("Start Primary Fire");

	// 【v130 新增】TargetActorKey 注册 — Object 类型过滤
	//   - 让 BT 编辑器下拉只显示 Object Key, 不会选错类型
	TargetActorKey.AddObjectFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTTask_StartPrimaryFire, TargetActorKey),
		AActor::StaticClass());
}


FString UBTTask_StartPrimaryFire::GetStaticDescription() const
{
	const FString TargetInfo = TargetActorKey.SelectedKeyName.IsNone()
		? TEXT("无 TargetActor Key (走 v109 BaseAimRotation 水平射线 — 旧行为)")
		: FString::Printf(TEXT("TargetActor Key='%s' (v130 3D 追踪射线 — 跟随 Target 上下)"),
			*TargetActorKey.SelectedKeyName.ToString());

	return FString::Printf(TEXT(
		"调 CurrentWeapon->Server_StartFire (镜像玩家 IA_Fire)\n"
		"射线起点 = Weapon Mesh Muzzle Socket\n"
		"射线方向算法:\n"
		"  - %s\n"
		"  - 无效 TargetKey → fallback 到 BaseCharacter::GetAimRayFromCrosshairOrEyes\n"
		"无武器/无 Pawn/无 AIController → Failed (零兜底)"),
		*TargetInfo);
}


EBTNodeResult::Type UBTTask_StartPrimaryFire::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		UE_LOG(LogBehaviorTree, Warning,
			TEXT("[BTTask_StartPrimaryFire] 无 AIController, 拒绝开火."));
		return EBTNodeResult::Failed;
	}

	ABaseCharacter* SelfPawn = Cast<ABaseCharacter>(AIC->GetPawn());
	if (!SelfPawn)
	{
		UE_LOG(LogBehaviorTree, Warning,
			TEXT("[BTTask_StartPrimaryFire] AIC='%s' Pawn 不是 ABaseCharacter, 拒绝开火."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	ABaseWeapon* Weapon = SelfPawn->GetCurrentWeapon();
	if (!Weapon)
	{
		UE_LOG(LogBehaviorTree, Warning,
			TEXT("[BTTask_StartPrimaryFire] AIC='%s' 无武器, 拒绝开火. "
			     "【修复】确认 BP_***_AI 已挂武器 / DT 配置正确."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	// ===========================================
	// 【v130 大厂 3D 追踪 P0 关键修复】
	// 算射线 — 优先用 TargetActor 算 3D 追踪射线,fallback 到 v109 GetAimRayFromCrosshairOrEyes
	//   - 用户反馈 (2026.08.02): AI 水平射击, 不能跟随 Target 上下位置
	//   - 旧版 (v109) 根因: AI 路径用 BaseAimRotation (水平, 准星旋转, 不跟随 Target 高度)
	//   - 新版 (v130) 修复: 优先用 ComputeFireRayTowardsTarget (3D, Muzzle→Target 方向)
	//
	// 大厂原则 — Target Data 优先 (零兜底优于隐式 fallback):
	//   路径 A: 配 TargetActorKey + TargetActor 有效 → 3D 追踪射线 (v130 新行为)
	//   路径 B: 没配 / TargetActor 无效 → fallback v109 (旧行为, 兼容)
	//   路径 C: v109 也失败 → BT Failed (零兜底, 不允许射线空跑)
	//
	// 大厂原则 — 与玩家 BP_BaseCharacter 镜像 100%:
	//   - 玩家路径: PlayerCameraManager → HUD Crosshair (也是 3D 完整射线)
	//   - AI 路径: ComputeFireRayTowardsTarget → 3D (跟玩家一致 — CS:GO/Apex/Valorant 大厂标准)
	// ===========================================
	FVector ClientRayOrigin;
	FVector ClientRayDirection;
	bool bGotRay = false;

	// 路径 A: 优先用 TargetActor 算 3D 追踪射线 (v130 新增)
	const bool bHasTargetKey = !TargetActorKey.SelectedKeyName.IsNone();
	if (bHasTargetKey)
	{
		UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
		if (BB)
		{
			AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(TargetActorKey.SelectedKeyName));
			if (IsValid(TargetActor))
			{
				bGotRay = SelfPawn->ComputeFireRayTowardsTarget(TargetActor, ClientRayOrigin, ClientRayDirection);
				if (bGotRay)
				{
					UE_LOG(LogBehaviorTree, Verbose,
						TEXT("[BTTask_StartPrimaryFire] 【v130 3D 追踪】AIC='%s' Target='%s' "
						     "Origin=%s Direction=%s (自动跟随 Target 上下)"),
						*AIC->GetName(),
						*TargetActor->GetName(),
						*ClientRayOrigin.ToCompactString(),
						*ClientRayDirection.ToCompactString());
				}
			}
			else
			{
				UE_LOG(LogBehaviorTree, Verbose,
					TEXT("[BTTask_StartPrimaryFire] 【v130】AIC='%s' TargetActorKey 配了但 BB 里是空, fallback 到 v109. "
					     "(检查上层装饰器顺序 — LineOfSight/InAttackRange 应该先拒判, 这里不该走到)"),
					*AIC->GetName());
			}
		}
	}

	// 路径 B: fallback v109 (旧行为, 保持兼容)
	if (!bGotRay)
	{
		bGotRay = SelfPawn->GetAimRayFromCrosshairOrEyes(ClientRayOrigin, ClientRayDirection);
		if (!bGotRay)
		{
			UE_LOG(LogBehaviorTree, Warning,
				TEXT("[BTTask_StartPrimaryFire] AIC='%s' GetAimRayFromCrosshairOrEyes 失败, 拒绝开火. "
				     "(详见上一行 UE_LOG Error 排查路径)"),
				*AIC->GetName());
			return EBTNodeResult::Failed;
		}
	}

	// ===========================================
	// 【v109 大厂镜像方案 P0 关键修复】
	// 调 Weapon->StartFire — 跟玩家 IA_Fire 走完全相同的 RPC 入口
	//   - 玩家: Weapon->Server_StartFire (RPC 序列化射线传给服务器) → 收 RPC
	//           → Server_StartFire_Implementation → WeaponFireComponent->StartFire
	//   - AI  :  Weapon->Server_StartFire 直接本地执行 (UE Server RPC 在 HasAuthority
	//           Actor 上直接调 Implementation, 不走网络跳跃 — BT 就在 Server 跑)
	//           → Server_StartFire_Implementation → WeaponFireComponent->StartFire
	//
	// 大厂原则 — 镜像玩家 100% (单一调用入口):
	//   - 玩家/AI 都调同一个 ABaseWeapon::Server_StartFire
	//   - 玩家走 RPC 序列化 (客户端 → 服务器)
	//   - AI 走本地直接调 (服务器进程内, BT 就在 Server)
	//   - 最终都汇聚到 WeaponFireComponent::StartFire (有 HasAuthority 守卫)
	//
	// 大厂原则 — 为什么不用 WeaponFireComponent->StartFire 直调:
	//   - WeaponFireComponent::StartFire 第 261 行明确拒绝客户端调用
	//   - 但"客户端拒绝"  → "AI 也不允许" 是反模式 (AI 在 Server 跑, 合法 Authority)
	//   - 为了保留"统一入口" 大厂原则, 走 ABaseWeapon::Server_StartFire 这条路
	//   - 这才是真正的"AI 镜像玩家": 同一函数, 不同 RPC 路径, 最终同一目标
	// ===========================================
	Weapon->Server_StartFire(ClientRayOrigin, ClientRayDirection);

	UE_LOG(LogBehaviorTree, Display,
		TEXT("[BTTask_StartPrimaryFire] 【v109 镜像方案】AIC='%s' Server_StartFire 已调. "
		     "Weapon='%s' RayOrigin=%s RayDir=%s "
		     "(同玩家路径走 GetAimRayFromCrosshairOrEyes + Server_StartFire)"),
		*AIC->GetName(),
		*Weapon->GetName(),
		*ClientRayOrigin.ToCompactString(),
		*ClientRayDirection.ToCompactString());

	return EBTNodeResult::Succeeded;
}
