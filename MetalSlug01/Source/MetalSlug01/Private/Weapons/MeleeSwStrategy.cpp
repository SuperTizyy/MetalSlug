// ==========================================
// UMeleeSwStrategy 实现 (大厂架构 v51)
//
// 【完整保留 v1-v50 BoxTrace 缝合算法】
//   - LastFrame 跨帧缓存
//   - IgnoreActors 一刀多伤防护
//   - BoxHalfSize 根据刀身长度动态计算
//   - 命中调 Server_ReportHit (RPC, 自动路由到服务器)
//
// 【零兜底】
//   - Mesh 失效 → 强制停 trace + Log Error
//   - Socket 不存在 → Log Error (配置错, 让美术修)
//   - StartTrace/EndLoc IsNearlyZero → 跳过本帧 (防 NaN)
// ==========================================

#include "Weapons/MeleeSwStrategy.h"

#include "Weapons/BaseWeapon.h"
#include "Components/MeshComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"

// 【编译修复 — P1】IsAttackerAI 是 ABaseCharacter 成员方法 (v40.6 注入),
//   TScriptInterface/IWeaponDamageStrategy 通过 ABaseWeapon->GetOwner() 拿 Owner 时
//   只看到 ABaseWeapon, 不能看见 ABaseCharacter. 这里必须显式 include 完整定义
//   (前向声明仅对引用/指针有效, 对 Cast<T>() 函数调用无效).
#include "Characters/BaseCharacter.h"


// 【v75 单一真理源】Socket 名称常量定义 — 与 .h 声明一一对应
const FName UMeleeSwStrategy::SocketName_TraceStart = FName(TEXT("TraceStart"));
const FName UMeleeSwStrategy::SocketName_TraceEnd   = FName(TEXT("TraceEnd"));


UMeleeSwStrategy::UMeleeSwStrategy()
{
	// 默认状态
	TraceState = EWeaponTraceState::Idle;
	bIsCurrentHeavy = false;
}


// ==========================================
// 1. StartTrace — 启动检测
// ==========================================

bool UMeleeSwStrategy::StartTrace(ABaseWeapon* Weapon, bool bIsHeavy,
	const FVector& /*ClientRayOrigin*/,
	const FVector& /*ClientRayDirection*/)
{
	// v82 客户端射线参数 — Melee 不使用, 走 Socket 路径
	if (!Weapon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UMeleeSwStrategy::StartTrace] Weapon 为空 — 拒绝启动检测."));
		return false;
	}

	UMeshComponent* Mesh = Weapon->GetMeshComponent();
	if (!Mesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UMeleeSwStrategy::StartTrace] Weapon=%s 没有 Mesh 组件 — 拒绝启动检测. ")
			TEXT("【v61.3 零兜底】修复: 在 BP 蓝图 Components 面板添加 StaticMeshComponent (近战武器) 或 SkeletalMeshComponent (枪械)."),
			*Weapon->GetName());
		return false;
	}

	// 【v75 P0 修复】零兜底: 校验 Socket 真实存在 — 美术没配立即报错
	//   根因: 旧版 StartTrace 不校验 Socket, GetSocketLocation 返回组件原点 fallback
	//   → LastFrameStartLoc == LastFrameEndLoc (都是组件原点) → TickDetection 跳过
	//   → 用户看不到任何 trace, 也不知道为什么 (兜底反模式)
	//   真理源: SocketName_TraceStart / SocketName_TraceEnd (头文件静态成员常量)
	if (!Mesh->DoesSocketExist(SocketName_TraceStart))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UMeleeSwStrategy::StartTrace] Weapon=%s Mesh 没有 Socket 'TraceStart' — 拒绝启动检测. ")
			TEXT("【v75 零兜底】修复: 打开武器 Mesh 资产 (BP_Weapon_ShovelLarge.uasset → Components → StaticMeshComponent) ")
			TEXT("→ 在 Details 面板 'Sockets' 分组 → Add Socket → 命名 'TraceStart' → 拖到刀刃起点."),
			*Weapon->GetName());
		return false;
	}
	if (!Mesh->DoesSocketExist(SocketName_TraceEnd))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UMeleeSwStrategy::StartTrace] Weapon=%s Mesh 没有 Socket 'TraceEnd' — 拒绝启动检测. ")
			TEXT("【v75 零兜底】修复: 同上, 在武器 Mesh 上加 Socket 'TraceEnd', 拖到刀刃终点."),
			*Weapon->GetName());
		return false;
	}

	// 记录激活态 (v74 — 状态标签 + 广播)
	const EWeaponTraceState OldState = TraceState;
	TraceState = EWeaponTraceState::Tracing;
	bIsCurrentHeavy = bIsHeavy;
	ActiveWeapon = Weapon;
	IgnoreActors.Empty();

	// 自己 + 持有武器的人加入黑名单 (防砍自己)
	IgnoreActors.Add(Weapon);
	if (AActor* Owner = Weapon->GetOwner())
	{
		IgnoreActors.Add(Owner);
	}

	// 初始化起始位置 (用同一静态常量, 避免魔法字符串散落)
	LastFrameStartLoc = Mesh->GetSocketLocation(SocketName_TraceStart);
	LastFrameEndLoc = Mesh->GetSocketLocation(SocketName_TraceEnd);

	// v74 广播: Idle → Tracing
	if (OldState != TraceState)
	{
		TraceStateChanged.Broadcast(TraceState, nullptr, FVector::ZeroVector, NAME_None);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[UMeleeSwStrategy::StartTrace] 启动近战检测 — Weapon=%s Heavy=%d 起点=%s 终点=%s"),
		*Weapon->GetName(),
		bIsHeavy ? 1 : 0,
		*LastFrameStartLoc.ToCompactString(),
		*LastFrameEndLoc.ToCompactString());

	return true;
}


// ==========================================
// 2. StopTrace — 停止检测
// ==========================================

void UMeleeSwStrategy::StopTrace(ABaseWeapon* Weapon)
{
	if (TraceState == EWeaponTraceState::Idle)
	{
		// 幂等: 已经停止, no-op
		return;
	}

	const EWeaponTraceState OldState = TraceState;
	TraceState = EWeaponTraceState::Idle;
	IgnoreActors.Empty();
	ActiveWeapon.Reset();
	bIsCurrentHeavy = false;

	// v74 广播: Tracing → Idle
	if (OldState != TraceState)
	{
		TraceStateChanged.Broadcast(TraceState, nullptr, FVector::ZeroVector, NAME_None);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[UMeleeSwStrategy::StopTrace] 停止近战检测 — Weapon=%s"),
		Weapon ? *Weapon->GetName() : TEXT("<null>"));
}


// ==========================================
// 3. TickDetection — 核心 BoxTrace 缝合检测
// ==========================================

void UMeleeSwStrategy::TickDetection(ABaseWeapon* Weapon, float DeltaTime)
{
	// 未激活 → 跳过 (大厂原则 - 不浪费 CPU)
	if (TraceState == EWeaponTraceState::Idle)
	{
		return;
	}

	if (!Weapon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UMeleeSwStrategy::TickDetection] Weapon 已失效 — 强制停止检测."));
		TraceState = EWeaponTraceState::Idle;
		TraceStateChanged.Broadcast(TraceState, nullptr, FVector::ZeroVector, NAME_None);
		return;
	}

	UMeshComponent* Mesh = Weapon->GetMeshComponent();
	if (!Mesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UMeleeSwStrategy::TickDetection] Weapon=%s 没有 Mesh 组件 — 强制停止检测."),
			*Weapon->GetName());
		TraceState = EWeaponTraceState::Idle;
		TraceStateChanged.Broadcast(TraceState, nullptr, FVector::ZeroVector, NAME_None);
		return;
	}

	// 读取当前帧的刀刃世界坐标
	//   真理源: SocketName_TraceStart / SocketName_TraceEnd (头文件静态成员常量)
	const FVector StartLoc = Mesh->GetSocketLocation(SocketName_TraceStart);
	const FVector EndLoc = Mesh->GetSocketLocation(SocketName_TraceEnd);

	// 【v75 零兜底】Socket 位置不能为零向量 — StartTrace 已校验 Socket 存在, 但位置可能仍在原点 (美术配错)
	//   旧版 (v60-v74): IsNearlyZero 静默跳过 → 用户看不到 trace 也不知道为什么 (兜底反模式)
	//   新版 (v75): 起点终点任一为零向量 → Log Error + 强制 StopTrace + 关闭状态标签
	const bool bStartInvalid = StartLoc.IsNearlyZero();
	const bool bEndInvalid = EndLoc.IsNearlyZero();
	if (bStartInvalid || bEndInvalid)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UMeleeSwStrategy::TickDetection] Weapon=%s Socket 位置为零向量 (Start=%s, End=%s) — 强制停止检测. ")
			TEXT("【v75 零兜底】Socket 已存在但位置是原点, 说明美术在 Mesh 编辑器拖 Socket 时没真正放上去. ")
			TEXT("修复: 打开武器 Mesh 资产 → Sockets 面板 → 拖动 TraceStart/TraceEnd 到刀刃两端 → 保存."),
			*Weapon->GetName(),
			*StartLoc.ToCompactString(),
			*EndLoc.ToCompactString());

		TraceState = EWeaponTraceState::Idle;
		TraceStateChanged.Broadcast(TraceState, nullptr, FVector::ZeroVector, NAME_None);
		ActiveWeapon.Reset();
		return;
	}

	// ============================================================
	// 核心黑科技: 缝合上一帧与当前帧的真空区
	// ============================================================
	const FVector LastMid = (LastFrameStartLoc + LastFrameEndLoc) * 0.5f;
	const FVector CurrentMid = (StartLoc + EndLoc) * 0.5f;

	const float BladeLength = FVector::Distance(StartLoc, EndLoc);

	// Box 半尺寸 (15 cm 是刀气厚度, 高度是刀身长度一半)
	const FVector BoxHalfSize = FVector(15.0f, 15.0f, BladeLength * 0.5f);

	// 绝对不用 MakeFromZ 猜角度, 直接拿 Socket 最真实物理旋转
	const FRotator BoxRotation = Mesh->GetSocketRotation(SocketName_TraceStart);

	// 过滤 IgnoreActors 中的失效引用 (TWeakObjectPtr 可能在武器切换后失效)
	TArray<AActor*> ValidIgnoreActors;
	ValidIgnoreActors.Reserve(IgnoreActors.Num());
	for (const TWeakObjectPtr<AActor>& Weak : IgnoreActors)
	{
		if (AActor* A = Weak.Get())
		{
			ValidIgnoreActors.Add(A);
		}
	}

	// BoxTraceSingle 从上一帧中心滑向当前帧中心
	FHitResult HitResult;
	const bool bHit = UKismetSystemLibrary::BoxTraceSingle(
		Weapon,
		LastMid,
		CurrentMid,
		BoxHalfSize,
		BoxRotation,
		UEngineTypes::ConvertToTraceType(ECC_Visibility),
		false,
		ValidIgnoreActors,
		EDrawDebugTrace::ForDuration,
		HitResult,
		true,
		FLinearColor::Red,        // TraceColor: 未命中颜色
		FLinearColor::Green,      // TraceHitColor: 命中颜色
		IWeaponDamageStrategy::kDebugTraceLifeTimeSeconds  // 【v60.10】显示时长 5 分钟
	);

	if (bHit && HitResult.GetActor())
	{
		// 命中 → 加入黑名单 (这刀没收回前不再判定他)
		IgnoreActors.Add(HitResult.GetActor());

		// 玩家/AI 判定: 从 Weapon Owner 读 bIsCurrentlyAttackerAI (单一真理源)
		bool bAIDriven = false;
		if (const ABaseCharacter* OwnerChar = Cast<ABaseCharacter>(Weapon->GetOwner()))
		{
			bAIDriven = OwnerChar->IsAttackerAI();
		}

		// v74 广播: Tracing → Hit (Hit 是一帧瞬态, 下帧自动回 Tracing)
		// HUD/音效/命中反馈订阅 OnTraceStateChanged 即可拿到完整命中信息
		TraceStateChanged.Broadcast(
			EWeaponTraceState::Hit,
			HitResult.GetActor(),
			HitResult.ImpactPoint,
			HitResult.BoneName);

		// 调 Server RPC 报告命中 (RPC 自动路由到服务器)
		Weapon->Server_ReportHit(
			HitResult.GetActor(),
			0.0f, // 伤害值由服务器按部位 + MeshType 重新计算
			HitResult.ImpactPoint,
			HitResult.ImpactNormal,
			HitResult.BoneName,
			bIsCurrentHeavy,
			bAIDriven
		);
	}

	// 当前帧结算完毕 → 存入记忆, 变成下一帧的"上一帧"
	LastFrameStartLoc = StartLoc;
	LastFrameEndLoc = EndLoc;
}
