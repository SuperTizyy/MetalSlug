// ==========================================
// AnimNotify_DetachMagazine 实现 (弹匣卸下通知)
// ==========================================
//
// 【大厂原则 — 职责对等】
//   本类与 AnimNotify_AttachMagazine 完全对称 — Reload 蒙太奇前半段 Detach
//   弹匣从武器弹匣槽移到左手 Socket (MagazineSocket_L), 后半段 Attach 装回
//
// 【职责链】
//   美术在 Reload 蒙太奇时间轴上挂此通知 → 弹匣从弹匣槽卸到左手
//   1. 校验 MeshComp/Owner/Weapon/MagazineMesh (零兜底, 与 Attach 镜像)
//   2. 校验角色 Mesh 上是否有 MagazineSocket_L (零兜底 — 没有 socket 直接报错)
//   3. Detach + Attach 两次操作, KeepWorld 规则避免弹匣位置跳变
//
// 【零兜底原则】
//   - Weapon 缺失 → Log Error + 拒绝
//   - MagazineMesh 未配置 → Log Error + 提示修复路径
//   - MagazineSocket_L 不存在 → Log Error + 提示修复路径 (在 Hand_L bone 上加 socket)
// ==========================================

#include "Animation/AnimNotify_DetachMagazine.h"
#include "Components/SkeletalMeshComponent.h"
#include "Weapons/BaseWeapon.h"
#include "Characters/BaseCharacter.h"


/**
 * Notify — AnimNotify 基类核心回调
 *
 * @brief  把弹匣从武器弹匣槽卸到角色左手的 MagazineSocket_L, 完成"换弹"动画视觉表现
 * @param  MeshComp        当前播放动画的骨骼网格组件 (通常为角色 Mesh)
 * @param  Animation       触发该通知的动画序列
 * @param  EventReference  通知事件引用 (UE 5 新 API)
 * @note   必须与 AnimNotify_AttachMagazine 在同一蒙太奇中使用 — 顺序: Detach → Attach
 * @note   零兜底: MagazineSocket_L 不存在时直接报错, 不会用兜底 socket
 */
void UAnimNotify_DetachMagazine::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	// 防崩保护
	if (!MeshComp)
	{
		return;
	}

	// 取 MeshComp 的 Owner
	ACharacter* OwningCharacter = Cast<ACharacter>(MeshComp->GetOwner());
	if (!OwningCharacter)
	{
		return;
	}

	// 转 ABaseCharacter 派生类
	ABaseCharacter* BaseChar = Cast<ABaseCharacter>(OwningCharacter);
	if (!BaseChar)
	{
		return;
	}

	// 取当前装备的武器
	ABaseWeapon* Weapon = BaseChar->GetCurrentWeapon();
	if (!Weapon)
	{
		// 零兜底: 没有武器时不应执行卸弹 — 配错直接报错
		UE_LOG(LogTemp, Error,
			TEXT("[DetachMagazine] Character %s has no equipped weapon, cannot detach magazine."),
			*OwningCharacter->GetName());
		return;
	}

	// 取武器的弹匣 Mesh
	USkeletalMeshComponent* MagazineMesh = Weapon->ResolveMagazineSkeletalMesh();
	if (!MagazineMesh)
	{
		// 零兜底: 弹匣 Mesh 未配置 — 提示修复路径
		UE_LOG(LogTemp, Error,
			TEXT("[DetachMagazine] MagazineSkeletalMesh not configured. Weapon=%s Character=%s. Fix: Add SkeletalMeshComponent named MagazineSkeletal in weapon BP Components panel."),
			*Weapon->GetName(),
			*OwningCharacter->GetName());
		return;
	}

	// 卸下前的状态日志
	USceneComponent* CurrentParent = MagazineMesh->GetAttachParent();
	FName CurrentSocket = MagazineMesh->GetAttachSocketName();
	UE_LOG(LogTemp, Log,
		TEXT("[DetachMagazine] ENTER. Weapon=%s MagazineMesh=%s CurrentParent='%s' Socket='%s'"),
		*Weapon->GetName(),
		*MagazineMesh->GetName(),
		CurrentParent ? *CurrentParent->GetName() : TEXT("<null>"),
		*CurrentSocket.ToString());

	// 校验角色 Mesh 上是否有 MagazineSocket_L — 这是换弹动画的左手挂载点
	if (!OwningCharacter->GetMesh()->DoesSocketExist(FName("MagazineSocket_L")))
	{
		// 零兜底: 左手 socket 不存在 → 提示修复路径 (在 Hand_L bone 上添加 socket)
		UE_LOG(LogTemp, Error,
			TEXT("[DetachMagazine] Character %s mesh has no MagazineSocket_L socket. Fix: Open character BP, go to Skeleton, add socket named MagazineSocket_L on Hand_L bone."),
			*OwningCharacter->GetName());
		return;
	}

	// 先 Detach — KeepWorld 规则确保弹匣保持当前世界坐标 (避免位置跳变)
	FDetachmentTransformRules DetachRules(EDetachmentRule::KeepWorld, true);
	MagazineMesh->DetachFromComponent(DetachRules);

	UE_LOG(LogTemp, Log,
		TEXT("[DetachMagazine] Detached. MagazineMesh Parent='%s' Socket='%s'"),
		MagazineMesh->GetAttachParent() ? *MagazineMesh->GetAttachParent()->GetName() : TEXT("<Detached>"),
		*MagazineMesh->GetAttachSocketName().ToString());

	// 再 Attach 到左手 MagazineSocket_L — 同样 KeepWorld
	FAttachmentTransformRules AttachmentRules(EAttachmentRule::KeepWorld, true);
	MagazineMesh->AttachToComponent(
		OwningCharacter->GetMesh(),
		AttachmentRules,
		FName("MagazineSocket_L"));

	// 最终状态日志: 确认弹匣已挂到左手
	FName FinalSocket = MagazineMesh->GetAttachSocketName();
	UE_LOG(LogTemp, Log,
		TEXT("[DetachMagazine] Magazine detached to left hand MagazineSocket_L. Weapon=%s Character=%s FinalSocket='%s'"),
		*Weapon->GetName(),
		*OwningCharacter->GetName(),
		*FinalSocket.ToString());
}
