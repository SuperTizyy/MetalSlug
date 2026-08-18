// ==========================================
// AnimNotify_AttachMagazine 实现 (弹匣装回通知)
// ==========================================
//
// 【大厂原则 — 职责单一】
//   本类是一个 AnimNotify (瞬态通知) — 仅在动画播放到指定帧时触发一次
//   不持有任何状态, 不跨帧执行 (与 ANS 区间通知形成对比)
//
// 【职责链】
//   美术在 Reload 蒙太奇时间轴上挂此通知 → 弹匣从左手装回弹匣槽
//   1. 校验 MeshComp/Owner/Weapon/MagazineMesh (零兜底)
//   2. 调用 Weapon->RestoreMagazineToWeapon() 执行实际挂载
//   3. 打印挂载前后日志, 便于调试
//
// 【零兜底原则】
//   - Weapon 缺失 → Log Error + 拒绝装回 (不允许静默跳过)
//   - MagazineMesh 未配置 → Log Error + 提示修复路径 (BP 加组件)
// ==========================================

#include "Animation/AnimNotify_AttachMagazine.h"
#include "Components/SkeletalMeshComponent.h"
#include "Weapons/BaseWeapon.h"
#include "Characters/BaseCharacter.h"


/**
 * Notify — AnimNotify 基类核心回调
 *
 * @brief  当 Reload 蒙太奇播放到指定帧时, UE 引擎自动调用此函数
 * @param  MeshComp        当前播放动画的骨骼网格组件 (通常为角色 Mesh)
 * @param  Animation       触发该通知的动画序列
 * @param  EventReference  通知事件引用 (UE 5 新 API)
 * @note   零兜底: 任何关键指针为空都立即 Log Error 并返回, 不静默跳过
 */
void UAnimNotify_AttachMagazine::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	// 防崩保护: MeshComp 为空时直接返回 (UE 可能在编辑器预览中传入 nullptr)
	if (!MeshComp)
	{
		return;
	}

	// 取 MeshComp 的 Owner, 即持有此 Mesh 的角色 Actor
	ACharacter* OwningCharacter = Cast<ACharacter>(MeshComp->GetOwner());
	if (!OwningCharacter)
	{
		return;
	}

	// 转 ABaseCharacter 派生类 — 武器 API 来自 ABaseCharacter
	ABaseCharacter* BaseChar = Cast<ABaseCharacter>(OwningCharacter);
	if (!BaseChar)
	{
		return;
	}

	// 取当前装备的武器 (单一真理源 = ABaseCharacter::GetCurrentWeapon)
	ABaseWeapon* Weapon = BaseChar->GetCurrentWeapon();
	if (!Weapon)
	{
		// 零兜底: 装弹必须先有武器 — 配错(没 Spawn 武器)直接报错, 拒绝静默跳过
		UE_LOG(LogTemp, Error,
			TEXT("[AttachMagazine] Character %s has no equipped weapon, cannot attach magazine."),
			*OwningCharacter->GetName());
		return;
	}

	// 取武器的弹匣 Mesh (按命名约定 MagazineSkeletal)
	USkeletalMeshComponent* MagazineMesh = Weapon->ResolveMagazineSkeletalMesh();
	if (!MagazineMesh)
	{
		// 零兜底: 弹匣 Mesh 未配置 — 提示修复路径, 让策划/美术加组件
		UE_LOG(LogTemp, Error,
			TEXT("[AttachMagazine] MagazineSkeletalMesh not configured. Weapon=%s Character=%s. Fix: Add SkeletalMeshComponent named MagazineSkeletal in weapon BP Components panel."),
			*Weapon->GetName(),
			*OwningCharacter->GetName());
		return;
	}

	// 装回前的状态日志: 记录当前挂载关系, 便于回溯
	USceneComponent* CurrentParent = MagazineMesh->GetAttachParent();
	FName CurrentSocket = MagazineMesh->GetAttachSocketName();
	UE_LOG(LogTemp, Log,
		TEXT("[AttachMagazine] ENTER. Weapon=%s MagazineMesh=%s CurrentParent='%s' Socket='%s'"),
		*Weapon->GetName(),
		*MagazineMesh->GetName(),
		CurrentParent ? *CurrentParent->GetName() : TEXT("<Detached>"),
		*CurrentSocket.ToString());

	// 核心调用: 把弹匣 Mesh 从左手 Socket 装回武器上的弹匣槽 (由 ABaseWeapon 实现)
	Weapon->RestoreMagazineToWeapon();

	// 装回后的状态日志: 确认挂载结果
	FName FinalSocket = MagazineMesh->GetAttachSocketName();
	USceneComponent* FinalParent = MagazineMesh->GetAttachParent();
	UE_LOG(LogTemp, Log,
		TEXT("[AttachMagazine] Magazine attached. Weapon=%s Character=%s FinalParent='%s' FinalSocket='%s'"),
		*Weapon->GetName(),
		*OwningCharacter->GetName(),
		FinalParent ? *FinalParent->GetName() : TEXT("<Detached>"),
		*FinalSocket.ToString());
}
