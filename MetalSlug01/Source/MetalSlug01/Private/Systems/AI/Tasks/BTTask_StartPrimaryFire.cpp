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
#include "GameFramework/Pawn.h"

#include "Characters/BaseCharacter.h"
#include "Weapons/BaseWeapon.h"
#include "Components/WeaponFireComponent.h"

UBTTask_StartPrimaryFire::UBTTask_StartPrimaryFire()
{
	NodeName = TEXT("Start Primary Fire");
}


FString UBTTask_StartPrimaryFire::GetStaticDescription() const
{
	return TEXT("调 CurrentWeapon->StartFire (镜像玩家 IA_Fire)\n"
				"射线算法 = BaseCharacter::GetAimRayFromCrosshairOrEyes (AI 路径)\n"
				"无目标/武器 → Failed");
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
	// 【v109 大厂镜像方案 P0 关键修复】
	// 算射线 — 用 BaseCharacter 已有的 GetAimRayFromCrosshairOrEyes API (line 2240)
	//   - 玩家路径:本地玩家 → HUD Crosshair World Ray (玩家准星)
	//   - AI 路径: IsLocallyControlled=false → GetBaseAimRotation (BT 控制的旋转)
	//              + WeaponMesh Muzzle Socket (由 GetAimRayFromCrosshairOrEyes 内部算)
	//   - 两条路径函数签名一致,AI 跟玩家完全对称
	//
	// 为什么 BT 不自己算 (v107 反模式):
	//   - BT 用 SelfLocation 作起点: 不准,应该用 Muzzle Socket
	//   - BT 用 (Target-Self) 作方向: 够用,但起点配错让射线从脚下发出
	//   - 把"算射线" 关注点抽到 BaseCharacter 才是大厂级抽象
	// ===========================================
	FVector ClientRayOrigin;
	FVector ClientRayDirection;
	if (!SelfPawn->GetAimRayFromCrosshairOrEyes(ClientRayOrigin, ClientRayDirection))
	{
		// 内部已 Log Error 解释根因(HUD 没初始化 / BaseAimRotation 是 Zero 等)
		UE_LOG(LogBehaviorTree, Warning,
			TEXT("[BTTask_StartPrimaryFire] AIC='%s' GetAimRayFromCrosshairOrEyes 失败, 拒绝开火. "
			     "(详见上一行 UE_LOG Error 排查路径)"),
			*AIC->GetName());
		return EBTNodeResult::Failed;
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
