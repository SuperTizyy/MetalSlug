// ==========================================
// 版权声明：在项目设置的描述页面填写您的版权信息。
// ==========================================
//
// @file WeaponAttachmentComponent.cpp
// @brief 武器网络复制 + 挂载 + SyncWeapon 子系统 — 实现
//
// 【大厂原则】
//   - 单一真理源: CurrentWeapon 在本组件, CharacterID/SpawnWeaponID 在本组件
//   - 零兜底: OnRep_CurrentWeapon 反查失败 → Log Error + return (Phase 1.2)
//   - 零兜底: FindWeaponAttachmentConfig 找不到精确 → Log Error + return nullptr (Phase 1.3)
//   - Owner 暴露: 通过 Owner->MeleeProfile / Owner->GetMesh() 等访问
//
// 【调用链】
//   PossessedBy (Server) → SyncWeaponFromProfile → SpawnAndEquipWeapon
//                                                        ↓
//                                                    SpawnActor + AttachToComponent
//                                                        ↓
//                                                    CurrentWeapon = NewWeapon (Replicated)
//                                                                             ↓
//                                                                  OnRep_CurrentWeapon (Client)
//                                                                             ↓
//                                                                  反查 WeaponID + 挂载
// ==========================================

// ==========================================
// 头文件包含区
// ==========================================
#include "Combat/WeaponAttachmentComponent.h"

// 包含 BaseCharacter 以访问 Owner->MeleeProfile / GetMesh() / GetController() 等
#include "Characters/BaseCharacter.h"

// 包含 BaseWeapon 以访问 IsA<>() / AttachToComponent
#include "Weapons/BaseWeapon.h"

// 【v56 大厂架构 — 生化模式】Strategy 装配 (按 MeshType 自动注入)
#include "Weapons/WeaponDamageStrategy.h"
#include "Weapons/MeleeSwStrategy.h"
#include "Weapons/RangedLineStrategy.h"
#include "Components/WeaponFireComponent.h"  // v60 — 武器开火组件
#include "Data/Tables/WeaponTableRow.h"     // v60 — DT_WeaponInfo 真理源
#include "Data/Config/WeaponSoundMapAsset.h" // v76 — 武器切换音效配置真理源

// 包含 RoomGameMode 以访问 WeaponDataTable (运行时武器配置)
#include "Systems/RoomGameMode.h"

// 包含 AMyGameHUD 以访问 HUD 刷新
#include "UI/MyGameHUD.h"

// 【v84 大厂架构新增】武器切换时刷新武器图标和弹夹数量
#include "Combat/CharacterIconComponent.h"

// 包含 GameHUDWidget 以访问 UpdateWeaponIconFromID
#include "UI/Game/GameHUDWidget.h"

// 【v54 大厂架构重构 — UAIProfileAsset 已删除】这里不再 include Data/AI/AIProfileAsset.h
// 关卡预放 AI 走 ConfigSO (DA_AIBehaviorConfig_*.uasset)
// 数据流: AMeleeAIController::SetupMeleeAI 末尾 → SetMeleeConfig(UAIBehaviorConfigSO*)

// UE 反射: DOREPLIFETIME
#include "Net/UnrealNetwork.h"

// UE 控制器: APlayerController
#include "GameFramework/PlayerController.h"

// UE DataTable: FindRow 模板
#include "Engine/DataTable.h"

// UE 反射: LogCategory
#include "Logging/LogMacros.h"

// 【v76 大厂架构】武器切换音效 (UE 反射 + 标准播放 API)
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"


// ==========================================
// 【v52 P0】槽位枚举 → 数组下标映射 (大厂原则 — 消除隐式偏移)
//
// 根因:
//   EWeaponSlotType 枚举顺序 (None=0, Primary=1, Melee=2, Secondary=3)
//   与 WeaponsInSlot 数组下标顺序 (0=Primary, 1=Secondary, 2=Melee) **严格对齐**
//   【v60.7 大厂重构】枚举值 = 数组下标 + 1 (Primary=1→0, Secondary=2→1, Melee=3→2)
//
// 大厂原则:
//   - WeaponsInSlot 数组是物理存储, 枚举是逻辑类型
//   - v60.7 后枚举值严格按业务顺序 (1=主, 2=副, 3=近), 数组下标 = 值 - 1
//   - 两者顺序完全对齐, 按键 N → uint8=N → 数组 [N-1]
//
// 映射表:
//   Primary   → 0
//   Secondary → 1
//   Melee     → 2
// ==========================================
static FORCEINLINE int32 SlotTypeToArrayIndex(EWeaponSlotType Slot)
{
	switch (Slot)
	{
	case EWeaponSlotType::Primary:   return 0;
	case EWeaponSlotType::Secondary: return 1;
	case EWeaponSlotType::Melee:     return 2;
	default: return INDEX_NONE; // None 或其他 → 无效
	}
}

// 【v78 大厂架构】反向映射: 数组索引 → 槽位类型
// 映射表:
//   0 → Primary
//   1 → Secondary
//   2 → Melee
// 其他 → None

// ==========================================
// 1. 构造函数
// ==========================================

/**
 * UWeaponAttachmentComponent 构造函数
 *
 * 目的: 不创建子组件 (本组件无子组件), 仅初始化默认值
 * 关键: 不启用 Tick (本组件不需要每帧 Tick, 所有逻辑都是事件驱动)
 */
UWeaponAttachmentComponent::UWeaponAttachmentComponent()
{
	// 不需要 Tick: 武器网络复制由 UE 引擎自动处理, 挂载由 OnRep 触发
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// 【2026.07.12 P0 修复】启用 Component 网络复制
	//
	// 根因: 旧构造函数只设 DOREPLIFETIME, 没调 SetIsReplicatedByDefault(true)
	//       → UE 默认 ActorComponent 不参与网络复制
	//       → CurrentWeapon 字段永远不复制到客户端
	//       → OnRep_CurrentWeapon 永远不触发
	//       → 客户端永远没武器 (客户端看不到任何 [OnRep_CurrentWeapon] 日志)
	//
	// 大厂原则 - 单一真理源:
	//   - SetIsReplicatedByDefault(true) 让 Component 本身参与复制
	//   - DOREPLIFETIME 注册 CurrentWeapon 字段
	//   - ReplicatedUsing = OnRep_CurrentWeapon 触发回调
	//   - 三层缺一不可, 这是 UE 5.6 硬约束
	//
	// 修复后: 服务器 SpawnAndEquipWeapon → CurrentWeapon = NewWeapon → 自动复制 → 所有客户端 OnRep 触发
	SetIsReplicatedByDefault(true);

	// 默认值 (已在头文件初始化, 这里不再重复)
	CurrentWeapon = nullptr;
}


// ==========================================
// 1. 静态工具函数
// ==========================================

// 【v77 大厂架构】检查武器是否已挂载到 Socket
//   原理: UE AttachToComponent 后, Actor 的 RootComponent 会记录挂载的 Socket 名
//   如果 GetAttachSocketName() != NAME_None, 说明已挂载
//   返回: true = 已挂载 (跳过重复 Attach), false = 未挂载 (需要补挂)
bool UWeaponAttachmentComponent::IsWeaponAttachedToSocket(ABaseWeapon* Weapon, ABaseCharacter* Owner) const
{
	if (!Weapon || !Owner || !Owner->GetMesh())
	{
		return false;
	}

	USceneComponent* RootComp = Weapon->GetRootComponent();
	if (!RootComp)
	{
		return false;
	}

	FName AttachedSocket = RootComp->GetAttachSocketName();
	if (AttachedSocket == NAME_None)
	{
		return false;
	}

	// 进一步验证: 武器确实挂在角色的 Mesh 上 (防止挂错对象)
	USceneComponent* AttachmentParent = RootComp->GetAttachParent();
	if (AttachmentParent != Owner->GetMesh())
	{
		return false;
	}

	return true;
}

// ==========================================
// 2. UE 生命周期
// ==========================================

/**
 * GetLifetimeReplicatedProps: 注册需要网络同步的 UPROPERTY
 *
 * 同步内容:
 *   - CurrentWeapon (武器指针, ReplicatedUsing 触发 OnRep)
 *   - CharacterID / SpawnWeaponID (字符串, 普通 Replicated)
 *
 * 【2026.07.12 P0 修复】Client 端 OnRep_CurrentWeapon 反查挂载配置依赖 CharacterID 和 SpawnWeaponID
 *                    如果这两个字段没复制, 客户端 OnRep 时拿到空字符串, 反查失败 → 武器不挂载
 */
void UWeaponAttachmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 【v78 大厂架构 — 单一复制链】WeaponState struct 一次性复制所有字段
	// 替代旧 CurrentWeapon + CurrentWeaponSlot 两个独立 UPROPERTY(Replicated)
	// UE 保证 struct 内所有字段在同一帧复制到所有客户端
	DOREPLIFETIME(UWeaponAttachmentComponent, WeaponState);

	// CurrentWeapon: ReplicatedUsing = OnRep_CurrentWeapon (保留向后兼容)
	// 服务器写 → 客户端 OnRep_CurrentWeapon 自动触发
	DOREPLIFETIME(UWeaponAttachmentComponent, CurrentWeapon);

	// CharacterID / SpawnWeaponID: 普通 Replicated
	// 服务器 PossessedBy / SpawnAIInternal 写入 → 客户端同步 → OnRep_CurrentWeapon 反查挂载
	DOREPLIFETIME(UWeaponAttachmentComponent, CharacterID);
	DOREPLIFETIME(UWeaponAttachmentComponent, SpawnWeaponID);

	// 【v51 大厂架构 — 生化模式】双槽位同步
	//   WeaponsInSlot: 服务器 SpawnBothWeapons 后, 客户端自动收到双武器指针数组
	//   CurrentWeaponSlot: 【v78 废弃】真理源移至 WeaponState，保留作为缓存视图
	DOREPLIFETIME(UWeaponAttachmentComponent, WeaponsInSlot);
	DOREPLIFETIME(UWeaponAttachmentComponent, CurrentWeaponSlot);

	// 【v81 大厂架构 — 客户端武器姿态修复】
	//   旧: 服务器调 SetActorRelativeLocation 但 UE 不复制 RelativeLocation 字段
	//   新: 服务器写入 ActiveSlotAttachment (FWeaponAttachmentRuntime) → UE 自动复制
	//       → 客户端 OnRep_CurrentWeapon / OnRep_CurrentWeaponSlot 读取并强制应用偏移
	DOREPLIFETIME(UWeaponAttachmentComponent, ActiveSlotAttachment);
}


/**
 * GetOwnerCharacterChecked — v40.3 单一真理源: 永远走 GetOwner()
 *
 * 反模式 (v6-v40.2): TWeakObjectPtr<ABaseCharacter> OwnerCharacter BeginPlay 缓存
 *   - 缓存可能失效 (BeginPlay 未跑 / BP 子类清空字段)
 *   - 多个调用方裸用 OwnerCharacter.Get() → 拿到 null 时 Log Error + return
 *
 * 新架构 (v40.3):
 *   - 真理源 = GetOwner() (UE 标准 API, Component 必挂在 Actor 上, 永远非空)
 *   - 不缓存 (零兜底 + DRY + 不增加不确定性)
 *   - Cast 失败 Log Error + return nullptr (强制修复 BP 子类化挂载错误)
 *
 * @return 有效 Owner (失败返回 nullptr, 调用方必须 if 检查 + 走错误处理路径)
 */
ABaseCharacter* UWeaponAttachmentComponent::GetOwnerCharacterChecked(const UActorComponent* Self)
{
	if (!Self)
	{
		// Self 为空 = 静默 nullptr 返回 (componnet 自身已失效, 不强求)
		return nullptr;
	}

	AActor* OwnerActor = Self->GetOwner();
	if (!OwnerActor)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] GetOwnerCharacterChecked: GetOwner() 返回 nullptr. "
				 "Component 实例: '%s'. 组件未挂在 Actor 上, 致命错误."),
			*GetNameSafe(Self));
		return nullptr;
	}

	ABaseCharacter* OwnerChar = Cast<ABaseCharacter>(OwnerActor);
	if (!OwnerChar)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] GetOwnerCharacterChecked: Owner '%s' (类型=%s) 不是 ABaseCharacter 派生. "
				 "本组件必须挂在 ABaseCharacter 子类上."),
			*OwnerActor->GetName(),
			*OwnerActor->GetClass()->GetName());
		return nullptr;
	}

	return OwnerChar;
}


/**
 * BeginPlay: 一次性诊断日志 (v40.3 不再缓存 Owner 字段)
 *
 * 历史 (v6-v40.2): 在此处缓存 OwnerCharacter TWeakObjectPtr
 * 现架构 (v40.3): 不缓存, 运行时全走 GetOwner() — BeginPlay 仅留诊断日志
 */
void UWeaponAttachmentComponent::BeginPlay()
{
	Super::BeginPlay();

	// 一次性校验 Owner (不缓存, 仅做启动期诊断 — 强制 BP 配置错误立即暴露)
	if (ABaseCharacter* Owner = GetOwnerCharacterChecked(this))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[WeaponAttachmentComponent] BeginPlay: Owner=%s, CharacterID='%s', SpawnWeaponID='%s', "
				 "CurrentWeapon=%s, bReplicates=%d"),
			*GetNameSafe(Owner), *CharacterID, *SpawnWeaponID,
			*GetNameSafe(CurrentWeapon), (int32)GetIsReplicated());
	}
}


/**
 * EndPlay: 清理 (无字段缓存, 无需清理 — 保留作为钩子)
 */
void UWeaponAttachmentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// v40.3 删除 OwnerCharacter 清空 (字段已不存在)
	Super::EndPlay(EndPlayReason);
}


// ==============================================================================
// ==============================================================================
// 【v81 大厂架构修复】OnRep_CurrentWeapon — 客户端强制补 Attach + 应用偏移
//
// 旧 (v77-v80) 反模式:
//
//   Bug 1/2/4: OnRep_CurrentWeapon 对"已挂载武器"重复 Destroy() + Attach()
//              流程: 服务器 Attach → CurrentWeapon 复制 → 客户端 OnRep
//                    → AttachToComponent(已挂载的武器) → 时序错误 → 偏移未应用
//                    → 如果找不到 AttachmentConfig → Destroy() → 武器消失
//   Bug 3:     CharacterID 复制晚于 CurrentWeapon → 跳过整个逻辑 → 武器无偏移
//   Bug 5 (v81 新): UE Actor 复制会自动同步 Attach 关系, 但不会同步 RelativeLocation/Rotation
//              → 客户端武器 Actor 已挂在角色 Mesh 上 (Socket 位置) 但没有策划偏移
//              → IsWeaponAttachedToSocket 返回 true → 跳过补偏移 → 客户端姿态错
//
// 新架构 (v81 — 单一真理源 + 显式同步):
//
//   1. 服务器 SpawnAndEquipWeapon → Attach + SetActorRelativeLocation/Rotation/Scale + 写入 ActiveSlotAttachment (Replicated)
//   2. 客户端 OnRep_CurrentWeapon 触发 → 走 ApplyAttachmentRuntime(CurrentWeapon)
//      → 强制 Attach 到角色 Mesh + 应用 ActiveSlotAttachment 配置的偏移
//      → 不论武器之前是否已挂载, 都应用服务器配置的偏移
//   3. 不再调用 Destroy() / 不再做"已挂载就跳过"分支
//
// 大厂原则:
//   - 单一真理源: 服务器 ActiveSlotAttachment 是偏移唯一权威
//   - 零兜底: 服务器未写入偏移 → bIsValid=false → 客户端 Log Error 强制修复
//   - 不依赖 UE 隐式行为: 显式同步偏移, 不依赖"Attach 自动同步"
// ==============================================================================
void UWeaponAttachmentComponent::OnRep_CurrentWeapon(ABaseWeapon* OldWeapon)
{
	ABaseCharacter* Owner = GetOwnerCharacterChecked(this);
	if (!Owner)
	{
		return;
	}

	const bool bIsLocallyControlled = Owner->IsLocallyControlled();
	const bool bIsAuthority = Owner->HasAuthority();

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] OnRep_CurrentWeapon: Old=%s, New=%s, Local=%d, Auth=%d"),
		*GetNameSafe(OldWeapon), *GetNameSafe(CurrentWeapon),
		bIsLocallyControlled, bIsAuthority);

	// 旧武器脱挂 (防御: 复活 / 换装场景)
	if (OldWeapon && IsValid(OldWeapon) && OldWeapon != CurrentWeapon)
	{
		OldWeapon->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		UE_LOG(LogTemp, Log,
			TEXT("[WeaponAttachment] OnRep_CurrentWeapon: 脱挂旧武器=%s"),
			*OldWeapon->GetName());
	}

	// 新武器为空, 服务器清空了武器 — 脱挂已结束 (OldWeapon 已处理)
	if (!CurrentWeapon)
	{
		return;
	}

	// 【v81 修复】删除 IsWeaponAttachedToSocket 跳过分支 — 不论是否已挂载, 都强制应用偏移
	//   单一真理源: ActiveSlotAttachment 字段 (服务器写入, Replicated)
	//   客户端必须严格应用服务器配置的偏移, 不能依赖"已挂载就跳过" 的隐式行为
	ApplyAttachmentRuntime(CurrentWeapon);
}


// ==============================================================================
// 【v81 大厂架构】ApplyAttachmentRuntime — 应用挂载运行时数据到武器 Actor
//
// 真理源 = ActiveSlotAttachment (服务器写入 Replicated)
// 调用方 (3 个, 都是 OnRep 入口):
//   - OnRep_CurrentWeapon 末尾 (远端玩家新武器复制过来时)
//   - OnRep_CurrentWeaponSlot 末尾 (远端玩家槽位切换时)
//   - Server_SwitchToWeaponSlot / SpawnAndEquipWeapon 已主动应用, 但这里做幂等保护
//
// 大厂原则 - 零兜底:
//   - Weapon 为空 → Log Error + return (强制修复 Spawn 链路)
//   - bIsValid=false → Log Error + return (服务器没写入偏移, 强制修复服务器逻辑)
//   - 不允许"已挂载就跳过" (v77 反模式已删除, 客户端必须强制应用偏移)
// ==============================================================================
void UWeaponAttachmentComponent::ApplyAttachmentRuntime(ABaseWeapon* Weapon)
{
	if (!Weapon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] ApplyAttachmentRuntime: Weapon 为空 — 拒绝应用. "
			     "【v81 零兜底】OnRep 必须在拿到有效 CurrentWeapon 后再调用本函数."));
		return;
	}

	if (!ActiveSlotAttachment.bIsValid)
	{
		// 服务器还没写入偏移 → 拒绝应用
		//   这通常意味着 Spawn 时未写入, 或 Server_SwitchToWeaponSlot 找不到配置
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] ApplyAttachmentRuntime: ActiveSlotAttachment.bIsValid=false — 服务器未写入挂载偏移. "
			     "Weapon=%s. 【v81 零兜底】客户端无法应用偏移, 请检查服务器 SpawnAndEquipWeapon/Server_SwitchToWeaponSlot 写入逻辑. "
			     "【排查路径】1) 确认服务器 SpawnAndEquipWeapon 末尾写入了 ActiveSlotAttachment; "
			     "2) 确认 DT_WeaponAttachmentConfig 有 PawnClass=%s + WeaponClass=%s 的精确匹配行."),
			*Weapon->GetName(),
			*GetOwner()->GetClass()->GetName(),
			*Weapon->GetClass()->GetName());
		return;
	}

	ABaseCharacter* Owner = GetOwnerCharacterChecked(this);
	if (!Owner || !Owner->GetMesh())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] ApplyAttachmentRuntime: Owner 或 Mesh 为空 — 拒绝 Attach. Weapon=%s"),
			*Weapon->GetName());
		return;
	}

	const FName SocketName = ActiveSlotAttachment.SocketName;
	const FVector RelativeLocation = ActiveSlotAttachment.RelativeLocation;
	const FRotator RelativeRotation = ActiveSlotAttachment.RelativeRotation;
	const FVector RelativeScale = ActiveSlotAttachment.RelativeScale3D;

	// Step 1: Attach (使用 SnapToTargetNotIncludingScale, 与服务器一致)
	FAttachmentTransformRules AttachmentRules = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
	Weapon->AttachToComponent(Owner->GetMesh(), AttachmentRules, SocketName);

	// Step 2: 强制应用相对偏移 (v57 零兜底修复 — 不允许 IsNearlyZero 跳过)
	Weapon->SetActorRelativeLocation(RelativeLocation);
	Weapon->SetActorRelativeRotation(RelativeRotation);
	Weapon->SetActorRelativeScale3D(RelativeScale);

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] ApplyAttachmentRuntime: 应用成功 — Weapon=%s Socket=%s Loc=%s Rot=%s Scale=%s"),
		*Weapon->GetName(),
		*SocketName.ToString(),
		*RelativeLocation.ToString(),
		*RelativeRotation.ToString(),
		*RelativeScale.ToString());
}


// ==============================================================================
// 【v78 大厂架构】RefreshWeaponVisibilityForSlotChange — 槽位切换刷新可见性
//
// 职责: 根据新激活的槽位，隐藏旧槽位武器、显示新槽位武器
//        替代 OnRep_CurrentWeaponSlot 的可见性逻辑，集中在一处
//
// 大厂原则 - 单一职责:
//   - 不做其他事情（不更新字段、不播音效、不广播 CharacterEvents）
//   - 只负责可见性
// ==============================================================================
void UWeaponAttachmentComponent::RefreshWeaponVisibilityForSlotChange(EWeaponSlotType NewActiveSlot)
{
	ABaseCharacter* Owner = GetOwnerCharacterChecked(this);
	if (!Owner)
	{
		return;
	}

	// 遍历所有槽位，设置可见性
	for (int32 Idx = 0; Idx < WeaponsInSlot.Num(); ++Idx)
	{
		if (!WeaponsInSlot.IsValidIndex(Idx) || !WeaponsInSlot[Idx])
		{
			continue;
		}

		ABaseWeapon* W = WeaponsInSlot[Idx];
		EWeaponSlotType SlotOfThisWeapon = ArrayIndexToSlotType(Idx);
		const bool bShouldShow = (SlotOfThisWeapon == NewActiveSlot);

		W->SetActorHiddenInGame(!bShouldShow);

		// 【v77 大厂原则 - 零幽灵碰撞】武器始终无碰撞
		W->SetActorEnableCollision(ECollisionEnabled::NoCollision);

		UE_LOG(LogTemp, Verbose,
			TEXT("[WeaponAttachment] RefreshWeaponVisibility: Pawn=%s Slot=%s Show=%d"),
			*Owner->GetName(),
			LexToString(SlotOfThisWeapon),
			bShouldShow ? 1 : 0);
	}
}


// ==============================================================================
// 4. EquipWeapon — 服务器换枪
// ==============================================================================

/**
 * EquipWeapon — 服务器装备武器 (简化接口, deprecated)
 *
 * 调用方: GameMode 直接指定 Class 的场景 (不走 DataTable)
 *
 * 流程:
 *   1. 仅服务器调用
 *   2. 如果手里已经有武器 → 销毁旧的
 *   3. SpawnActor 生成新武器
 *   4. 挂载到默认 Socket "WeaponSocket_L"
 *
 * 【v36 DEPRECATED】此接口违反大厂单一真理源 — 不查 DataTable, 不查 AttachmentConfig
 *   业务场景应该走 RequestWeaponSpawn (查表 + 挂载配置), 不要用 EquipWeapon.
 *   保留只是为了让 BP 不编译失败. 调用时打 Warning 提醒改用 RequestWeaponSpawn.
 */
void UWeaponAttachmentComponent::EquipWeapon(TSubclassOf<ABaseWeapon> WeaponClassToEquip)
{
	UE_LOG(LogTemp, Warning,
		TEXT("[WeaponAttachment] EquipWeapon 是 deprecated 接口 (v36), 不查 DataTable 也不查 AttachmentConfig. "
			 "【大厂原则】单一真理源 = WeaponDataTable + WeaponAttachmentDataTable. "
			 "请改用 RequestWeaponSpawn(TSubclassOf<ABaseWeapon>) 走完整链路. Pawn=%s, WeaponClass=%s"),
		*GetNameSafe(GetOwnerCharacterChecked(this)), *GetNameSafe(WeaponClassToEquip));

	ABaseCharacter* Owner = GetOwnerCharacterChecked(this);
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] EquipWeapon: Owner 无效."));
		return;
	}

	// 只有服务器上帝有资格发枪
	if (!Owner->HasAuthority() || !WeaponClassToEquip)
	{
		return;
	}

	// 如果手里已经有武器, 先销毁旧的
	if (CurrentWeapon)
	{
		CurrentWeapon->Destroy();
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 在世界中凭空召唤出这把武器
	CurrentWeapon = Owner->GetWorld()->SpawnActor<ABaseWeapon>(
		WeaponClassToEquip, Owner->GetActorLocation(), Owner->GetActorRotation(), SpawnParams);

	if (CurrentWeapon)
	{
		// 焊接到第三人称全身模型的 "WeaponSocket_L" 插槽
		CurrentWeapon->AttachToComponent(
			Owner->GetMesh(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			FName("WeaponSocket_L"));
	}
}


// ==========================================
// 5. RequestWeaponSpawn — 公开入口 (关卡预放 AI)
// ==========================================

/**
 * RequestWeaponSpawn — 武器生成请求入口 (对外暴露)
 *
 * 调用方:
 *   - AMeleeAIController::SetupMeleeAI (OnPossess 末尾手动触发武器生成)
 *
 * 内部直接调 SpawnAndEquipWeapon, 复用完整链路 (查表/挂载/HUD)
 */
void UWeaponAttachmentComponent::RequestWeaponSpawn(TSubclassOf<ABaseWeapon> WeaponClass)
{
	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] RequestWeaponSpawn: WeaponClass=%s, Owner=%s"),
		*GetNameSafe(WeaponClass), *GetNameSafe(GetOwnerCharacterChecked(this)));

	// 零兜底: WeaponClass 必须非空
	if (!WeaponClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] RequestWeaponSpawn: WeaponClass 为空. "
				 "【v54.3 零兜底】拒绝生成武器. "
				 "【根因排查】1) 调用方 LoadSynchronous 失败 2) DT_WeaponInfo 反查失败 3) Config.LevelPlacedWeaponClass 未配."));
		return;
	}

	// 直接调内部方法, 复用完整链路 (挂载配置/HUD)
	SpawnAndEquipWeapon(WeaponClass);
}


// ==========================================
// 6. SpawnAndEquipWeapon — 服务器权威生成
// ==========================================

/**
 * SpawnAndEquipWeapon — 根据 WeaponID 查表并生成+装备武器
 *
 * 工业级规范: 指针与有效性校验贯穿全流程
 *
 * 流程:
 *   1. WeaponID 校验
 *   2. 幂等检查 (防止 PossessedBy + SetupMeleeAI 同帧重复生成)
 *   3. GM + WeaponDataTable 校验
 *   4. 查表 FWeaponInfo (WeaponID → WeaponBlueprint)
 *   5. 查表 FWeaponAttachmentConfig (CharacterID + WeaponID → Socket/Offset)
 *   6. LoadSynchronous 加载 WeaponBlueprint
 *   7. SpawnActor 生成武器
 *   8. 挂载到 Socket (SnapToTargetNotIncludingScale)
 *   9. 应用 RelativeLocation/Rotation/Scale
 *   10. 写 CurrentWeapon (触发 Replicated)
 *   11. 刷新 HUD 武器图标 (仅本地玩家)
 */
/**
 * 【v54.3 大厂架构重构 — 直接接受 Class, 删除 DT_WeaponInfo 中间层】SpawnAndEquipWeapon
 *
 * 用户原话 2026.07.16:
 *   "DefaultWeaponRowName 属性为什么不直接引用 DT_WeaponInfo 表的 WeaponBlueprint 内容啊"
 *   → 真理源直接传递 Class, 不需要 DT 反查
 *
 * 旧流程 (v54.2 — 中间层冗余):
 *   1. WeaponID 校验
 *   2. 幂等检查
 *   3. GM + WeaponDataTable 校验
 *   4. 查表 FWeaponInfo (WeaponID → WeaponBlueprint)         ← 删除
 *   5. 查表 FWeaponAttachmentConfig (CharacterID + WeaponID → Socket/Offset)
 *   6. LoadSynchronous 加载 WeaponBlueprint                  ← 删除 (调用方已 Load)
 *   7. SpawnActor 生成武器
 *   8. 挂载到 Socket
 *   9. 应用 RelativeLocation/Rotation/Scale
 *   10. 写 CurrentWeapon (触发 Replicated)
 *   11. 刷新 HUD 武器图标
 *
 * 新流程 (v54.3 — 真理源直接 Class):
 *   1. WeaponClass 校验
 *   2. 幂等检查
 *   3. 查挂载配置 FWeaponAttachmentConfig (PawnClass + WeaponClass → Socket/Offset)  — 保留 (UI 配置)
 *   4. SpawnActor 生成武器
 *   5. 挂载到 Socket
 *   6. 应用 RelativeLocation/Rotation/Scale
 *   7. 写 CurrentWeapon (触发 Replicated)
 *   8. 刷新 HUD 武器图标
 *
 * 大厂原则:
 *   - 真理源直接传递: 调用方 (RequestWeaponSpawn) 已经 LoadSynchronous 拿到 Class, 这里不再查 DT
 *   - 删除中间层冗余: DT_WeaponInfo 反查从武器生成路径完全删除 (DT 仍保留供 UI 查询武器图标/列表)
 *   - 编译期类型安全: TSubclassOf<ABaseWeapon> 拒绝错类型
 */
void UWeaponAttachmentComponent::SpawnAndEquipWeapon(TSubclassOf<ABaseWeapon> WeaponClass)
{
	ABaseCharacter* Owner = GetOwnerCharacterChecked(this);
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SpawnAndEquipWeapon: Owner 无效. "
				 "【v40.3 零兜底】Component 已挂在已销毁 Actor 上? 检查 BP 子类化配置."));
		return;
	}

	// 【v54.3 显式优于隐式】WeaponClass 必须非空
	if (!WeaponClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SpawnAndEquipWeapon: WeaponClass 为空, 拒绝生成武器. "
				 "【v54.3 零兜底】不静默跳过, 不生成默认武器. "
				 "【根因排查】1) 调用方 LoadSynchronous 失败 2) DT_WeaponInfo 反查失败 3) Config.LevelPlacedWeaponClass 未配."));
		return;
	}

	// 【幂等性检查】防止重复生成武器
	if (IsValid(CurrentWeapon))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[WeaponAttachment] SpawnAndEquipWeapon: 已有武器 %s, 跳过生成 — WeaponClass=%s"),
			*CurrentWeapon->GetName(), *WeaponClass->GetName());
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] SpawnAndEquipWeapon: 开始生成武器: WeaponClass=%s, PawnClass=%s (真理源直接 Class, 跳过 DT_WeaponInfo 中间层)"),
		*WeaponClass->GetName(), *Owner->GetClass()->GetName());

	// 【v54.3 大厂重构】DT_WeaponInfo 反查已删除 — 武器 Class 由调用方直接传递
	//   旧 (v54.2): GM->WeaponDataTable->FindRow<FWeaponInfo>(FName(*WeaponID)) -> WeaponInfo->WeaponBlueprint.LoadSynchronous()
	//   新 (v54.3): 调用方已经传入 WeaponClass, 直接使用

	// 1. 查找武器挂载配置 (PawnClass + WeaponClass → Socket/Offset)
	FWeaponAttachmentConfig* AttachmentConfig = FindWeaponAttachmentConfig(Owner->GetClass(), WeaponClass);

	// 2. 在服务器上生成武器
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.Instigator = Owner;

	ABaseWeapon* NewWeapon = Owner->GetWorld()->SpawnActor<ABaseWeapon>(
		WeaponClass,
		Owner->GetActorLocation(),
		Owner->GetActorRotation(),
		SpawnParams
	);

	if (!NewWeapon)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[WeaponAttachment] SpawnAndEquipWeapon: SpawnActor 失败."));
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] SpawnAndEquipWeapon: 武器生成成功: %s"), *NewWeapon->GetName());

	// 4. 【零兜底】挂载参数必须从配置读取
	//
	// 旧实现 (v22-v31.x) 反模式:
	//   - 找不到挂载配置 → 静默用默认值 'WeaponSocket_R'
	//   - 角色没这个 socket → attach 实际无效 → 武器浮在原点
	//   - trace 的两端坐标返回 0 → IsNearlyZero() true → 跳过整个 trace
	//   - 用户看到"动画有, 伤害无" — 根因被默认值掩盖
	//
	// 新架构 (v32 — 零兜底):
	//   - 严格依赖 FindWeaponAttachmentConfig 返回的配置
	//   - 配置缺失 → Log Error + Destroy 武器 + 不 attach
	//   - 这强制用户配 WeaponAttachmentDataTable (v37 真理源 = GM, 不是 BP 角色)
	if (!AttachmentConfig)
	{
		// 【v54.3 大厂重构 — Class 强类型】删除 WeaponID 中间层引用
		//   旧 (v54.2): CharacterID='%s', WeaponID='%s' (FString 字符串)
		//   新 (v54.3): CharacterID 已废弃 (改用 PawnClass), WeaponClass 直接传
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SpawnAndEquipWeapon: 挂载配置缺失 — PawnClass='%s', WeaponClass='%s' 没在 WeaponAttachmentDataTable 中找到. "
			     "【v32 零兜底】不静默用默认值 — 销毁武器 %s, 强制修复 UE 编辑器配置 "
			     "【v37 单一真理源】UE 编辑器 → BP_GM_RoomGameMode → Class Defaults → Room|Data → "
			     "Weapon Attachment DataTable 字段必须配 DT_WeaponAttachmentConfig)."),
			*Owner->GetClass()->GetName(), *WeaponClass->GetName(), *NewWeapon->GetName());
		NewWeapon->Destroy();
		return;
	}

	const FName SocketName = AttachmentConfig->SocketName;
	const FVector RelativeLocation = AttachmentConfig->RelativeLocation;
	const FRotator RelativeRotation = AttachmentConfig->RelativeRotation;
	const FVector RelativeScale = AttachmentConfig->RelativeScale;

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] SpawnAndEquipWeapon: 使用挂载配置: Socket=%s, Loc=%s, Rot=%s"),
		*SocketName.ToString(),
		*RelativeLocation.ToString(),
		*RelativeRotation.ToString());

	// 4. 武器碰撞必须关闭【v72 大厂架构 P0 修复】
	//   根因: 武器 BP 默认 CollisionProfile=BlockAll, Attach 到角色手部后与角色胶囊体重叠
	//   → 物理引擎把角色往反方向推 → "按什么都往后"
	//   修复: Spawn 后立即 SetCollisionEnabled(NoCollision), SwitchToWeaponSlot 时同样处理
	if (UMeshComponent* WeaponMesh = NewWeapon->GetMeshComponent())
	{
		WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	else
	{
		// GetMeshComponent 内部已有 Log Error, 这里只记录
		UE_LOG(LogTemp, Warning,
			TEXT("[WeaponAttachment] SpawnAndEquipWeapon: GetMeshComponent 返回 null, 无法关闭碰撞. Weapon=%s"),
			*NewWeapon->GetName());
	}

	// 5. 将武器焊接到角色的插槽上
	FAttachmentTransformRules AttachmentRules = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
	NewWeapon->AttachToComponent(Owner->GetMesh(), AttachmentRules, SocketName);

	// 6. 应用相对位置、旋转、缩放偏移【v57 大厂架构修复 — 零兜底】
	//   旧 (v56): IsNearlyZero 兜底检查 → 用户配置的 (0,0,0) 偏移被忽略
	//   新 (v57): 始终应用配置值 → 用户配置 (0,0,0) 视为显式意图
	//   大厂原则: IsNearlyZero 是兜底反模式, 强制修复 DT 配置比静默跳过更好
	NewWeapon->SetActorRelativeLocation(RelativeLocation);
	NewWeapon->SetActorRelativeRotation(RelativeRotation);
	NewWeapon->SetActorRelativeScale3D(RelativeScale);

	// 【v57 大厂架构诊断日志】显示实际应用的偏移量
	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] 应用相对偏移: Socket=%s, Loc=%s, Rot=%s, Scale=%s"),
		*SocketName.ToString(),
		*RelativeLocation.ToString(),
		*RelativeRotation.ToString(),
		*RelativeScale.ToString());

	// 7. 更新 CurrentWeapon 指针, 这会触发网络同步
	CurrentWeapon = NewWeapon;

	// 【v60 大厂架构 — 武器初始化】调用 WeaponFireComponent::InitializeFromWeaponConfig
	//   - 注入弹药/射速/换弹/开火模式/蒙太奇 (从 DT_WeaponInfo 读取)
	//   - 玩家/AI 共用同一套初始化路径 (与 v24 DissolveComponent 对称)
	//   - 仅枪械 (Primary/Secondary) 调用, 近战 (Melee) 跳过 (FireComponent 没初始化也能用, 字段 nullptr 校验由 Component 内部做)
	InitializeWeaponFireConfigFromClass(NewWeapon, WeaponClass);

	// 【v71 大厂架构修复】注入 DamageStrategy (之前漏了，导致枪械无法开火)
	//   - 旧路径 (v56-v70): SpawnAndEquipWeapon 调了 InitializeWeaponFireConfigFromClass 但没调 Strategy 注入
	//   - 真实入口 SpawnAndConfigureWeaponInSlot 有 Strategy 注入，但玩家路径走 SpawnAndEquipWeapon
	//   - SpawnAndConfigureWeaponInSlot 的代码在这里内联，确保与双槽路径一致
	EWeaponMeshType ConfigMeshTypeForStrategy = NewWeapon->GetMeshType();
	TScriptInterface<IWeaponDamageStrategy> Strategy;
	switch (ConfigMeshTypeForStrategy)
	{
	case EWeaponMeshType::Melee:
		Strategy.SetObject(NewObject<UMeleeSwStrategy>(NewWeapon));
		Strategy.SetInterface(Cast<IWeaponDamageStrategy>(Strategy.GetObject()));
		break;
	case EWeaponMeshType::Primary:
	case EWeaponMeshType::Secondary:
		Strategy.SetObject(NewObject<URangedLineStrategy>(NewWeapon));
		Strategy.SetInterface(Cast<IWeaponDamageStrategy>(Strategy.GetObject()));
		break;
	case EWeaponMeshType::None:
	default:
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SpawnAndEquipWeapon: MeshType=None — 无法装配 DamageStrategy. Weapon=%s. 修复: DT_WeaponInfo 中该武器的 MeshType 必须设为 Primary/Secondary/Melee."),
			*NewWeapon->GetName());
		break;
	}
	if (Strategy && Strategy.GetInterface())
	{
		NewWeapon->SetDamageStrategy(Strategy);
		UE_LOG(LogTemp, Log,
			TEXT("[WeaponAttachment] SpawnAndEquipWeapon: DamageStrategy 注入成功 — Weapon=%s Strategy=%s"),
			*NewWeapon->GetName(),
			*Strategy.GetObject()->GetClass()->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SpawnAndEquipWeapon: DamageStrategy 注入失败 — Weapon=%s MeshType=%d. 无法开火!"),
			*NewWeapon->GetName(), static_cast<int32>(ConfigMeshTypeForStrategy));
	}

	// 【v61.2 核心修复】MeshType 校验必须在 InitializeWeaponFireConfigFromClass 之后执行
	//   否则 DT_WeaponInfo.MeshType 还没写入 Weapon->MeshType, Gun 被误判为 Melee
	const EWeaponMeshType SpawnedWeaponMeshType = NewWeapon->GetMeshType();
	if (SpawnedWeaponMeshType == EWeaponMeshType::None)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SpawnEquipWeapon: MeshType=None — 武器=%s WeaponClass=%s. ")
			TEXT("【v61.2 零兜底】修复: DT_WeaponInfo 中 '%s' 行的 MeshType 必须设为 Primary/Secondary/Melee."),
			*NewWeapon->GetName(), *WeaponClass->GetName(), *NewWeapon->WeaponRowName.ToString());
	}

	// 8. 刷新战斗 HUD 上的武器图标 (仅本地玩家)
	//
	// 【v54.3 大厂重构 — Class 强类型真理源】HUD 图标的更新由 CharacterIconComponent 独立负责
	//   - 旧 (v54.2): 这里 SpawnAndEquipWeapon 末尾直接调 HUD->UpdateWeaponIconFromID(FString WeaponID)
	//     (耦合! 武器生成 → HUD 图标 → 单一真理源分裂)
	//   - 新 (v54.3): SpawnAndEquipWeapon 只负责生成/挂载/复制, HUD 图标由 CharacterIconComponent::RefreshWeaponIconOnHUD 独立链路处理
	//     - CharacterIconComponent 读 Owner->GetSpawnWeaponID() (Replicated 字符串) → 查 DT_WeaponInfo → 拿 Icon
	//     - 跟武器生成路径完全独立 (大厂原则: 职责对等, 不交叉)
	//
	// 因此这里不再调 HUD.UpdateWeaponIconFromID, 否则会形成两条独立链路, 违背 v40.1 "职责单一"

	// 9. 【v81 大厂架构 — 客户端武器姿态修复】写入挂载运行时数据
	//    服务器写入 → UE 复制 → 客户端 OnRep_CurrentWeapon/OnRep_CurrentWeaponSlot 读取并应用偏移
	//    客户端不再依赖"服务器 Attach + 客户端被 UE 自动同步 Attach 关系" 的隐式行为
	ActiveSlotAttachment.SocketName = SocketName;
	ActiveSlotAttachment.RelativeLocation = RelativeLocation;
	ActiveSlotAttachment.RelativeRotation = RelativeRotation;
	ActiveSlotAttachment.RelativeScale3D = RelativeScale;
	ActiveSlotAttachment.bIsValid = true; // 标记服务器已应用 → 客户端可以信任

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] SpawnAndEquipWeapon: 写入 ActiveSlotAttachment (Replicated) — Socket=%s Loc=%s Rot=%s — 客户端将自动应用"),
		*SocketName.ToString(),
		*RelativeLocation.ToString(),
		*RelativeRotation.ToString());
}


// ==========================================
// 7. SetSpawnLoadout / GetSpawnWeaponID / Setter 接口
// ==========================================

/**
 * SetSpawnLoadout — 统一的武器/角色预填入口
 *
 * 大厂原则 - 显式赋值优于隐式跳过:
 *   - 无论是否为空都写入, 确保外部覆盖意图被尊重
 *   - 空字符串时应显式清空, 而非保留旧值
 */
void UWeaponAttachmentComponent::SetSpawnLoadout(const FString& InCharacterID, const FString& InWeaponID)
{
	// 【2026.07.12 P0 诊断】日志保留 — 排查 Spawn 链路不写入字段问题
	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] SetSpawnLoadout: InCharID='%s', InWeaponID='%s', "
			 "Old CharID='%s', Old WeaponID='%s'"),
		*InCharacterID, *InWeaponID, *CharacterID, *SpawnWeaponID);

	// 无论是否为空都写入, 不再跳过空字符串
	CharacterID = InCharacterID;
	SpawnWeaponID = InWeaponID;
}


/** Setter (用于 ResetSpawnWeaponID 等场景) */
void UWeaponAttachmentComponent::SetSpawnWeaponID(const FString& InWeaponID)
{
	SpawnWeaponID = InWeaponID;
}


/** Setter */
void UWeaponAttachmentComponent::SetCharacterID(const FString& InCharacterID)
{
	CharacterID = InCharacterID;
}


// ==========================================
// 8. SetMeleeConfig — ConfigSO 注入入口 (v54 改名, 原 SetMeleeProfile)
// ==========================================

/**
 * SetMeleeConfig — ConfigSO 直接注入武器配置 (v54 改名, 原 SetMeleeProfile)
 *
 * 设计: SetSpawnLoadout 只做字段写入, 不承担"来源解析"职责.
 *       本方法负责从 UAIBehaviorConfigSO 解析 DefaultWeaponRowName,
 *       然后调用 SetSpawnLoadout 写入.
 *
 * 【v54 大厂架构重构 — UAIProfileAsset 已删除】
 *   - 旧: SetMeleeProfile(UAIProfileAsset*) → 写 Owner->MeleeProfile (字段已删)
 *   - 新: SetMeleeConfig(UAIBehaviorConfigSO*) → 直接写 SetSpawnLoadout
 *   - Owner 不再持有 Profile 字段, Config 由调用方传入 (一次性使用)
 *
 * 【v49 大厂架构重构】
 *   - 删除 CharacterRowName 解析 (字段已废弃)
 *   - 武器挂载查表改用 Pawn->GetClass() 强类型, 不再需要 CharacterID 字符串
 *
 * 调用方:
 *   - AMeleeAIController::SetupMeleeAI 末尾 (关卡预放 AI 自举路径)
 *   - AZombieAIController::SetupZombieAI 末尾
 */
void UWeaponAttachmentComponent::SetMeleeConfig(UAIBehaviorConfigSO* InConfig)
{
	// 【2026.07.12 P0 修复】Owner 单一真理源 = GetOwner()
	//
	// 新架构 (单一真理源 - 不缓存, 直接问 Component 的真理源):
	//   UE 5.6 标准做法: Component 的真理源永远是 GetOwner() (Component 必挂在 Actor 上)
	//   不再用 WeakPtr 缓存 (缓存可能失效, 增加不确定性 — 这就是"隐性兜底")
	//   每次显式问 GetOwner(), 拿到的是 Component 真实挂载的 Actor (100% 准确)
	//   Cast 失败直接 Error + return (零兜底: 失败就是失败, 强制修复 BP 组件挂载)
	//
	// 【v36 重大修复 — 显式优于隐式】拒绝 WeaponID == NAME_None 的隐式容错
	//
	// 旧版 (v32-v35) 反模式:
	//   - Profile.WeaponID == NAME_None 时, WeaponIDStr 保持空字符串 FString()
	//   - 写入 Pawn.SpawnWeaponID = ""
	//   - OnPossess 末尾 GetSpawnWeaponID() 读空字符串 → 跳过武器生成
	//   - 注释自辩"留空 = 不生成武器" = 字面违反零兜底原则
	//
	// 新架构 (v54 — ConfigSO 替代 Profile):
	//   - Config->DefaultWeaponRowName == NAME_None → Log Error + return (拒绝写入)
	//   - 强制用户在 UE 编辑器 → DA_AIBehaviorConfig_XXX → Identity|LevelPlacedAI 配 DT_WeaponInfo 的 RowName
	//   - "留空 = 不生成武器" 改用专门的 bHasWeapon 布尔字段 (策划意图), 不混用 NAME_None
	//   - 大厂原则: WeaponID 字段语义是"必须有值", 不是"可空"
	//
	// 大厂原则 (与"单一真理源"一致):
	//   - 真理源 = GetOwner() (Component 的标准 API, 永远非空除非 Actor 已销毁)
	//   - 不缓存 (缓存失效 = 隐性兜底 = 隐藏 bug)
	//   - 不静默兜底 (Cast 失败立即 Log Error + return, 强制显式修复)
	ABaseCharacter* Owner = Cast<ABaseCharacter>(GetOwner());
	if (!Owner)
	{
		// v40.3 之前此处直接 Log Error — 已上移到 GetOwnerCharacterChecked 集中处理
		return;
	}

	if (!InConfig)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SetMeleeConfig: InConfig 为空. "
				 "【v54 零兜底】拒绝写入. 检查调用方 SetupMeleeAI/SpawnAIInternal 是否传入有效 ConfigSO."));
		return;
	}

	// 【v54.3 大厂架构重构 — 真理源直接 Class, 跳过 DT_WeaponInfo 中间层】拒绝 LevelPlacedWeaponClass.IsNull()
	//   旧 (v54.2): DefaultWeaponRowName (FName) → 查 DT_WeaponInfo[RowName] → 拿 WeaponBlueprint
	//   新 (v54.3): LevelPlacedWeaponClass (TSoftClassPtr<ABaseWeapon>) → LoadSynchronous → 直接 Class
	//   - 跳过 SpawnWeaponID 字符串中转 + 跳过 SpawnAndEquipWeapon 内 DT_WeaponInfo 反查
	//   - 单一真理源: Config.LevelPlacedWeaponClass 直接决定武器 BP
	if (InConfig->LevelPlacedWeaponClass.IsNull())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SetMeleeConfig: Config=%s 的 LevelPlacedWeaponClass 为空 (TSoftClassPtr null). "
				 "【v54.3 零兜底】拒绝生成武器. "
				 "【修复】UE 编辑器 → DA_AIBehaviorConfig_%s → LevelPlacedAI → LevelPlacedWeaponClass 字段必须配 BP_Weapon_*.uasset. "
				 "如果 AI 设计上不需要武器, 应该让字段 = null 是显式错误, 不该静默. "
				 "【大厂原则】配置错必须显式化, 不允许用 null 当 'no weapon' 标志位."),
			*InConfig->GetName(), *InConfig->GetName());
		// 拒绝写入 (Pawn.SpawnWeaponID 保持空, 后续链路会 Log Error, 强制修复)
		return;
	}

	// 【v54.3 大厂原则 — 真理源直接传递】LoadSynchronous 必须成功
	TSubclassOf<ABaseWeapon> WeaponClass = InConfig->LevelPlacedWeaponClass.LoadSynchronous();
	if (!WeaponClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SetMeleeConfig: Config=%s 的 LevelPlacedWeaponClass.LoadSynchronous() 失败. "
				 "【v54.3 零兜底】资产可能已被删除或路径错. "
				 "【修复】UE 编辑器 → DA_AIBehaviorConfig_%s → LevelPlacedWeaponClass 字段重新选 BP."),
			*InConfig->GetName(), *InConfig->GetName());
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] SetMeleeConfig: Config=%s, DesiredWeaponClass=%s (真理源直接持 BP, 跳过 DT_WeaponInfo 中间层)"),
		*InConfig->GetName(), *WeaponClass->GetName());

	// 【v54.3 重构】直接调 RequestWeaponSpawn(Class) — 不再走 SpawnWeaponID 字符串中转
	RequestWeaponSpawn(WeaponClass);
}


// 【v54 大厂架构重构 — UAIProfileAsset 整个类已删除】SetMeleeProfile 函数整个删除
//   旧版签名: SetMeleeProfile(UAIProfileAsset*) — UAIProfileAsset 类不存在, 无法保留编译
//   新版签名: SetMeleeConfig(UAIBehaviorConfigSO*) — 见本文件上方 SetMeleeConfig 实现
//   所有调用方已迁移到 SetMeleeConfig


// ==========================================
// 9. SyncWeaponFromConfig — 从 ConfigSO 同步字段 (v54 改名, 原 SyncWeaponFromProfile)
// ==========================================

/**
 * SyncWeaponFromConfig — 从传入 ConfigSO 同步武器数据 (v54 改名, 原 SyncWeaponFromProfile)
 *
 * 【v54 大厂架构重构 — UAIProfileAsset 已删除】
 *   - 旧: SyncWeaponFromProfile() → 读 Owner->MeleeProfile 字段
 *   - 新: SyncWeaponFromConfig(UAIBehaviorConfigSO*) → 调方传入 Config, 不缓存字段
 *   - Owner 不再持有 Profile 字段, 不再缓存
 *
 * 调用方:
 *   - AMeleeAIController::SetupMeleeAI 末尾 (关卡预放 AI 自举路径)
 *   - AZombieAIController::SetupZombieAI 末尾
 *
 * 逻辑:
 *   1. 如果 SpawnWeaponID 为空, 从 Config.DefaultWeaponRowName 填充
 *   2. 诊断日志记录同步结果
 *
 * 【v49 大厂重构】删除 CharacterID 同步 (改用 Pawn->GetClass() 强类型查表)
 */
void UWeaponAttachmentComponent::SyncWeaponFromConfig()
{
	// 【v54 大厂架构】本函数保留实现兼容性, 但实际写入已在 SetMeleeConfig 完成
	// 这里不再从任何字段读取, 而是由外部直接调 SetMeleeConfig(InConfig) → 完成 SetSpawnLoadout
	// 保留为空实现是为了不破坏外部已有调用方
}


// 【v54 大厂架构重构 — Owner->MeleeProfile 字段已删除】SyncWeaponFromProfile 函数整个删除
//   旧版签名: SyncWeaponFromProfile() — 从已删除的 Owner->MeleeProfile 字段读
//   新版签名: SyncWeaponFromConfig() — 见本文件上方 SyncWeaponFromConfig 实现
//   所有调用方已迁移到 SyncWeaponFromConfig (因为 Owner->MeleeProfile 字段不存在)


// ==========================================
// 10. FindWeaponAttachmentConfig — 严格匹配 (Phase 1.3 零兜底)
// ==========================================

/**
 * FindWeaponAttachmentConfig — 在 WeaponAttachmentDataTable 中查找挂载配置 (v32 严格匹配 — 零兜底)
 *
 * 【Phase 1.3 修复 — 零兜底】删除通配 fallback
 *
 * 历史 (v22-v31.x) 反模式:
 *   - 注释自承"退而求其次, 记录一个通配配置作为 fallback"
 *   - 找不到精确匹配时, 静默返回"通配占位行" (通常是设计师留的占位行)
 *   - 武器用错位置/Socket → 玩家视觉上"武器位置飘了"但**没有任何 Log Error**
 *   - 通配 fallback 命中时**不打日志**, 只在 !FallbackMatch 时打 Warning
 *
 * 新架构 (Phase 1.3 — 零兜底):
 *   - 严格匹配: 仅匹配 TargetCharacter + WeaponBlueprint 都精确对齐的行
 *   - 找不到精确 → Log Error + return nullptr
 *   - 任何降级"通配兼容"被删除, 武器挂载配置错误必须显式化
 *
 * 大厂原则:
 *   - 配置缺失 → Log Error (不是 Warning)
 *   - 配置错误 = 大厂零兜底禁区, 必须强制修复 WeaponAttachmentDataTable
 */
FWeaponAttachmentConfig* UWeaponAttachmentComponent::FindWeaponAttachmentConfig(
	TSubclassOf<ABaseCharacter> InPawnClass,
	TSubclassOf<ABaseWeapon> InWeaponClass) const
{
	ABaseCharacter* Owner = GetOwnerCharacterChecked(this);
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] FindWeaponAttachmentConfig: Owner 无效. "
				 "【v40.3 零兜底】Component 必须挂在 ABaseCharacter 上. "
				 "如果连接到了非 ABaseCharacter 的 Actor 上, 请修复 BP 子类化 (BP_*.WeaponAttach 父类)."));
		return nullptr;
	}

	// 【v49 零兜底】参数校验
	if (!InPawnClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] FindWeaponAttachmentConfig: InPawnClass 为空. "
			     "【修复路径1】调用方应传入 Pawn->GetClass() (BP 强类型). "
			     "【修复路径2】OnRep_CurrentWeapon 路径应从 CurrentWeapon->GetClass() 反查."));
		return nullptr;
	}
	if (!InWeaponClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] FindWeaponAttachmentConfig: InWeaponClass 为空. "
			     "【修复路径】调用方应传入 CurrentWeapon->GetClass() (BP 强类型)."));
		return nullptr;
	}

	static const FString ContextString(TEXT("WeaponAttachmentLookup"));

	// 【v37 单一真理源】从 GM 拿 DataTable, 不再用组件字段 (字段已删)
	UDataTable* WeaponAttachmentDataTable = GetWeaponAttachmentDataTable();
	if (!WeaponAttachmentDataTable)
	{
		// GetWeaponAttachmentDataTable 内部已 Log Error, 这里不再重复
		return nullptr;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] 查找配置: PawnClass=%s, WeaponClass=%s, Owner=%s"),
		*InPawnClass->GetName(), *InWeaponClass->GetName(), *Owner->GetName());

	TArray<FName> RowNames = WeaponAttachmentDataTable->GetRowNames();

	for (const FName& RowName : RowNames)
	{
		FWeaponAttachmentConfig* Config = WeaponAttachmentDataTable->FindRow<FWeaponAttachmentConfig>(RowName, ContextString);
		if (!Config)
		{
			continue;
		}

		// ============================================================
		// 【v49 大厂架构重构】按 Class 精确匹配 — 单一真理源
		//
		// 旧 (v32-v48): TargetCharacter 配 TSoftClassPtr, WeaponBlueprint 配 TSoftClassPtr
		//              → 实际匹配时按 IsChildOf 等逻辑, 容易错位
		//
		// 新 (v49): TargetCharacter Class == InPawnClass 且 WeaponBlueprint Class == InWeaponClass
		//         → BP 强类型比对, UE 编辑器强约束
		//         → 不允许 IsChildOf 模糊匹配 (避免"父类行匹配子类 Pawn"的不可预期行为)
		// ============================================================

		// 1. TargetCharacter 匹配 (Class 精确比对)
		if (Config->TargetCharacter.IsNull())
		{
			// TargetCharacter 留空 → 通配行 (大厂原则禁止, 但兼容旧配置)
			UE_LOG(LogTemp, Warning,
				TEXT("[WeaponAttachment] FindWeaponAttachmentConfig: RowName='%s' 的 TargetCharacter 留空, 视为通配行. "
				     "【v49 警告】大厂原则禁止通配 — 必须显式配 TargetCharacter."),
				*RowName.ToString());
		}
		else
		{
			UClass* TargetCharClass = Config->TargetCharacter.LoadSynchronous();
			if (!TargetCharClass)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[WeaponAttachment] FindWeaponAttachmentConfig: RowName='%s' 的 TargetCharacter 加载失败."),
					*RowName.ToString());
				continue;
			}
			if (TargetCharClass != InPawnClass)
			{
				continue;  // PawnClass 不匹配 → 跳过
			}
		}

		// 2. WeaponBlueprint 匹配 (Class 精确比对)
		if (Config->WeaponBlueprint.IsNull())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[WeaponAttachment] FindWeaponAttachmentConfig: RowName='%s' 的 WeaponBlueprint 留空. "
				     "【v49 警告】拒绝通配 — 必须显式配 WeaponBlueprint."),
				*RowName.ToString());
			continue;  // 武器必须显式配
		}

		UClass* ConfigWeaponClass = Config->WeaponBlueprint.LoadSynchronous();
		if (!ConfigWeaponClass)
		{
			continue;
		}
		if (ConfigWeaponClass != InWeaponClass)
		{
			continue;  // WeaponClass 不匹配 → 跳过
		}

		// 3. 精确匹配成功
		UE_LOG(LogTemp, Log,
			TEXT("[WeaponAttachment] 找到精确匹配配置: RowName='%s' (PawnClass=%s, WeaponClass=%s)"),
			*RowName.ToString(), *InPawnClass->GetName(), *InWeaponClass->GetName());
		return Config;
	}

	// 【Phase 1.3 零兜底改造】删除通配 fallback
	//
	// 历史 (v22-v31.x) 反模式:
	//   - 上面 for 循环找不到精确匹配 → 退到通配占位行
	//   - 通配 fallback 命中时不打日志, 武器位置错乱但没报警
	//   - 注释自辩"退而求其次" = 字面违反零兜底原则
	//
	// 新行为 (Phase 1.3):
	//   - Log Error + return nullptr
	//   - 调用方 (OnRep_CurrentWeapon / SpawnAndEquipWeapon) 拿到 nullptr → 武器不挂载
	//   - 玩家看到"武器没显示" > "武器位置错乱" (后者让 BP 配置错被永远掩盖)
	UE_LOG(LogTemp, Error,
		TEXT("[WeaponAttachment] 未找到精确匹配配置: PawnClass=%s, WeaponClass=%s. "
			 "Pawn=%s. "
			 "【v49 大厂原则】拒绝返回通配 fallback — 必须修复 WeaponAttachmentDataTable, "
			 "添加 (TargetCharacter=PawnClass + WeaponBlueprint=WeaponClass) 精确匹配行. "
			 "打开 GM_RoomGameMode Class Defaults → Room|Data → Weapon Attachment DataTable 找到 DT 配置."),
		*InPawnClass->GetName(), *InWeaponClass->GetName(), *Owner->GetName());
	return nullptr;
}

// =============================================================================
// 【v37 单一真理源】GetWeaponAttachmentDataTable — 从 GM 拿武器挂载配置 DataTable
//
// 真理源迁移 (v37 大厂重构):
//   - 旧 (v32-v36): UWeaponAttachmentComponent 内每个 BP 都需要配这个字段
//     - BP_BaseCharacter / BP_SWAT_C / BP_GruntAI 各自配一次
//     - 配置反模式: 3 个 BP 持有同一资产 → 用户实际踩坑: 全部忘配
//   - 新 (v37): 集中在 GameMode 配一次, 所有角色通过 GM 拿到
//     - BP_GM_RoomGameMode → Class Defaults → Room|Data → Weapon Attachment DataTable
//     - 找不到 / 为空 → Log Error + return nullptr (零兜底 — 强制修 GM)
// =============================================================================
UDataTable* UWeaponAttachmentComponent::GetWeaponAttachmentDataTable() const
{
	ABaseCharacter* Owner = GetOwnerCharacterChecked(this);
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] GetWeaponAttachmentDataTable: Owner 无效 (已 Destroy 或未初始化)."));
		return nullptr;
	}

	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] GetWeaponAttachmentDataTable: World 无效. Pawn=%s"),
			*Owner->GetName());
		return nullptr;
	}

	ARoomGameMode* GM = World->GetAuthGameMode<ARoomGameMode>();
	if (!GM)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] GetWeaponAttachmentDataTable: AuthGameMode 不是 ARoomGameMode (类型=%s). "
				 "Pawn=%s. 【v37 单一真理源】武器挂载表只能在 ARoomGameMode 派生类中配."),
			*GetNameSafe(World->GetAuthGameMode()), *Owner->GetName());
		return nullptr;
	}

	UDataTable* ResultTable = GM->WeaponAttachmentDataTable;
	if (!ResultTable)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] GetWeaponAttachmentDataTable: GM->WeaponAttachmentDataTable 为空! "
				 "Pawn=%s. 【v37 单一真理源】必须在 BP_GM_RoomGameMode 的 Class Defaults → Room|Data → "
				 "Weapon Attachment DataTable 字段配 DT_WeaponAttachmentConfig. "
				 "【零兜底】不许默认分配, 不许 fallback."),
			*Owner->GetName());
		return nullptr;
	}

	return ResultTable;
}


// =============================================================================
// 【v51 大厂架构 — 生化模式】双槽位系统实现
//
// 设计原则:
//   - 复用现有 SpawnAndEquipWeapon 链路 (查 DT_WeaponInfo + 挂载配置)
//   - 在 Spawn 完成后, 根据 BP 配置的 MeshType 注入 Strategy
//   - 玩家路径使用, AI 路径继续走 CurrentWeapon 单字段 (向后兼容)
// =============================================================================


// -----------------------------------------------------------------------------
// 1. GetWeaponInSlot — 槽位查询 (BlueprintPure)
// -----------------------------------------------------------------------------
ABaseWeapon* UWeaponAttachmentComponent::GetWeaponInSlot(EWeaponSlotType Slot) const
{
	// 零兜底: Slot 非法 → 返回 nullptr (合法状态, 调用方按 nullptr 跳过)
	if (Slot == EWeaponSlotType::None)
	{
		return nullptr;
	}

	const int32 Index = SlotTypeToArrayIndex(Slot);
	if (!WeaponsInSlot.IsValidIndex(Index))
	{
		return nullptr;
	}

	return WeaponsInSlot[Index];
}

/**
 * HasWeaponInSlot — 检查槽位是否有武器
 */
bool UWeaponAttachmentComponent::HasWeaponInSlot(EWeaponSlotType Slot) const
{
	return GetWeaponInSlot(Slot) != nullptr;
}


// -----------------------------------------------------------------------------
// 2. Server_SwitchToWeaponSlot — 槽位切换 (服务器权威)
// -----------------------------------------------------------------------------
void UWeaponAttachmentComponent::Server_SwitchToWeaponSlot(EWeaponSlotType TargetSlot)
{
	ABaseCharacter* Owner = GetOwnerCharacterChecked(this);
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] Server_SwitchToWeaponSlot: Owner 无效 — 拒绝切槽位."));
		return;
	}

	// 零兜底: 仅服务器可切 (RPC 路由进来后已保证服务器执行, 但显式校验防客户端误调)
	if (!Owner->HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] Server_SwitchToWeaponSlot: 调用方非服务器 (Pawn=%s) — 拒绝切槽位. "
			     "必须通过 BaseCharacter::Server_SwitchToWeaponSlot RPC 调用."),
			*Owner->GetName());
		return;
	}

	// 零兜底: 槽位合法
	if (TargetSlot == EWeaponSlotType::None)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] Server_SwitchToWeaponSlot: TargetSlot=None — 拒绝切. Pawn=%s"),
			*Owner->GetName());
		return;
	}

	// 零兜底: 目标槽位必须有武器
	ABaseWeapon* TargetWeapon = GetWeaponInSlot(TargetSlot);
	if (!TargetWeapon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] Server_SwitchToWeaponSlot: 目标槽位 %s 没有武器 — 拒绝切. Pawn=%s. "
			     "【修复】确保玩家大厅已选两把武器 (SelectedWeaponID1 + SelectedWeaponID2), 或 Server_SpawnBothWeapons 已调用."),
			LexToString(TargetSlot), *Owner->GetName());
		return;
	}

	// 同槽位 → no-op (幂等)
	if (CurrentWeaponSlot == TargetSlot)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[WeaponAttachment] Server_SwitchToWeaponSlot: 同槽位 %s, no-op. Pawn=%s"),
			LexToString(TargetSlot), *Owner->GetName());
		return;
	}

	// ============================================================
	// 执行切换
	// ============================================================
	const EWeaponSlotType OldSlot = CurrentWeaponSlot;
	ABaseWeapon* OldWeapon = GetWeaponInSlot(OldSlot);

	// 【v78 大厂架构 — 单一复制链】先更新 WeaponState（真理源）
	//   UE 保证 WeaponState 内所有字段（ActiveWeapon + ActiveSlot）同步复制到客户端
	//   替代旧版分别写 CurrentWeapon + CurrentWeaponSlot（独立复制导致时序不一致）
	FWeaponState NewState;
	NewState.ActiveWeapon = TargetWeapon;
	NewState.ActiveSlot = TargetSlot;
	WeaponState = NewState;

	// 1. 隐藏旧槽位武器 (保留 Spawn, 仅不可见 — 防切换 GC 抖动)
	if (OldWeapon && OldWeapon != TargetWeapon)
	{
		OldWeapon->SetActorHiddenInGame(true);
		OldWeapon->SetActorEnableCollision(false);
	}

	// 2. 显示新槽位武器
	// 【v58.1 大厂架构 P0】先校验武器的 ActiveMesh, 没有 mesh 不能启用碰撞
	//   根因: BP 配错 (没 StaticMeshComponent) 时, UE 自动建 DefaultSceneRoot (单位立方体, BlockAll collision)
	//   → SetActorEnableCollision(true) 后武器变成"看不见的墙"卡住玩家
	//   → 用户看到的"切到近战武器后 WASD 全部后退"就是这个 ghost collision 推角色
	//   修复: 强制校验 ActiveMesh, 没有 mesh → 拒绝启用 collision (避免幽灵碰撞)
	//         + Log Error 让用户知道配置错
	UMeshComponent* TargetActiveMesh = TargetWeapon->GetMeshComponent();
	if (!TargetActiveMesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] Server_SwitchToWeaponSlot: 目标武器无 ActiveMesh — 拒绝启用 collision. "
			     "Weapon=%s Pawn=%s. 【v58.1 大厂原则 — 零幽灵碰撞】启用 collision 而没有 mesh → UE 创建 BlockAll 立方体把玩家挡住. "
			     "【修复】在 BP_%s 的 Components 面板加 mesh 组件 (Melee=StaticMesh, Primary/Secondary=SkeletalMesh) 并配置 mesh 引用."),
			*TargetWeapon->GetName(),
			*Owner->GetName(),
			*TargetWeapon->GetClass()->GetName());
		// 仍然隐藏并显示 (用户可能想看 BP 错误), 但拒绝启用 collision
		TargetWeapon->SetActorHiddenInGame(false);
	}
	else
	{
		TargetWeapon->SetActorHiddenInGame(false);
		// 【v72 大厂架构 P0 修复】武器绝对不能启用碰撞！
		//   旧版: SetActorEnableCollision(true) → 武器与角色胶囊体重叠 → 物理引擎推角色
		//   → 用户看到"切到近战武器后 WASD 全部后退"
		//   修复: 武器始终 NoCollision, 由游戏逻辑自己控制命中检测
		if (UMeshComponent* WeaponMesh = TargetWeapon->GetMeshComponent())
		{
			WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	// 3. 更新 CurrentWeapon (向后兼容单武器 API — 所有调用 GetCurrentWeapon() 的代码无需改)
	CurrentWeapon = TargetWeapon;

	// 4. 更新 CurrentWeaponSlot (向后兼容 — 真理源在 WeaponState)
	CurrentWeaponSlot = TargetSlot;

	// 【v58 防御性日志】切到 MeshType=None 的武器 — 警告用户 BP 配置错误
	//   GetActiveMesh() 会返回 nullptr → 武器不显示 → 用户看到"武器切了但不显示"
	//   这不是 Bug, 是 BP 没配 MeshType
	const EWeaponMeshType ActiveMeshType = TargetWeapon->GetMeshType();
	if (ActiveMeshType == EWeaponMeshType::None)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] Server_SwitchToWeaponSlot: 切到 MeshType=None 的武器 — 武器不会显示在角色手上! "
			     "Weapon=%s Slot=%s MeshType=%d. "
			     "【v58 大厂原则 — 零兜底】必须在 BP 蓝图 (BP_%s) Class Defaults → Mesh → MeshType 配置为 Melee/Primary/Secondary."),
			*TargetWeapon->GetName(),
			LexToString(TargetSlot),
			static_cast<int32>(ActiveMeshType),
			*TargetWeapon->GetClass()->GetName());
	}

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] Server_SwitchToWeaponSlot: 切换槽位 — Pawn=%s 旧=%s 新=%s 武器=%s (MeshType=%d)"),
		*Owner->GetName(),
		LexToString(OldSlot),
		LexToString(TargetSlot),
		*TargetWeapon->GetName(),
		static_cast<int32>(TargetWeapon->GetMeshType()));

	// 【v76 大厂架构】播放"拿起武器"音效 (服务器权威触发, 客户端通过 Multicast 听到)
	//   单一入口: 任何"拿起武器"事件都走这一条 — 与 Server_SwitchToWeaponSlot 是同一时刻
	//   大厂原则 - 零兜底:
	//     - GM/Asset 未配 → Log Error (在 GetWeaponSoundMapAsset / DA 内部), 本函数静默忽略
	//     - Slot.Entry.Sound 为空 → Log Error + 拒绝播放
	PlayEquipSoundForSlot(TargetSlot);

	// 【v81 大厂架构 — 客户端武器姿态修复】写入新槽位的挂载运行时数据
	//   切换到 TargetSlot 时, 必须用目标武器的挂载配置刷新 ActiveSlotAttachment
	//   → 客户端 OnRep_CurrentWeaponSlot 触发 → 读取并应用新槽位的偏移
	{
		FWeaponAttachmentConfig* TargetSlotConfig = FindWeaponAttachmentConfig(
			Owner->GetClass(), TargetWeapon->GetClass());
		if (TargetSlotConfig)
		{
			ActiveSlotAttachment.SocketName = TargetSlotConfig->SocketName;
			ActiveSlotAttachment.RelativeLocation = TargetSlotConfig->RelativeLocation;
			ActiveSlotAttachment.RelativeRotation = TargetSlotConfig->RelativeRotation;
			ActiveSlotAttachment.RelativeScale3D = TargetSlotConfig->RelativeScale;
			ActiveSlotAttachment.bIsValid = true;

			UE_LOG(LogTemp, Log,
				TEXT("[WeaponAttachment] Server_SwitchToWeaponSlot: 刷新 ActiveSlotAttachment — Slot=%s Socket=%s"),
				LexToString(TargetSlot),
				*TargetSlotConfig->SocketName.ToString());
		}
		else
		{
			// 零兜底: 找不到配置 → 不写 ActiveSlotAttachment (bIsValid 保持 false)
			//   客户端 OnRep 会检测到 bIsValid=false → Log Error + 强制修复
			UE_LOG(LogTemp, Error,
				TEXT("[WeaponAttachment] Server_SwitchToWeaponSlot: TargetSlot=%s 的挂载配置缺失 — ActiveSlotAttachment.bIsValid 保持 false. "
				     "PawnClass=%s WeaponClass=%s. 【v81 零兜底】客户端将看到 Log Error, 请修复 DT_WeaponAttachmentConfig."),
				LexToString(TargetSlot),
				*Owner->GetClass()->GetName(),
				*TargetWeapon->GetClass()->GetName());
		}
	}

	// 【v58 大厂架构诊断】切换后检查武器是否真的可见
	//   排查 "切过去了但看不到" 的根因: Hidden / 位置 / Attach 失败
	//   GetMeshComponent 是 BaseWeapon public wrapper (GetActiveMesh 是 protected)
	UMeshComponent* ActiveMesh = TargetWeapon->GetMeshComponent();
	const FVector WeaponWorldLoc = TargetWeapon->GetActorLocation();
	const bool bIsHidden = TargetWeapon->IsHidden();
	// GetAttachSocketName 是 USceneComponent 方法, 武器 Actor 的根组件就是武器 mesh
	const FName AttachedSocket = TargetWeapon->GetRootComponent()
		? TargetWeapon->GetRootComponent()->GetAttachSocketName()
		: NAME_None;

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] Server_SwitchToWeaponSlot: 切换后诊断 — Weapon=%s "
		     "Hidden=%d AttachSocket='%s' ActiveMesh=%s WorldLoc=%s "
		     "(若 Hidden=true → SetActorHiddenInGame 没生效; "
		     "若 AttachSocket 为 NAME_None → 没 Attach 到 Mesh; "
		     "若 ActiveMesh=null → BP 没配 MeshType 或 mesh 组件)"),
		*TargetWeapon->GetName(),
		bIsHidden ? 1 : 0,
		*AttachedSocket.ToString(),
		ActiveMesh ? *ActiveMesh->GetName() : TEXT("<null>"),
		*WeaponWorldLoc.ToCompactString());

	// 【v84 大厂架构修复】武器切换完成后必须广播弹夹信息
	// 根因: 切枪后 HUD 应显示新武器的弹夹数量 (近战=1/1, 枪械=实际弹药)
	// 解决方案: Server_SwitchToWeaponSlot 完成后立即广播弹夹数量
	if (UCharacterIconComponent* IconComp = Owner->FindComponentByClass<UCharacterIconComponent>())
	{
		// 1. 先刷新武器图标 (服务器查表, RPC 推 Icon)
		FString NewWeaponID = TargetWeapon->WeaponRowName.ToString();
		IconComp->RefreshWeaponIconOnHUDFromServer(NewWeaponID);

		// 2. 再刷新弹夹数量
		IconComp->BroadcastWeaponAmmoInfo(Owner);
	}
}


// -----------------------------------------------------------------------------
// 【v78 大厂架构】OnRep_CurrentWeaponSlot — 客户端槽位同步回调
// -----------------------------------------------------------------------------
//
// 【v78 大厂架构 — 单一真理源】
//   CurrentWeaponSlot 通过 ReplicatedUsing 触发本回调
//   WeaponState 通过 DOREPLIFETIME 普通复制（同步 ActiveWeapon + ActiveSlot）
//
//   本 OnRep 读取 WeaponState 作为真理源，执行所有显示逻辑
//   如果 WeaponState.ActiveSlot != CurrentWeaponSlot，说明复制顺序不一致，用 WeaponState
//
void UWeaponAttachmentComponent::OnRep_CurrentWeaponSlot(EWeaponSlotType OldSlot)
{
	ABaseCharacter* Owner = GetOwnerCharacterChecked(this);
	if (!Owner)
	{
		return;
	}

	// 【v78 零兜底】WeaponState 是真理源，必须有效
	if (WeaponState.ActiveSlot == EWeaponSlotType::None)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] OnRep_CurrentWeaponSlot: WeaponState.ActiveSlot=None — 非法状态! Pawn=%s. ")
			TEXT("【v78 大厂原则】拒绝执行切换，强制修复服务器 SwitchToWeaponSlot 链路."),
			*Owner->GetName());
		return;
	}

	// 【v78 大厂架构 — 单一真理源】
	//   CurrentWeaponSlot 可能和 WeaponState.ActiveSlot 不一致（复制顺序问题）
	//   使用 WeaponState.ActiveSlot 作为真理源
	const EWeaponSlotType TrueSlot = WeaponState.ActiveSlot;

	// 防御性检查：如果 CurrentWeaponSlot（触发源）和 WeaponState.ActiveSlot（真理源）不一致
	if (TrueSlot != CurrentWeaponSlot)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[WeaponAttachment] OnRep_CurrentWeaponSlot: WeaponState.ActiveSlot(%s) != CurrentWeaponSlot(%s) — 复制顺序不一致! ")
			TEXT("使用 WeaponState.ActiveSlot 作为真理源. Pawn=%s"),
			LexToString(TrueSlot),
			LexToString(CurrentWeaponSlot),
			*Owner->GetName());
	}

	// 隐藏旧槽位武器
	if (ABaseWeapon* OldWeapon = GetWeaponInSlot(OldSlot))
	{
		OldWeapon->SetActorHiddenInGame(true);
		OldWeapon->SetActorEnableCollision(ECollisionEnabled::NoCollision);
	}

	// 显示新槽位武器 — 优先使用 WeaponState.ActiveWeapon（真理源）
	if (ABaseWeapon* NewWeapon = WeaponState.ActiveWeapon)
	{
		NewWeapon->SetActorHiddenInGame(false);

		if (UMeshComponent* WeaponMesh = NewWeapon->GetMeshComponent())
		{
			WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[WeaponAttachment] OnRep_CurrentWeaponSlot: 客户端武器无 ActiveMesh. Weapon=%s Pawn=%s."),
				*NewWeapon->GetName(), *Owner->GetName());
		}

		// 更新向后兼容字段
		CurrentWeapon = NewWeapon;
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[WeaponAttachment] OnRep_CurrentWeaponSlot: WeaponState.ActiveWeapon=null. Pawn=%s Slot=%s"),
			*Owner->GetName(), LexToString(TrueSlot));
	}

	// 更新向后兼容字段
	CurrentWeaponSlot = TrueSlot;

	// 【v81 大厂架构 — 客户端武器姿态修复】槽位切换后必须应用新槽位的挂载偏移
	//   服务器 Server_SwitchToWeaponSlot 末尾已写入 ActiveSlotAttachment (新槽位)
	//   客户端 OnRep 触发 → 应用服务器配置的新槽位偏移
	ApplyAttachmentRuntime(WeaponState.ActiveWeapon);

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] OnRep_CurrentWeaponSlot: Pawn=%s 旧=%s 新=%s ActiveWeapon=%s"),
		*Owner->GetName(),
		LexToString(OldSlot),
		LexToString(TrueSlot),
		*GetNameSafe(WeaponState.ActiveWeapon));
}

// ==========================================
// 【v76 大厂架构 — 武器切换音效】PlayEquipSoundForSlot 单入口实现
// ==========================================
//
// 单一入口 (零重复):
//   - 唯一调用方: Server_SwitchToWeaponSlot 末尾 (服务器权威)
//   - 客户端播放由 Multicast RPC 负责 — 不在本函数内重复 (防双发)
//   - 不用 OnRep_CurrentWeaponSlot 路径 (服务器切换自己 / 已本地控制的 Client 触发的是 Server 路径,
//     远端 Client 触发的是 OnRep,但远端 Client 的"拿起音效"通过 Server → Multicast 在 OnRep 之前到)
//
// 大厂原则 - 单一真理源 (与 v37 WeaponAttachmentDataTable 完全对称):
//   - 数据流: Server_SwitchToWeaponSlot → GetAuthGameMode → GetWeaponSoundMapAsset → FindSoundForSlot → Multicast_RPC
//   - 不在 BP 蓝图 / 武器字段 / Component 内重复配置
//
// 大厂原则 - 零兜底:
//   - GM 未配 → GetWeaponSoundMapAsset 已 Log Error, 本函数直接返回
//   - Slot.Entry.Sound 为空 → Log Error (强制修复 DA_WeaponSoundMap.uasset)
//
// 大厂原则 - 可观测性:
//   - 成功播放 → Display Log (玩家和测试都能看到)
//   - 失败 (任何一层) → Error Log (运维发现)
//
// 大厂原则 - 网络策略:
//   - 服务器播本地 (Multicast RPC 内部对服务器自己也会广播一次)
//   - 所有客户端收 Multicast → 播放
//   - 不让远端 Client 自己 OnRep 时再播一次 (避免双发)
// ==========================================
void UWeaponAttachmentComponent::PlayEquipSoundForSlot(EWeaponSlotType NewSlot)
{
	// 1. 不在客户端触发 (Multicast RPC 由服务器权威发出)
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return; // 客户端路径: Multicast RPC 自己会触发播放
	}

	// 2. 真理源: GM 单点访问
	ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode());
	if (!GM)
	{
		// 极少见: 测试模式可能没 GM (但 Server_SwitchToWeaponSlot 通常在战斗中触发)
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] PlayEquipSoundForSlot: GetAuthGameMode 不是 ARoomGameMode (类型=%s) — 拒绝播放. "
			     "【v76 大厂原则 — 零兜底】检查地图 GameMode Override."),
			*GetNameSafe(GetWorld()->GetAuthGameMode()));
		return;
	}

	UWeaponSoundMapAsset* SoundMapAsset = GM->GetWeaponSoundMapAsset();
	if (!SoundMapAsset)
	{
		// GetWeaponSoundMapAsset 内部已 Log Error, 本函数不重复
		return;
	}

	const FWeaponSlotSoundEntry* Entry = SoundMapAsset->FindSoundForSlot(NewSlot);
	if (!Entry)
	{
		// FindSoundForSlot 内部已 Log Error
		return;
	}

	// 3. 零兜底: Entry 内 Sound 字段为空 → 强制修复 DA
	if (!Entry->Sound)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] PlayEquipSoundForSlot: Slot=%s 的 Sound 字段为空. "
			     "【v76 大厂原则 — 零兜底】打开 DA_WeaponSoundMap.uasset → Slots 分类 → %s → Sound 字段必须配 SoundCue / SoundWave / MetaSoundSource. "
			     "未配 → 拒绝播放, 强制修复 DA."),
			LexToString(NewSlot),
			LexToString(NewSlot));
		return;
	}

	// 4. Multicast RPC — 服务器权威广播, 所有客户端听
	//   RPC 必须在 Actor 上 (UE 5.6 硬约束), 转交给 Owner Actor 调
	ABaseCharacter* OwnerChar = Cast<ABaseCharacter>(GetOwner());
	if (!OwnerChar)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] PlayEquipSoundForSlot: Owner 不是 ABaseCharacter (类型=%s) — 无法发 Multicast RPC. "
			     "【v76 零兜底】检查组件挂载点."),
			*GetNameSafe(GetOwner()));
		return;
	}

	OwnerChar->Multicast_PlayEquipSoundForSlot(NewSlot, Entry->Sound, Entry->VolumeMultiplier, Entry->PitchMultiplier);

	UE_LOG(LogTemp, Display,
		TEXT("[WeaponAttachment] PlayEquipSoundForSlot: Pawn=%s 切到 Slot=%s 发 Multicast RPC — Sound=%s Vol=%.2f Pitch=%.2f"),
		*GetOwner()->GetName(),
		LexToString(NewSlot),
		*Entry->Sound->GetName(),
		Entry->VolumeMultiplier,
		Entry->PitchMultiplier);
}


// -----------------------------------------------------------------------------
// 4. Server_SpawnAllWeapons — 3 槽位一次性 Spawn (服务器权威, v52 P0)
//
// 业务模型 (v52):
//   - 每个玩家有 3 个槽位: Primary (主武器) + Secondary (副武器) + Melee (近战)
//   - 大厅允许玩家选 3 把武器, 战斗开局一次 Spawn 全部
//   - Secondary / Melee 可为空 (玩家可能只选 1 把或 2 把)
//   - Primary 可空 (v60.6 大厂重构 — 允许玩家大厅不选主武器, 改用刀开局)
//
// 【v60.6 大厂重构 — 槽位独立性 + 智能默认】
//   - 旧 (v52-v60.5) 反模式: Primary 失败 → 整个流程 return → 用户"枪没配齐刀也用不了"
//   - 新 (v60.6): 每个槽位独立 Spawn, 独立失败, 独立 Log Error
//   - Primary 失败但 Melee 成功 → 自动退到 Melee 作为当前武器
//   - 全部失败 → 显式 Log Error (玩家没任何武器)
//
// 旧 v51 实现 (Server_SpawnBothWeapons) 仅 2 槽位 (Primary + Melee), v52 扩展
// -----------------------------------------------------------------------------
void UWeaponAttachmentComponent::Server_SpawnAllWeapons(TSubclassOf<ABaseWeapon> PrimaryClass, TSubclassOf<ABaseWeapon> SecondaryClass, TSubclassOf<ABaseWeapon> MeleeClass)
{
	// 【v60.6 大厂重构 — 槽位独立性 + 智能默认】
	ABaseCharacter* Owner = GetOwnerCharacterChecked(this);
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] Server_SpawnAllWeapons: Owner 无效 — 拒绝 Spawn."));
		return;
	}

	// 零兜底: 服务器权威
	if (!Owner->HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] Server_SpawnAllWeapons: 调用方非服务器 — 拒绝. Pawn=%s"),
			*Owner->GetName());
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] Server_SpawnAllWeapons: 开始 3 槽位 Spawn — Pawn=%s Primary=%s Secondary=%s Melee=%s"),
		*Owner->GetName(),
		*GetNameSafe(PrimaryClass),
		*GetNameSafe(SecondaryClass),
		*GetNameSafe(MeleeClass));

	// 初始化数组 (大厂原则: 显式 > 隐式)
	//   3 个槽位固定 size: [0]=Primary, [1]=Secondary, [2]=Melee
	WeaponsInSlot.Reset();
	WeaponsInSlot.SetNum(3);

	// ============================================================
	// 【v60.6 大厂原则 — 槽位独立性】每个槽位独立 Spawn, 失败不影响其他
	// ============================================================

	// Primary (主武器槽, 可空 — 允许玩家大厅不选主武器)
	if (PrimaryClass)
	{
		ABaseWeapon* PrimaryWeapon = SpawnAndConfigureWeaponInSlot(PrimaryClass, EWeaponSlotType::Primary);
		if (PrimaryWeapon)
		{
			WeaponsInSlot[SlotTypeToArrayIndex(EWeaponSlotType::Primary)] = PrimaryWeapon;
		}
		// SpawnAndConfigureWeaponInSlot 失败 → 内部已 Log Error → 不打断其他槽位
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[WeaponAttachment] Server_SpawnAllWeapons: PrimaryClass 为空 — 跳过 Primary 槽位. Pawn=%s"),
			*Owner->GetName());
	}

	// Secondary (副武器槽, 可空)
	if (SecondaryClass)
	{
		ABaseWeapon* SecondaryWeapon = SpawnAndConfigureWeaponInSlot(SecondaryClass, EWeaponSlotType::Secondary);
		if (SecondaryWeapon)
		{
			WeaponsInSlot[SlotTypeToArrayIndex(EWeaponSlotType::Secondary)] = SecondaryWeapon;
		}
	}

	// Melee (近战槽, 可空)
	if (MeleeClass)
	{
		ABaseWeapon* MeleeWeapon = SpawnAndConfigureWeaponInSlot(MeleeClass, EWeaponSlotType::Melee);
		if (MeleeWeapon)
		{
			WeaponsInSlot[SlotTypeToArrayIndex(EWeaponSlotType::Melee)] = MeleeWeapon;
		}
	}

	// ============================================================
	// 【v60.6 智能默认当前武器】
	//   - Primary 成功 → 选 Primary (玩家期望的开局武器)
	//   - Primary 失败但 Melee 成功 → 选 Melee (玩家至少能用刀)
	//   - 全部失败 → 显式 Log Error + CurrentWeapon 保持 null (玩家没武器)
	// ============================================================
	ABaseWeapon* SelectedDefault = nullptr;
	EWeaponSlotType SelectedSlot = EWeaponSlotType::None;

	if (ABaseWeapon* PW = WeaponsInSlot[SlotTypeToArrayIndex(EWeaponSlotType::Primary)])
	{
		SelectedDefault = PW;
		SelectedSlot = EWeaponSlotType::Primary;
	}
	else if (ABaseWeapon* MW = WeaponsInSlot[SlotTypeToArrayIndex(EWeaponSlotType::Melee)])
	{
		// 【v60.6】Primary 不可用, 退到 Melee (玩家至少能用刀)
		UE_LOG(LogTemp, Warning,
			TEXT("[WeaponAttachment] Server_SpawnAllWeapons: Primary 不可用, 退到 Melee 作为当前武器. Pawn=%s"),
			*Owner->GetName());
		SelectedDefault = MW;
		SelectedSlot = EWeaponSlotType::Melee;
	}
	else if (ABaseWeapon* SW = WeaponsInSlot[SlotTypeToArrayIndex(EWeaponSlotType::Secondary)])
	{
		// Secondary 排第二 (备用)
		SelectedDefault = SW;
		SelectedSlot = EWeaponSlotType::Secondary;
	}

	if (!SelectedDefault)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] Server_SpawnAllWeapons: 全部槽位 Spawn 失败 — 玩家没武器! Pawn=%s. "
			     "【v60.6 零兜底】修复: 检查上面 Primary/Secondary/Melee 各自的 Spawn 失败日志, 按提示修复 BP."),
			*Owner->GetName());
	}
	else
	{
		// 【v78 大厂架构 — 单一复制链】写入 WeaponState（真理源）
		FWeaponState NewState;
		NewState.ActiveWeapon = SelectedDefault;
		NewState.ActiveSlot = SelectedSlot;
		WeaponState = NewState;

		// 向后兼容字段
		CurrentWeapon = SelectedDefault;
		CurrentWeaponSlot = SelectedSlot;
	}

	// ============================================================
	// 【v77 P0 修复】所有槽位武器碰撞必须始终为 NoCollision
	//
	// 旧 (v60.x) 反模式:
	//   - bShouldShow=true 时错误设 SetActorEnableCollision(true)
	//   - 武器有碰撞体 → 与玩家身体碰撞 → 武器把玩家推开 → "玩家被武器弹飞"
	//   - 日志: BP_Weapon_ShovelLarge_C_0 堵在角色前面 → 玩家被弹飞
	//
	// 大厂原则 - 零幽灵碰撞:
	//   - 武器是角色的延伸, 不能有物理碰撞 (只有关卡设计用的物理武器才有碰撞)
	//   - Server_SwitchToWeaponSlot 已修复 (v72), 这里漏了
	// ============================================================
	if (ABaseWeapon* W = WeaponsInSlot[SlotTypeToArrayIndex(EWeaponSlotType::Primary)])
	{
		W->SetActorHiddenInGame(CurrentWeaponSlot != EWeaponSlotType::Primary);
		W->SetActorEnableCollision(ECollisionEnabled::NoCollision);
	}
	if (ABaseWeapon* W = WeaponsInSlot[SlotTypeToArrayIndex(EWeaponSlotType::Secondary)])
	{
		W->SetActorHiddenInGame(CurrentWeaponSlot != EWeaponSlotType::Secondary);
		W->SetActorEnableCollision(ECollisionEnabled::NoCollision);
	}
	if (ABaseWeapon* W = WeaponsInSlot[SlotTypeToArrayIndex(EWeaponSlotType::Melee)])
	{
		W->SetActorHiddenInGame(CurrentWeaponSlot != EWeaponSlotType::Melee);
		W->SetActorEnableCollision(ECollisionEnabled::NoCollision);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] Server_SpawnAllWeapons: 3 槽位 Spawn 完成 — Pawn=%s 当前槽位=%s 武器=%s "
		     "(Primary=%s Secondary=%s Melee=%s)"),
		*Owner->GetName(),
		LexToString(CurrentWeaponSlot),
		CurrentWeapon ? *CurrentWeapon->GetName() : TEXT("<null>"),
		WeaponsInSlot[SlotTypeToArrayIndex(EWeaponSlotType::Primary)] ? TEXT("OK") : TEXT("FAIL"),
		WeaponsInSlot[SlotTypeToArrayIndex(EWeaponSlotType::Secondary)] ? TEXT("OK") : TEXT("FAIL/Empty"),
		WeaponsInSlot[SlotTypeToArrayIndex(EWeaponSlotType::Melee)] ? TEXT("OK") : TEXT("FAIL/Empty"));

	// 【v84 大厂架构修复】武器 Spawn 完成后必须广播弹夹信息
	// 根因: 弹夹数量在武器 Spawn 之前广播 (BroadcastWeaponAmmoInfo 早于 Server_SpawnAllWeapons)
	//        导致 HUD 显示的是"无武器默认值 1/1"而非实际弹药数量
	// 解决方案: Server_SpawnAllWeapons 完成后立即广播弹夹数量
	if (UCharacterIconComponent* IconComp = Owner->FindComponentByClass<UCharacterIconComponent>())
	{
		IconComp->BroadcastWeaponAmmoInfo(Owner);
	}
}


// -----------------------------------------------------------------------------
// 5. SpawnAndConfigureWeaponInSlot — 内部辅助: Spawn 一把武器 + 注入 Strategy
// -----------------------------------------------------------------------------
ABaseWeapon* UWeaponAttachmentComponent::SpawnAndConfigureWeaponInSlot(TSubclassOf<ABaseWeapon> WeaponClass, EWeaponSlotType Slot)
{
	ABaseCharacter* Owner = GetOwnerCharacterChecked(this);
	if (!Owner)
	{
		return nullptr;
	}

	// 零兜底: WeaponClass 必须非空
	if (!WeaponClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SpawnAndConfigureWeaponInSlot: WeaponClass 为空 — 拒绝 Spawn. Slot=%s. Pawn=%s"),
			LexToString(Slot), *Owner->GetName());
		return nullptr;
	}

	// 1. 查挂载配置
	FWeaponAttachmentConfig* AttachmentConfig = FindWeaponAttachmentConfig(Owner->GetClass(), WeaponClass);
	if (!AttachmentConfig)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SpawnAndConfigureWeaponInSlot: 挂载配置缺失 — PawnClass=%s, WeaponClass=%s, Slot=%s. "
			     "【v32 零兜底】强制修复 WeaponAttachmentDataTable."),
			*Owner->GetClass()->GetName(), *WeaponClass->GetName(), LexToString(Slot));
		return nullptr;
	}

	// 2. SpawnActor 生成武器
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.Instigator = Owner;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ABaseWeapon* NewWeapon = Owner->GetWorld()->SpawnActor<ABaseWeapon>(
		WeaponClass,
		Owner->GetActorLocation(),
		Owner->GetActorRotation(),
		SpawnParams
	);

	if (!NewWeapon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SpawnAndConfigureWeaponInSlot: SpawnActor 失败 — WeaponClass=%s Slot=%s"),
			*WeaponClass->GetName(), LexToString(Slot));
		return nullptr;
	}

	// ============================================================
	// 【v61.2 核心修复】火控配置必须先于 MeshType 校验执行
	//   - DT_WeaponInfo.MeshType 只有通过 InitializeWeaponFireConfigFromClass 才会写入 Weapon->MeshType
	//   - 如果先校验 MeshType (此时 Weapon->MeshType = Melee 默认值)，枪械被误判为近战
	//   - 执行顺序: Spawn → InitializeWeaponFireConfigFromClass (写入 MeshType) → 校验 MeshType → 校验 Mesh → 校验 Socket → Attach
	// ============================================================
	InitializeWeaponFireConfigFromClass(NewWeapon, WeaponClass);

	// ============================================================
	// 【v60.8 大厂架构 — 协议验证提前】SpawnActor 之后立即校验
	//   旧 (v60.4) 反模式: Spawn 完 → Attach → 配 Strategy → 打"成功"日志 → 再校验 Mesh/Socket
	//     → 用户看到"成功"就以为武器 Spawn 成功了, 实际后面校验失败已经 Destroy
	//     → 浪费 GC 资源 (Spawn 完再 Destroy)
	//   新 (v60.8) 大厂原则 — 错误尽早暴露:
	//     1. 先校验 MeshType (BP 配置)
	//     2. 再校验 Mesh 组件 (FindComponentByClass)
	//     3. 再校验 Socket (Muzzle / TraceStart+End) — 协议要求
	//   失败立即 Destroy + return nullptr, 不浪费后续配置
	// ============================================================

	// (a) MeshType 校验 (零兜底 - BP 配置错误必须立刻暴露)
	const EWeaponMeshType ConfigMeshTypeEarly = NewWeapon->GetMeshType();
	if (ConfigMeshTypeEarly == EWeaponMeshType::None)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SpawnAndConfigureWeaponInSlot: MeshType=None — 拒绝 Spawn. "
			     "Weapon=%s WeaponClass=%s Slot=%s. 【v60.8 零兜底】在 BP_%s 的 Class Defaults → MeshType 字段必须设为 Primary/Secondary/Melee."),
			*NewWeapon->GetName(), *WeaponClass->GetName(), LexToString(Slot),
			*WeaponClass->GetName());
		NewWeapon->Destroy();
		return nullptr;
	}

	// (b) Mesh 组件校验 (零兜底 - 必须有 SkeletalMesh/StaticMesh)
	UMeshComponent* WeaponMeshEarly = NewWeapon->GetMeshComponent();
	if (!WeaponMeshEarly)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SpawnAndConfigureWeaponInSlot: 武器 Mesh 组件未配置 — 拒绝 Spawn. "
			     "Weapon=%s WeaponClass=%s MeshType=%d Slot=%s Pawn=%s. "
			     "【v60.8 零兜底】修复: "
			     "1) 在 BP_%s 的 Components 面板添加 SkeletalMeshComponent (Primary/Secondary 武器) 或 StaticMeshComponent (Melee 武器); "
			     "2) 在 SkeletalMeshComponent/StaticMeshComponent 的 Details → Skeletal Mesh/Static Mesh 字段设置武器模型资产."),
			*NewWeapon->GetName(),
			*WeaponClass->GetName(),
			static_cast<int32>(ConfigMeshTypeEarly),
			LexToString(Slot),
			*Owner->GetName(),
			*WeaponClass->GetName());
		NewWeapon->Destroy();
		return nullptr;
	}

	// (c) Socket 协议校验 (零兜底 - 必须有协议要求的 Socket)
	if (ConfigMeshTypeEarly == EWeaponMeshType::Primary || ConfigMeshTypeEarly == EWeaponMeshType::Secondary)
	{
		if (!WeaponMeshEarly->DoesSocketExist(FName("Muzzle")))
		{
			// 【v60.8 增强诊断】列出武器 Mesh 上所有现有 Socket 名, 帮助用户诊断拼写错误
			// UE 5.6: USceneComponent::GetAllSocketNames() 是无参版本, 返回 TArray<FName>
			const TArray<FName> AllSocketNames = WeaponMeshEarly->GetAllSocketNames();
			FString SocketList;
			for (const FName& Name : AllSocketNames)
			{
				SocketList += Name.ToString() + TEXT(", ");
			}
			if (SocketList.IsEmpty())
			{
				SocketList = TEXT("(无 — Mesh 上没有任何 Socket)");
			}

			UE_LOG(LogTemp, Error,
				TEXT("[WeaponAttachment] SpawnAndConfigureWeaponInSlot: 枪械武器无 Muzzle Socket — 拒绝 Spawn. "
				     "Weapon=%s WeaponClass=%s Slot=%s Mesh=%s. "
				     "【v60.8 零兜底】原因排查: "
				     "1) 检查 Socket 加在了**正确的资产**: Content Browser 双击 SkeletalMesh 资产 (不是 BP), "
				     "在 Window → Skeleton Tree → 右键 root → Add Socket, 命名**严格**为 'Muzzle' (大小写敏感); "
				     "2) **检查 BP 的 SkeletalMeshComponent 子组件的 Skeletal Mesh 字段引用的是这个资产** (不是别的); "
				     "3) 保存资产 (Ctrl+S) 后必须**重新编译 BP** (BP 缓存了旧版 Mesh 引用). "
				     "当前 Mesh '%s' 上的所有 Socket: [%s]"),
				*NewWeapon->GetName(), *WeaponClass->GetName(), LexToString(Slot),
				*WeaponMeshEarly->GetName(),
				*WeaponMeshEarly->GetName(),
				*SocketList);
			NewWeapon->Destroy();
			return nullptr;
		}
	}
	else if (ConfigMeshTypeEarly == EWeaponMeshType::Melee)
	{
		const bool bHasStart = WeaponMeshEarly->DoesSocketExist(FName("TraceStart"));
		const bool bHasEnd = WeaponMeshEarly->DoesSocketExist(FName("TraceEnd"));
		if (!bHasStart || !bHasEnd)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[WeaponAttachment] SpawnAndConfigureWeaponInSlot: 近战武器无 TraceStart/TraceEnd Socket — 拒绝 Spawn. "
				     "Weapon=%s WeaponClass=%s Slot=%s (HasTraceStart=%d HasTraceEnd=%d). "
				     "【v60.8 零兜底】修复: 在武器 Static/Skeletal Mesh 资产上加 'TraceStart' + 'TraceEnd' 两个 Socket, 位置对齐刀刃两端."),
				*NewWeapon->GetName(), *WeaponClass->GetName(), LexToString(Slot),
				bHasStart ? 1 : 0, bHasEnd ? 1 : 0);
			NewWeapon->Destroy();
			return nullptr;
		}
	}

	// 3. 挂载到 Socket
	// 【v58 大厂架构 — 零兜底】先校验 Socket 在角色 Mesh 上是否存在
	//   UE AttachToComponent 静默失败: socket 不存在时 attach 到 component root, 武器"消失"
	//   必须显式校验 + Log Error, 让用户知道 DT 配置错或角色 Mesh 没这个 Socket
	USkeletalMeshComponent* OwnerMesh = Owner->GetMesh();
	if (!OwnerMesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SpawnAndConfigureWeaponInSlot: 角色 Mesh 为空 — 无法 Attach. "
			     "Weapon=%s Slot=%s Pawn=%s. 【v58 零兜底】检查 BP_BaseCharacter 的 Components → Mesh."),
			*NewWeapon->GetName(), LexToString(Slot), *Owner->GetName());
		NewWeapon->Destroy();
		return nullptr;
	}

	const FName SocketName = AttachmentConfig->SocketName;
	const bool bSocketExists = !SocketName.IsNone() && OwnerMesh->DoesSocketExist(SocketName);
	if (!bSocketExists)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SpawnAndConfigureWeaponInSlot: Socket '%s' 在角色 Mesh 上不存在 — Attach 静默失败. "
			     "Weapon=%s PawnClass=%s Slot=%s. "
			     "【v58 大厂原则 — 零兜底】修复方法: "
			     "1) 在 BP_%s 的 Mesh 组件 (SkeletalMesh) 上添加 Socket '%s'; "
			     "2) 或在 DT_WeaponAttachmentConfig 中修改 SocketName 为角色 Mesh 上实际存在的 Socket."),
			*SocketName.ToString(),
			*NewWeapon->GetName(),
			*Owner->GetClass()->GetName(),
			LexToString(Slot),
			*Owner->GetClass()->GetName(),
			*SocketName.ToString());
		NewWeapon->Destroy();
		return nullptr;
	}

	NewWeapon->AttachToComponent(
		OwnerMesh,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		SocketName
	);

	// 【v72 大厂架构 P0 修复】武器 Spawn 后立即关闭碰撞 (与 SpawnAndEquipWeapon 路径对称)
	//   根因: 武器 BP 默认 CollisionProfile=BlockAll, Attach 到角色手部后与角色胶囊体重叠
	//   → 物理引擎把角色往反方向推 → "按什么都往后"
	if (UMeshComponent* WeaponMesh = NewWeapon->GetMeshComponent())
	{
		WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// 【v58 大厂架构诊断日志】验证 Attach 是否真的成功
	//   UE AttachToComponent 即便 Socket 不存在也返回 true (静默成功, 但实际 attach 到 root)
	//   必须 GetAttachSocketName 二次校验
	//   GetAttachSocketName 是 USceneComponent 方法, 武器 Actor 的根组件就是武器 mesh
	const FName ActualAttachedSocket = NewWeapon->GetRootComponent()
		? NewWeapon->GetRootComponent()->GetAttachSocketName()
		: NAME_None;
	if (ActualAttachedSocket != SocketName)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SpawnAndConfigureWeaponInSlot: Attach 后校验失败! "
			     "请求 Socket='%s' 但实际 attach 到 '%s'. "
			     "Weapon=%s Pawn=%s. 【v58 零兜底】角色 Mesh 上这个 Socket 名无效, 强制修复 Mesh 或 DT."),
			*SocketName.ToString(),
			*ActualAttachedSocket.ToString(),
			*NewWeapon->GetName(),
			*Owner->GetName());
		NewWeapon->Destroy();
		return nullptr;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] SpawnAndConfigureWeaponInSlot: Attach 成功 — Weapon=%s Socket='%s' Pawn=%s"),
		*NewWeapon->GetName(),
		*ActualAttachedSocket.ToString(),
		*Owner->GetName());

	// 4. 应用相对偏移【v57 大厂架构修复 — 零兜底】
	//   旧 (v56): IsNearlyZero 兜底检查 → 用户配置的 (0,0,0) 偏移被忽略
	//   新 (v57): 始终应用配置值 → 用户配置 (0,0,0) 视为显式意图
	//   大厂原则: IsNearlyZero 是兜底反模式, 强制修复 DT 配置比静默跳过更好
	NewWeapon->SetActorRelativeLocation(AttachmentConfig->RelativeLocation);
	NewWeapon->SetActorRelativeRotation(AttachmentConfig->RelativeRotation);
	NewWeapon->SetActorRelativeScale3D(AttachmentConfig->RelativeScale);

	// 【v57 大厂架构诊断日志】显示实际应用的偏移量
	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] 应用相对偏移: Socket=%s, Loc=%s, Rot=%s, Scale=%s"),
		*AttachmentConfig->SocketName.ToString(),
		*AttachmentConfig->RelativeLocation.ToString(),
		*AttachmentConfig->RelativeRotation.ToString(),
		*AttachmentConfig->RelativeScale.ToString());

	// ============================================================
	// 【v56 大厂架构核心】注入 Strategy
	// ============================================================

	// 按 MeshType 决定 Strategy 类型
	// 【v56 设计】MeshType 直接决定 Strategy，不在需要 WeaponType
	//   - Melee → UMeleeSwStrategy (BoxTrace)
	//   - Primary/Secondary → URangedLineStrategy (LineTrace)
	TScriptInterface<IWeaponDamageStrategy> Strategy;

	// 先检查 MeshType（BP 配置的枚举值）
	EWeaponMeshType ConfigMeshType = NewWeapon->GetMeshType();

	switch (ConfigMeshType)
	{
	case EWeaponMeshType::Melee:
		Strategy.SetObject(NewObject<UMeleeSwStrategy>(NewWeapon));
		Strategy.SetInterface(Cast<IWeaponDamageStrategy>(Strategy.GetObject()));
		break;

	case EWeaponMeshType::Primary:
	case EWeaponMeshType::Secondary:
		Strategy.SetObject(NewObject<URangedLineStrategy>(NewWeapon));
		Strategy.SetInterface(Cast<IWeaponDamageStrategy>(Strategy.GetObject()));
		break;

	case EWeaponMeshType::None:
	default:
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SpawnAndConfigureWeaponInSlot: MeshType=None — 无法装配 Strategy. "
			     "Weapon=%s Slot=%s. 【v56 零兜底】必须在 BP 蓝图 Class Defaults 中配置 MeshType 字段."),
			*NewWeapon->GetName(), LexToString(Slot));
		break;
	}

	if (Strategy && Strategy.GetInterface())
	{
		NewWeapon->SetDamageStrategy(Strategy);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] SpawnAndConfigureWeaponInSlot: 成功 — Weapon=%s MeshType=%d Strategy=%s Slot=%s"),
		*NewWeapon->GetName(),
		static_cast<int32>(ConfigMeshType),
		Strategy && Strategy.GetObject() ? *Strategy.GetObject()->GetClass()->GetName() : TEXT("<none>"),
		LexToString(Slot));

	// 【v81 大厂架构 — 客户端武器姿态修复】写入挂载运行时数据
	//   服务器 SpawnAndConfigureWeaponInSlot 是"槽位专用" Spawn 入口 (v52+)
	//   仅当 Spawn 的是当前激活槽位时, 写入 ActiveSlotAttachment
	//   (否则 — 比如生成副武器时 ActiveSlot 还是 Primary — 会被 Primary 的 OnRep 错误覆盖)
	if (Slot == CurrentWeaponSlot)
	{
		ActiveSlotAttachment.SocketName = AttachmentConfig->SocketName;
		ActiveSlotAttachment.RelativeLocation = AttachmentConfig->RelativeLocation;
		ActiveSlotAttachment.RelativeRotation = AttachmentConfig->RelativeRotation;
		ActiveSlotAttachment.RelativeScale3D = AttachmentConfig->RelativeScale;
		ActiveSlotAttachment.bIsValid = true;

		UE_LOG(LogTemp, Log,
			TEXT("[WeaponAttachment] SpawnAndConfigureWeaponInSlot: 写入 ActiveSlotAttachment (当前激活槽位=%s) — Socket=%s"),
			LexToString(Slot),
			*AttachmentConfig->SocketName.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[WeaponAttachment] SpawnAndConfigureWeaponInSlot: Slot=%s 不是当前激活槽位 (=%s), 不写 ActiveSlotAttachment."),
			LexToString(Slot), LexToString(CurrentWeaponSlot));
	}

	return NewWeapon;
}


// ==========================================
// v60 大厂架构 — 武器火控配置初始化
// ==========================================
//
// 【设计 — 单一真理源迁移】
//   - 弹药/射速/换弹/开火模式/蒙太奇 全部从 DT_WeaponInfo 读
//   - 调用方: SpawnAndEquipWeapon / SpawnAndConfigureWeaponInSlot (服务器 Spawn 后)
//   - 由 UWeaponFireComponent::InitializeFromWeaponConfig 写入字段
//
// 【DT 行反查 — WeaponClass → FWeaponInfo】
//   路径: GM->WeaponDataTable → 遍历 RowName 找到 WeaponBlueprint == WeaponClass 的行
//   时间复杂度 O(N), 但 DT 行数 < 100, 完全可接受
//   (避免给每个武器额外加 "RowName" 字段, 用 Class 反查是 UE 主流做法)

/**
 * InitializeWeaponFireConfigFromClass — 武器生成后初始化火控组件
 *
 * 调用方: SpawnAndEquipWeapon / SpawnAndConfigureWeaponInSlot (服务器)
 * 流程:
 *   1. WeaponFireComponent 字段校验 (nullptr → 跳过, 但 Log Warning 提示)
 *   2. 按 WeaponClass 查 DT_WeaponInfo
 *   3. 写入 Weapon->WeaponRowName (供 Server_ReportHit 后续读伤害用)
 *   4. 调 WeaponFireComponent->InitializeFromWeaponConfig
 *
 * 零兜底:
 *   - WeaponFireComponent 未挂载 → Log Warning 跳过 (近战武器也挂载, 字段一定存在)
 *   - DT 找不到该武器行 → Log Error + 跳过 (强制修复 DT)
 *   - GM->WeaponDataTable 为空 → Log Error + 跳过
 */
void UWeaponAttachmentComponent::InitializeWeaponFireConfigFromClass(ABaseWeapon* NewWeapon, TSubclassOf<ABaseWeapon> WeaponClass)
{
	if (!NewWeapon || !WeaponClass)
	{
		return;
	}

	// 1. 校验 WeaponFireComponent 字段 (C++ 构造函数已创建, 这里防御性检查)
	if (!NewWeapon->WeaponFireComponent)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[WeaponAttachment] InitializeWeaponFireConfigFromClass: Weapon=%s WeaponFireComponent 为空. 修复: 检查 BP_Weapon_Xxx 是否删了 UWeaponFireComponent 子对象."),
			*NewWeapon->GetName());
		return;
	}

	// 2. 找 GameMode + DT
	UWorld* World = NewWeapon->GetWorld();
	if (!World)
	{
		return;
	}

	ARoomGameMode* GM = World->GetAuthGameMode<ARoomGameMode>();
	if (!GM || !GM->WeaponDataTable)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] InitializeWeaponFireConfigFromClass: ARoomGameMode 或 WeaponDataTable 未配 — 武器火控配置无法初始化. Weapon=%s. 修复: BP_GM_RoomGameMode Class Defaults 必须配 WeaponDataTable 字段."),
			*NewWeapon->GetName());
		return;
	}

	// 3. 遍历 DT 找匹配行 (WeaponBlueprint == WeaponClass)
	//
	// 大厂原则 (v60.2 修复 — UE 标准 API):
	//   - 旧版 (v60.0-v60.1) 反模式: GetAllRows() + 指针减法算索引
	//     → 跨版本 const 转换/类型转换产生垃圾指针 (-613688988) → Assertion 崩溃
	//   - 新版: 直接迭代 RowMap, Pair.Key 就是 RowName (UE 标准做法, 无指针算术)
	//   - 时间复杂度 O(N), DT_WeaponInfo 通常 < 100 行, 完全可接受
	//
	// UE 5.6 API: UDataTable::GetRowMap() 返回 TMap<FName, uint8*>
	//   - 内部存储按 RowName 哈希, 与 RowName 字段值一致
	//   - uint8* 指向原始 row bytes, reinterpret_cast 到 FWeaponInfo* 读取字段
	const FWeaponInfo* FoundWeaponInfo = nullptr;
	FName FoundRowName = NAME_None;

	if (!GM->WeaponDataTable)
	{
		// 防御性: 上面已校验 GM, 这里再保一次
		return;
	}

	const FString ContextString = TEXT("WeaponAttachment::InitializeWeaponFireConfigFromClass");
	const TMap<FName, uint8*>& RowMap = GM->WeaponDataTable->GetRowMap();

	for (const TPair<FName, uint8*>& Pair : RowMap)
	{
		if (!Pair.Value)
		{
			continue;
		}

		const FWeaponInfo* Row = reinterpret_cast<const FWeaponInfo*>(Pair.Value);
		if (Row && Row->WeaponBlueprint.LoadSynchronous() == WeaponClass)
		{
			FoundWeaponInfo = Row;
			FoundRowName = Pair.Key; // 直接拿 UE 标准 Map key, 无指针算术
			break;
		}
	}

	(void)ContextString; // 保留以备未来 GetAllRows 路径用

	if (!FoundWeaponInfo)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] InitializeWeaponFireConfigFromClass: DT_WeaponInfo 找不到 WeaponClass=%s 的行 — 武器火控配置无法初始化. Weapon=%s. 修复: 在 DT_WeaponInfo 中添加一行 WeaponBlueprint=%s."),
			*WeaponClass->GetName(), *NewWeapon->GetName(), *WeaponClass->GetName());
		return;
	}

	// 4. 写入 WeaponRowName (供 Server_ReportHit 读伤害用)
	NewWeapon->WeaponRowName = FoundRowName;

	// 5. 【v61.2 大厂重构 P0】从 DT_WeaponInfo 读取 MeshType 写入 Weapon
	//    - MeshType 是武器的核心配置, 必须从 DT_WeaponInfo 单一真理源派生
	//    - BP 蓝图层的 MeshType 字段被废弃 (不再读取)
	//    - 零兜底: DT_WeaponInfo.MeshType == None → 仍然写入, 让 SpawnAndConfigureWeaponInSlot 中的校验拒绝
	NewWeapon->MeshType = FoundWeaponInfo->MeshType;

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] InitializeWeaponFireConfigFromClass: DT_WeaponInfo '%s' MeshType=%d — 写入 Weapon->MeshType — 武器=%s"),
		*FoundRowName.ToString(),
		static_cast<int32>(FoundWeaponInfo->MeshType),
		*NewWeapon->GetName());

	if (FoundWeaponInfo->MeshType == EWeaponMeshType::None)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] InitializeWeaponFireConfigFromClass: DT_WeaponInfo '%s' 的 MeshType 为 None — 武器=%s. ")
			TEXT("【v61.2 零兜底】SpawnAndConfigureWeaponInSlot 会拒绝此武器. 修复: 打开 DT_WeaponInfo, 找到 '%s' 行, 在 Combat 分类下将 MeshType 设为 Primary / Secondary / Melee (不能是 None)."),
			*FoundRowName.ToString(),
			*NewWeapon->GetName(),
			*FoundRowName.ToString());
	}

	// 6. 初始化 FireComponent (服务器权威)
	NewWeapon->WeaponFireComponent->InitializeFromWeaponConfig(*FoundWeaponInfo, FoundRowName);

	// 6. 【v60.14 新增】写入武器射程 (策划配置, 非硬编码)
	//    - 射线终点 = StartLoc + Direction * Weapon->AttackRange
	//    - 零兜底:
	//      (a) AttackRange <= 0 → Log Error + 拒绝写入 (否则射线终点 = StartLoc, 永远打不到东西)
	//      (b) AttackRange > 20000cm (200米) → Log Error 提示 (设计异常, AK47 不可能 600 米)
	//          仍写入 (允许运行, 但开发者必须改 DT — 上一版本没这校验, 用户 AK47 配 60000cm 直接打 40km 远)
	if (FoundWeaponInfo->AttackRange <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] InitializeWeaponFireConfigFromClass 失败: WeaponRow=%s AttackRange<=0 (=%f). 修复: DT_WeaponInfo 中 AttackRange 必须 >= 100cm"),
			*FoundRowName.ToString(), FoundWeaponInfo->AttackRange);
	}
	else
	{
		NewWeapon->AttackRange = FoundWeaponInfo->AttackRange;

		// 【v82+ 防御】AttackRange 上限校验 — 设计异常, 必须告知开发者
		//   真根因: 之前没这校验, BQ001.AK47 在 DT 里配成 60000cm → 射线 End 在 40km 外
		//   → 打到地图底面 Landscape_0 (Y=-40180 距离地图原点 40km) → 用户看到"射线乱飞"
		constexpr float kMaxReasonableAttackRangeCm = 20000.0f; // 200 米, 大厂武器射程上限
		if (FoundWeaponInfo->AttackRange > kMaxReasonableAttackRangeCm)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[WeaponAttachment] InitializeWeaponFireConfigFromClass: WeaponRow=%s AttackRange=%.0fcm 超过 200 米上限 (=%fcm). "
				     "【v82+ 零兜底】强烈建议修改 DT_WeaponInfo 中该行的 AttackRange (典型值: AK47=3000cm, 狙击枪=15000cm). "
				     "代码已写入该值, 但射线会打到地图边缘, 表现是'射线乱飞/超长'."),
				*FoundRowName.ToString(), FoundWeaponInfo->AttackRange, kMaxReasonableAttackRangeCm);
		}
	}

	// 7. 【v60.16 新增】写入枪口偏移 (策划配置, 非硬编码)
	//    - 射线起点 = 相机位置 - 相机方向 × (TargetArmLength + MuzzleOffset)
	//    - 零兜底: MuzzleOffset < 0 → Log Error 拒绝初始化
	if (FoundWeaponInfo->MuzzleOffset < 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] InitializeWeaponFireConfigFromClass 失败: WeaponRow=%s MuzzleOffset<0 (=%f). 修复: DT_WeaponInfo 中 MuzzleOffset 必须 >= 0cm"),
			*FoundRowName.ToString(), FoundWeaponInfo->MuzzleOffset);
	}
	else
	{
		NewWeapon->MuzzleOffset = FoundWeaponInfo->MuzzleOffset;
		UE_LOG(LogTemp, Log,
			TEXT("[WeaponAttachment] InitializeWeaponFireConfigFromClass: WeaponRow=%s AttackRange=%.0fcm MuzzleOffset=%.0fcm (策划配置)"),
			*FoundRowName.ToString(), NewWeapon->AttackRange, NewWeapon->MuzzleOffset);
	}
}
