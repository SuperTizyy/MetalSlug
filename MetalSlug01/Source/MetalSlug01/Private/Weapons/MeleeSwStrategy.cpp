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


UMeleeSwStrategy::UMeleeSwStrategy()
{
	// 默认状态
	bIsActive = false;
	bIsCurrentHeavy = false;
}


// ==========================================
// 1. StartTrace — 启动检测
// ==========================================

bool UMeleeSwStrategy::StartTrace(ABaseWeapon* Weapon, bool bIsHeavy)
{
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

	// 记录激活态
	bIsActive = true;
	bIsCurrentHeavy = bIsHeavy;
	ActiveWeapon = Weapon;
	IgnoreActors.Empty();

	// 自己 + 持有武器的人加入黑名单 (防砍自己)
	IgnoreActors.Add(Weapon);
	if (AActor* Owner = Weapon->GetOwner())
	{
		IgnoreActors.Add(Owner);
	}

	// 初始化起始位置 (防 0 点开刀)
	LastFrameStartLoc = Mesh->GetSocketLocation(FName("TraceStart"));
	LastFrameEndLoc = Mesh->GetSocketLocation(FName("TraceEnd"));

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
	if (!bIsActive)
	{
		// 幂等: 已经停止, no-op
		return;
	}

	bIsActive = false;
	IgnoreActors.Empty();
	ActiveWeapon.Reset();
	bIsCurrentHeavy = false;

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
	if (!bIsActive)
	{
		return;
	}

	if (!Weapon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UMeleeSwStrategy::TickDetection] Weapon 已失效 — 强制停止检测."));
		bIsActive = false;
		return;
	}

	UMeshComponent* Mesh = Weapon->GetMeshComponent();
	if (!Mesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UMeleeSwStrategy::TickDetection] Weapon=%s 没有 Mesh 组件 — 强制停止检测."),
			*Weapon->GetName());
		bIsActive = false;
		return;
	}

	// 读取当前帧的刀刃世界坐标
	const FVector StartLoc = Mesh->GetSocketLocation(FName("TraceStart"));
	const FVector EndLoc = Mesh->GetSocketLocation(FName("TraceEnd"));

	// 零兜底: 两点重合或 Socket 不存在 → 跳过 (防 NaN/零矩阵)
	if ((EndLoc - StartLoc).IsNearlyZero())
	{
		// 不报错 (允许开始帧 LastFrame 全零), 但跳过本帧检测
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
	const FRotator BoxRotation = Mesh->GetSocketRotation(FName("TraceStart"));

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
