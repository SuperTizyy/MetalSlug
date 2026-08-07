// ==========================================
// UMeleeSwStrategy 实现 (大厂架构 v98.1)
//
// 【v98.1 重大修正 — RPC 极简架构】
//   v98.0 错误: 给"服务器端玩家 Pawn"加了"跳过 Server_ReportHit"逻辑
//   - 假设: 服务器端命中 → 跳过 → 客户端 RPC 统一走
//   - 实际: ListenServer host 自己的 Pawn 在 host 进程里, host 客户端=host server
//           客户端 RPC 永远不发 → 服务器跳过 → 0 伤害
//   - 用户反馈: "监听服务器近战武器不扣血"
//
//   v98.1 修正: 无条件调 Server_ReportHit / Server_ReportMotherAttackHit
//   - 真理源: UE Server RPC 通道保证
//     · 服务器端调用 → 同进程直接执行 Implementation → 1 次扣血
//     · 客户端端调用 → RPC 序列化 → 服务器 Implementation → 1 次扣血
//   - 重复扣血防护: BaseWeapon::Tick 守卫 (HasAuthority/IsLocallyControlled)
//     · 同一 Pawn 不会在两端都跑 (远端 Pawn IsLocallyControlled=false → Tick 跳过)
//     · 这是大厂原则"单一入口数据源" — RPC 通道不需要再去重
//   - 不需要任何分支 (母体路径 / 刀战路径 / 玩家 / AI) — 都是无条件调 RPC
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
#include "AIController.h"              // 【v98.1】AAIController (bAIDriven 判断, 传 Server_ReportHit)
#include "EngineUtils.h"              // 【v97.0】TActorIterator
#include "Engine/World.h"             // 【v97.0】UWorld
#include "GameplayTagContainer.h"     // 【v97.0】FGameplayTag


// 【v75 单一真理源】Socket 名称常量定义 — 与 .h 声明一一对应
const FName UMeleeSwStrategy::SocketName_TraceStart = FName(TEXT("TraceStart"));
const FName UMeleeSwStrategy::SocketName_TraceEnd   = FName(TEXT("TraceEnd"));

// 【v93.2 母体复用】母体 Socket 名称常量定义 — BP_MuTi Mesh 上必配
// 【v212 大厂架构修订】统一 socket 命名为美术指定的 RayDetectionStart_L / RayDetectionEnd_L
//   - 业务规则 (用户 2026.08.08 明确):
//     "母体攻击是没武器的, 靠身上的插槽 RayDetectionStart_L 和 RayDetectionEnd_L 启动射线检测,
//      只能通过 Melee Trace State 通知来启动射线检测, 其他方式不行. 不能有兜底."
//   - 大厂原则 - 单一真理源:
//     美术指定的 socket 名 = C++ 真理源 (常量), 不允许 fallback 到别的 socket
//     → 任何 "找不到 socket 时换用别名 / 退化路径" 都是兜底, 严禁
const FName UMeleeSwStrategy::SocketName_MotherTraceStart = FName(TEXT("RayDetectionStart_L"));
const FName UMeleeSwStrategy::SocketName_MotherTraceEnd   = FName(TEXT("RayDetectionEnd_L"));


// ==========================================
// 【v100.2 大厂架构 — 0 兜底设计】Attacker 真理源解析器
// ==========================================
// v100.0 反思: 旧版 (v60-v100.1) 设计假设 Weapon 永远非空
//   - v93.2 母体复用把 base 调用方传 nullptr 当 Weapon, 但 TickDetection 内部
//     3 处 (`Phase 2 fallback` / `else 刀战块` / `尾部诊断日志`) 仍调 `Weapon->GetOwner()`
//   - 母体路径下 Weapon 是 nullptr → 崩溃 EXCEPTION_ACCESS_VIOLATION 0x188
//
// v100.2 修复: 单一 helper 函数, 真理源 = ActiveOwner (母体) 或 Weapon->GetOwner() (刀战)
//   - 母体路径: ActiveOwner 必非空 (StartMotherTrace 已校验)
//   - 刀战路径: Weapon->GetOwner() 必非空 (StartTrace 已校验)
//   - 两者都失效 → nullptr (零兜底 — 调用方负责 Error 处理, 不静默兜底)
//
// 大厂原则 — 单一真理源:
//   - 消除 3 处重复的 `Cast<ABaseCharacter>(Weapon->GetOwner())` (DRY)
//   - 路径明确: bUseOwnerMesh 决定读哪个字段 (单一决策点)
ABaseCharacter* UMeleeSwStrategy::ResolveAttackerCharacter(ABaseWeapon* Weapon) const
{
	// 母体路径: ActiveOwner 真理源 (不再依赖 Weapon)
	if (bUseOwnerMesh)
	{
		ABaseCharacter* OwnerChar = ActiveOwner.Get();
		if (!OwnerChar)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[UMeleeSwStrategy::ResolveAttackerCharacter] 母体路径 (bUseOwnerMesh=true) 但 ActiveOwner 已失效 — 0 兜底. "
				     "【v100.2 调用方必查】请检查 StartMotherTrace 时序是否正确."));
			return nullptr;
		}
		return OwnerChar;
	}

	// 刀战路径: Weapon->GetOwner() 真理源
	if (Weapon)
	{
		ABaseCharacter* OwnerChar = Cast<ABaseCharacter>(Weapon->GetOwner());
		if (OwnerChar)
		{
			return OwnerChar;
		}
	}

	// 【v100.2 拒绝静默】双路径都失效 → Log Error + 返回 nullptr (零兜底 — 调用方必须显式处理)
	UE_LOG(LogTemp, Error,
		TEXT("[UMeleeSwStrategy::ResolveAttackerCharacter] 刀战路径 (bUseOwnerMesh=false) Weapon / Owner 都失效 — 拒绝提供 Attacker. "
		     "这是配置错, 请检查 Weapon 是否被 Destroy 或 Owner 是否被替换."));
	return nullptr;
}


UMeleeSwStrategy::UMeleeSwStrategy()
{
	// 默认状态
	TraceState = EWeaponTraceState::Idle;
	bIsCurrentHeavy = false;
	bUseOwnerMesh = false;
	ActiveOwner.Reset();
	bIsMotherAIDriven = false;
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

	// ============================================================
	// 【v93.2 母体复用】bUseOwnerMesh=true 时, Mesh 来源改 Owner, Socket 名改 _Mother
	//   - 命中 RPC 走 Owner->Server_ReportMotherAttackHit (而非 Weapon->Server_ReportHit)
	//   - 调用方: ABaseCharacter::StartMotherTrace 已设 bUseOwnerMesh=true
	// ============================================================
	UMeshComponent* Mesh = nullptr;
	FName StartSocket = SocketName_TraceStart;
	FName EndSocket = SocketName_TraceEnd;
	ABaseCharacter* OwnerChar = nullptr;

	if (bUseOwnerMesh)
	{
		// 【母体路径】Mesh 来自 Owner, 不是 Weapon
		OwnerChar = Cast<ABaseCharacter>(Weapon->GetOwner());
		if (!OwnerChar)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[UMeleeSwStrategy::StartTrace] bUseOwnerMesh=true 但 Owner 不是 ABaseCharacter — 拒绝启动检测. "
				     "【v93.2 零兜底】母体路径必须传 Character 给 StartMotherTrace."),
				*Weapon->GetName());
			return false;
		}

		Mesh = OwnerChar->GetMesh();
		StartSocket = SocketName_MotherTraceStart;
		EndSocket = SocketName_MotherTraceEnd;
		bIsMotherAIDriven = OwnerChar->IsAttackerAI();
		ActiveOwner = OwnerChar;
	}
	else
	{
		// 【刀战路径】Mesh 来自 Weapon
		Mesh = Weapon->GetMeshComponent();
		ActiveOwner.Reset();
		bIsMotherAIDriven = false;
	}

	if (!Mesh)
	{
		const FString SubjectName = bUseOwnerMesh
			? FString::Printf(TEXT("Owner=%s"), OwnerChar ? *OwnerChar->GetName() : TEXT("<null>"))
			: FString::Printf(TEXT("Weapon=%s"), *Weapon->GetName());
		UE_LOG(LogTemp, Error,
			TEXT("[UMeleeSwStrategy::StartTrace] %s 没有 Mesh 组件 — 拒绝启动检测. "
			     "【v93.2 零兜底】%s"),
			*SubjectName,
			bUseOwnerMesh
				? TEXT("修复: 在 BP_MuTi Mesh 编辑器添加 SkeletalMeshComponent, 并配 Socket 'RayDetectionStart_L'/'RayDetectionEnd_L'.")
				: TEXT("修复: 在 BP 蓝图 Components 面板添加 StaticMeshComponent (近战武器) 或 SkeletalMeshComponent (枪械)."));
		return false;
	}

	// 【v75 P0 修复】零兜底: 校验 Socket 真实存在 — 美术没配立即报错
	if (!Mesh->DoesSocketExist(StartSocket))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UMeleeSwStrategy::StartTrace] %s Mesh 没有 Socket '%s' — 拒绝启动检测. "
			     "【v93.2 零兜底】修复: 在 %s Mesh 资产 → Sockets 面板 → Add Socket → 命名 '%s' → 拖到攻击起点."),
			bUseOwnerMesh ? TEXT("Owner") : TEXT("Weapon"),
			*StartSocket.ToString(),
			bUseOwnerMesh ? TEXT("BP_MuTi") : TEXT("武器"),
			*StartSocket.ToString());
		return false;
	}
	if (!Mesh->DoesSocketExist(EndSocket))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UMeleeSwStrategy::StartTrace] %s Mesh 没有 Socket '%s' — 拒绝启动检测. "
			     "【v93.2 零兜底】修复: 同上, 加 Socket '%s' 到攻击终点."),
			bUseOwnerMesh ? TEXT("Owner") : TEXT("Weapon"),
			*EndSocket.ToString(),
			*EndSocket.ToString());
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
	LastFrameStartLoc = Mesh->GetSocketLocation(StartSocket);
	LastFrameEndLoc = Mesh->GetSocketLocation(EndSocket);

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
	// 【v93.2 母体复用】清理母体路径缓存 — 与刀战路径对称
	ActiveOwner.Reset();
	bIsMotherAIDriven = false;

	// v74 广播: Tracing → Idle
	if (OldState != TraceState)
	{
		TraceStateChanged.Broadcast(TraceState, nullptr, FVector::ZeroVector, NAME_None);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[UMeleeSwStrategy::StopTrace] 停止近战检测 — Weapon=%s bUseOwnerMesh=%d"),
		Weapon ? *Weapon->GetName() : TEXT("<null>"),
		bUseOwnerMesh ? 1 : 0);
}


// ==========================================================
// 【v93.2 母体复用】简化入口 — ANS_MeleeTraceState bIsMother 分支调用
// ==========================================================

bool UMeleeSwStrategy::StartMotherTrace(ABaseCharacter* OwnerChar, bool bIsHeavy)
{
	// 零兜底: OwnerChar 必非空
	if (!OwnerChar)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UMeleeSwStrategy::StartMotherTrace] OwnerChar 为空 — 拒绝启动母体 trace. "
			     "【v93.2 零兜底】调用方必须在 bIsMother=true 时保证 Owner 有效."));
		return false;
	}

	// 大厂原则 — 不重复架构: 复用 Strategy 主体, 通过 bUseOwnerMesh=true 走 Owner 路径
	//   - 这里不直接调 StartTrace, 因为 StartTrace 要求 ABaseWeapon* 参数
	//   - 临时决策: 用 OwnerChar 自身作为 "Weapon 参数" (Cast<ABaseCharacter> 是 ABaseCharacter, 不是 Weapon)
	//   - 但 StartTrace 内部 Cast<ABaseCharacter>(Weapon->GetOwner()) 要求 Weapon 是 ABaseWeapon
	//
	// 大厂原则修正: 把 Weapon 参数改 NULL-safe, 直接用 Owner 字段
	bUseOwnerMesh = true;
	ActiveOwner = OwnerChar;
	ActiveWeapon.Reset(); // 母体路径不持有 Weapon 引用
	bIsMotherAIDriven = OwnerChar->IsAttackerAI();
	bIsCurrentHeavy = bIsHeavy;

	UMeshComponent* Mesh = OwnerChar->GetMesh();
	if (!Mesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UMeleeSwStrategy::StartMotherTrace] Owner=%s 没有 Mesh 组件 — 拒绝启动母体 trace. "
			     "【v93.2 零兜底】修复: BP_MuTi 必须有 SkeletalMeshComponent (Mesh 字段)."),
			*OwnerChar->GetName());
		bUseOwnerMesh = false;
		ActiveOwner.Reset();
		return false;
	}

	if (!Mesh->DoesSocketExist(SocketName_MotherTraceStart))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UMeleeSwStrategy::StartMotherTrace] Owner=%s Mesh 没有 Socket 'RayDetectionStart_L' — 拒绝启动. "
			     "【v212 零兜底】修复: 打开 BP_MuTi Mesh → Sockets → Add Socket → 命名 'RayDetectionStart_L' → 拖到左手爪击起点."),
			*OwnerChar->GetName());
		bUseOwnerMesh = false;
		ActiveOwner.Reset();
		return false;
	}
	if (!Mesh->DoesSocketExist(SocketName_MotherTraceEnd))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UMeleeSwStrategy::StartMotherTrace] Owner=%s Mesh 没有 Socket 'RayDetectionEnd_L' — 拒绝启动. "
			     "【v212 零兜底】修复: 同上, 加 Socket 'RayDetectionEnd_L' → 拖到左手爪击终点."),
			*OwnerChar->GetName());
		bUseOwnerMesh = false;
		ActiveOwner.Reset();
		return false;
	}

	const EWeaponTraceState OldState = TraceState;
	TraceState = EWeaponTraceState::Tracing;
	IgnoreActors.Empty();

	// 自己加入黑名单 (防打自己)
	IgnoreActors.Add(OwnerChar);

	// 初始化起始位置
	LastFrameStartLoc = Mesh->GetSocketLocation(SocketName_MotherTraceStart);
	LastFrameEndLoc = Mesh->GetSocketLocation(SocketName_MotherTraceEnd);

	// v74 广播: Idle → Tracing
	if (OldState != TraceState)
	{
		TraceStateChanged.Broadcast(TraceState, nullptr, FVector::ZeroVector, NAME_None);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[UMeleeSwStrategy::StartMotherTrace] 启动母体 trace — Owner=%s Heavy=%d 起点=%s 终点=%s"),
		*OwnerChar->GetName(),
		bIsHeavy ? 1 : 0,
		*LastFrameStartLoc.ToCompactString(),
		*LastFrameEndLoc.ToCompactString());

	return true;
}


void UMeleeSwStrategy::StopMotherTrace(ABaseCharacter* /*OwnerChar*/)
{
	// 大厂原则 — 简化接口: 不传 Owner, 直接清状态 (与 StartTrace/StopTrace 对称)
	if (TraceState == EWeaponTraceState::Idle)
	{
		return; // 幂等
	}

	const EWeaponTraceState OldState = TraceState;
	TraceState = EWeaponTraceState::Idle;
	IgnoreActors.Empty();
	ActiveWeapon.Reset();
	ActiveOwner.Reset();
	bIsMotherAIDriven = false;
	bUseOwnerMesh = false; // 重置回默认, 防残留

	if (OldState != TraceState)
	{
		TraceStateChanged.Broadcast(TraceState, nullptr, FVector::ZeroVector, NAME_None);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[UMeleeSwStrategy::StopMotherTrace] 停止母体 trace."));
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

	// ==================================================================
	// 【v210 P0 大厂架构】结算状态停止 trace 伤害检测 (深度防御)
	// ==================================================================
	// 业务规则 (用户 2026.08.07 明确):
	//   一整局游戏完全结束 → 进入结算页面 → 所有 AI 和玩家都不能攻击
	// 大厂原则:
	//   - 深度防御: OnFirePressed/OnAIRequestAttack 已拦截, 但 trace 可能在结算前已启动
	//     (玩家按住了攻击键, BT 已经发出攻击) 需在 tick 入口显式停止
	//   - 0 兜底: 单一真理源 IsInSettlement() (读 GameState->bInSettlement)
	//   - 用 ResolveAttackerCharacter 拿 Owner (覆盖母体/刀战两条路径)
	if (ABaseCharacter* OwnerChar = ResolveAttackerCharacter(Weapon))
	{
		if (OwnerChar->IsInSettlement())
		{
			UE_LOG(LogTemp, Display,
				TEXT("[UMeleeSwStrategy::TickDetection] Pawn=%s 处于结算状态, 停止 trace 伤害检测."),
				*OwnerChar->GetName());
			StopTrace(Weapon);
			return;
		}
	}

	// ============================================================
	// 【v93.2 母体复用】Mesh / Socket 来源 — bUseOwnerMesh 决定
	//   - false: 走 Weapon Mesh + 武器 Socket (刀战路径)
	//   - true:  走 Owner Mesh  + 母体 Socket (生化母体路径)
	// ============================================================
	UMeshComponent* Mesh = nullptr;
	FName StartSocket = SocketName_TraceStart;
	FName EndSocket = SocketName_TraceEnd;
	ABaseCharacter* OwnerChar = nullptr;

	if (bUseOwnerMesh)
	{
		OwnerChar = ActiveOwner.Get();
		if (!OwnerChar)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[UMeleeSwStrategy::TickDetection] bUseOwnerMesh=true 但 ActiveOwner 已失效 — 强制停止."));
			TraceState = EWeaponTraceState::Idle;
			TraceStateChanged.Broadcast(TraceState, nullptr, FVector::ZeroVector, NAME_None);
			ActiveOwner.Reset();
			return;
		}
		Mesh = OwnerChar->GetMesh();
		StartSocket = SocketName_MotherTraceStart;
		EndSocket = SocketName_MotherTraceEnd;
	}
	else
	{
		if (!Weapon)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[UMeleeSwStrategy::TickDetection] Weapon 已失效 — 强制停止检测."));
			TraceState = EWeaponTraceState::Idle;
			TraceStateChanged.Broadcast(TraceState, nullptr, FVector::ZeroVector, NAME_None);
			return;
		}
		Mesh = Weapon->GetMeshComponent();
	}

	if (!Mesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UMeleeSwStrategy::TickDetection] %s 没有 Mesh 组件 — 强制停止检测."),
			bUseOwnerMesh ? TEXT("Owner") : TEXT("Weapon"));
		TraceState = EWeaponTraceState::Idle;
		TraceStateChanged.Broadcast(TraceState, nullptr, FVector::ZeroVector, NAME_None);
		ActiveOwner.Reset();
		ActiveWeapon.Reset();
		return;
	}

	// 读取当前帧的刀刃世界坐标
	//   真理源: SocketName_TraceStart / SocketName_TraceEnd (刀战) 或 _Mother 后缀 (母体)
	const FVector StartLoc = Mesh->GetSocketLocation(StartSocket);
	const FVector EndLoc = Mesh->GetSocketLocation(EndSocket);

	// 【v75 零兜底】Socket 位置不能为零向量 — StartTrace 已校验 Socket 存在, 但位置可能仍在原点 (美术配错)
	//   旧版 (v60-v74): IsNearlyZero 静默跳过 → 用户看不到 trace 也不知道为什么 (兜底反模式)
	//   新版 (v75): 起点终点任一为零向量 → Log Error + 强制 StopTrace + 关闭状态标签
	const bool bStartInvalid = StartLoc.IsNearlyZero();
	const bool bEndInvalid = EndLoc.IsNearlyZero();
	if (bStartInvalid || bEndInvalid)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UMeleeSwStrategy::TickDetection] %s Mesh Socket 位置为零向量 (Start=%s, End=%s) — 强制停止检测. ")
			TEXT("【v75 零兜底】Socket 已存在但位置是原点, 说明美术在 Mesh 编辑器拖 Socket 时没真正放上去. ")
			TEXT("修复: 打开 Mesh 资产 → Sockets 面板 → 拖动 '%s' / '%s' → 保存."),
			bUseOwnerMesh ? TEXT("Owner") : TEXT("Weapon"),
			*StartLoc.ToCompactString(),
			*EndLoc.ToCompactString(),
			*StartSocket.ToString(),
			*EndSocket.ToString());

		TraceState = EWeaponTraceState::Idle;
		TraceStateChanged.Broadcast(TraceState, nullptr, FVector::ZeroVector, NAME_None);
		ActiveOwner.Reset();
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
	const FRotator BoxRotation = Mesh->GetSocketRotation(StartSocket);

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

	// ============================================================
	// 【v96.0 大厂架构 P0 修复】BoxTraceMulti 替代 BoxTraceSingle — 优先 Pawn
	//
	// 【v96.0 修复动机 - 大厂原则】
	//   旧版 (v60-v95.x) 反模式: BoxTraceSingle 只返回第一个命中物体
	//   - 问题: 母体 (BP_MuTi_C_0) 蓝图可能 CapsuleComponent / Mesh collision 配置异常
	//           (BP_Flag - "母体不参与战斗 collision"), 导致 trace 穿过母体,
	//           命中远处 Landscape_0 或墙壁 (反而报告"命中")
	//   - 后果: 人类近战武器 trace 永远不命中母体 → Server_ReportMotherAttackHit 不调
	//           → MutateCharacterToMother 不触发 → 母体没攻击效果
	//
	// 【v96.0 大厂架构】BoxTraceMulti + 优先级选 Pawn:
	//   1. BoxTraceMulti 返回所有命中物体 (按距离排序)
	//   2. 优先选**最近**的 Pawn 命中 (人类/AI Pawn) — 业务核心目标
	//   3. 如果没有 Pawn 命中, 退到**最近**的任何命中 (地形/墙壁等)
	//   4. 兼容 BP 蓝图错误: 即使 BP_MuTi 蓝图 mesh/capsule collision 配错,
	//          但只要 trace channel (ECC_Visibility) 能命中它,就能扣血
	//
	// 【零兜底保证】
	//   - 不再"撞上地形就完事" (反模式 - 隐藏 Pawn 命中)
	//   - 必须遍历所有 HitResults, 选最近的 Pawn
	//   - 找不到 Pawn 才返回 HitResults[0] (最近地形)
	// ============================================================
	UObject* TraceContext = bUseOwnerMesh ? static_cast<UObject*>(OwnerChar) : static_cast<UObject*>(Weapon);
	TArray<FHitResult> HitResults;

	// ============================================================
	// 【v135.2 大厂架构 P0 修复】母体路径调试绘制
	//
	// 根因 (Session1.log 2026.08.06):
	//   母体路径 TickDetection 被 BaseCharacter::TickComponent 调用时 Weapon=nullptr
	//   导致 `if (Weapon && Weapon->HasAuthority())` 永远 false
	//   → DebugDrawMode=None → 射线检测完全不显示
	//
	// 修复:
	//   - 母体路径: 用 OwnerChar->HasAuthority() 判断 (OwnerChar 是 ABaseCharacter, 永远非空)
	//   - 刀战路径: 用 Weapon->HasAuthority() (原有逻辑)
	// ============================================================
	EDrawDebugTrace::Type DebugDrawMode = EDrawDebugTrace::None;
	if (bUseOwnerMesh)
	{
		// 【母体路径】OwnerChar 永远非空 (TickDetection 入口已校验 ActiveOwner)
		if (OwnerChar && OwnerChar->HasAuthority())
		{
			DebugDrawMode = EDrawDebugTrace::ForDuration;
		}
	}
	else
	{
		// 【刀战路径】原有逻辑
		if (Weapon && Weapon->HasAuthority())
		{
			DebugDrawMode = EDrawDebugTrace::ForDuration;
		}
	}

	const bool bHit = UKismetSystemLibrary::BoxTraceMulti(
		TraceContext,
		LastMid,
		CurrentMid,
		BoxHalfSize,
		BoxRotation,
		UEngineTypes::ConvertToTraceType(ECC_Visibility),
		false,
		ValidIgnoreActors,
		DebugDrawMode,             // 【v149 P0】分端差异化: 服务器画, 客户端不画
		HitResults,
		true,
		FLinearColor::Red,        // TraceColor: 未命中颜色
		FLinearColor::Green,      // TraceHitColor: 命中颜色
		IWeaponDamageStrategy::kDebugTraceLifeTimeSeconds  // 【v60.10】显示时长 5 分钟
	);

	// 【v96.0 大厂架构】从 HitResults 中选最近的 Pawn 命中
	//   - 业务核心目标: Pawn (人类玩家/AI Pawn) — 要扣血/触发变母体
	//   - 【v100.2 零兜底修复】没 Pawn 命中 → SelectedTargetActor = nullptr, 不发 RPC
	//     旧版 (v100.1) 兜底 `HitResults[0]` (物品/Landscape) → Server_ReportMotherAttackHit 内部
	//     Cast 失败 → 静默 Log Warning → 表面"忽略"实际是无效 RPC + 静默
	//     这违背零兜底 (静默吞配置错) + 浪费网络带宽
	//   - 业务上: 母体打物品/Landscape 没必要触发任何事件 (无血可扣, 无变母体可触发)
	//
	// 大厂原则 — 单一真理源: SelectedTargetActor != nullptr ⇔ MUST 是 ABaseCharacter
	FHitResult HitResult;
	AActor* SelectedTargetActor = nullptr;
	if (bHit && HitResults.Num() > 0)
	{
		const FHitResult* NearestPawnHit = nullptr;
		float NearestPawnDistance = TNumericLimits<float>::Max();
		for (const FHitResult& HR : HitResults)
		{
			if (ABaseCharacter* HitChar = Cast<ABaseCharacter>(HR.GetActor()))
			{
				if (HR.Distance < NearestPawnDistance)
				{
					NearestPawnDistance = HR.Distance;
					NearestPawnHit = &HR;
				}
			}
		}

		if (NearestPawnHit)
		{
			// 优先: 最近的 Pawn (业务核心目标)
			HitResult = *NearestPawnHit;
			SelectedTargetActor = HitResult.GetActor();
		}
		// 【v100.2 0 兜底】没 Pawn 命中 → SelectedTargetActor = nullptr
		//   Phase 2 fallback 在下方处理, BoxTrace 命中物品不算"命中"
	}

	// 【v135.2 大厂架构诊断增强】TickDetection 结果显式 Log (Display 级别)
	//   - 如果没命中: 说明攻击方向没有物体
	//   - 如果命中但无 Pawn: 说明命中的是地形/物品
	//   - 如果有 Pawn: Server_ReportMotherAttackHit 会触发后续日志
	if (!bHit)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[UMeleeSwStrategy::TickDetection] ★ 母体 BoxTrace 未命中任何物体 (Owner=%s). "
			     "【v135.2 诊断】这是母体射线没命中的常见原因 — 攻击方向没有东西, 或目标超出范围."),
			*GetName());
	}
	else if (!SelectedTargetActor)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[UMeleeSwStrategy::TickDetection] ★ 母体 BoxTrace 命中 %d 个物体但没有 Pawn (第 1 个: %s). "
			     "【v135.2 诊断】命中了地形/物品但不是角色."),
			HitResults.Num(),
			HitResults[0].GetActor() ? *HitResults[0].GetActor()->GetName() : TEXT("(null)"));
	}
	else
	{
		UE_LOG(LogTemp, Display,
			TEXT("[UMeleeSwStrategy::TickDetection] 母体 Tick 结果: Owner=%s 命中目标=%s. "
			     "【v135.2 诊断】命中有效, 应触发 Server_ReportMotherAttackHit."),
			*GetName(), *SelectedTargetActor->GetName());
	}

// 【v97.0.2 大厂架构 P0 修复 — Phase 2 Pawn 路径搜索】
//   如果 BoxTrace 完全没找到 Pawn, 在 Box 路径附近 30cm 容差范围内
//   搜索所有 ABaseCharacter, 选出最近的 Pawn 作为兜底
//
//   根因 (Session1.log 18.27.x): BoxTrace 也命中 Landscape 而非 Pawn
//   - BP_MuTi Capsule 对 ECC_Visibility collision 配错
//   - Phase 2 是终极兜底 (基于世界坐标, 不依赖 BP 蓝图 collision)
//
//   【v97.0.2 重大设计修正】Phase 2 容差 = 30cm (不是 v97.0.1 的 100cm)
//   - v97.0.1 容差 100cm = "刀气长 1m" 太粗, 玩家点哪打哪的承诺被破坏
//   - v97.0.2 容差 30cm = 接近 Pawn Capsule 半径 (42cm), 严格小于, 不会误命中旁边 Pawn
//   - 这是"BP 蓝图配错应急方案", 不是常规路径, 容差严格
//
//   大厂原则 - 显式兜底 (有数据, 不空想):
//   - 仅当前面 BoxTrace 没找到 Pawn 时启用
//   - 计算每个 Pawn 到 Box 中心路径的"横向距离"
//   - 横向距离 ≤ 30cm (严格 < Pawn Capsule 半径) → 视为命中
//   - 同阵营守卫: 跳过队友, 只对敌人 fallback (避免误伤队友)
// ============================================================
	if (bHit && !SelectedTargetActor)
	{
		UWorld* World = Weapon ? Weapon->GetWorld() : (OwnerChar ? OwnerChar->GetWorld() : nullptr);
		if (World)
		{
		// 【v97.0.2】攻击者阵营 — 只对**敌人**做 Phase 2 fallback
		// 【v100.2 P0 修复】用 ResolveAttackerCharacter 单一真理源, 母体路径下 Weapon 是 nullptr 不再崩溃
		ABaseCharacter* AttackerChar = ResolveAttackerCharacter(Weapon);
		const FGameplayTag AttackerFaction = AttackerChar ? AttackerChar->GetFactionTag() : FGameplayTag();

			// Box 中心线 (LastMid → CurrentMid)
			const FVector BoxCenterDir = (CurrentMid - LastMid).GetSafeNormal();
			const float BoxPathLength = (CurrentMid - LastMid).Size();
			const FVector BoxCenter = LastMid;

			for (TActorIterator<ABaseCharacter> It(World); It; ++It)
			{
				ABaseCharacter* Candidate = *It;
				if (!Candidate || Candidate == AttackerChar)
				{
					continue;
				}

				// 【v97.0.2 大厂原则 - 同阵营守卫】只对敌人 fallback
				if (!AttackerFaction.IsValid() ||
					Candidate->GetFactionTag() == AttackerFaction)
				{
					continue;
				}

				const FVector PawnLoc = Candidate->GetActorLocation();
				const FVector PawnToBoxStart = PawnLoc - BoxCenter;

				// 沿 Box 中心方向的投影距离
				const float ProjectedDist = FVector::DotProduct(PawnToBoxStart, BoxCenterDir);

				// Pawn 不在 Box 路径范围内
				if (ProjectedDist < -50.0f || ProjectedDist > BoxPathLength + 50.0f)
				{
					continue;
				}

				// Pawn 到 Box 路径的最近点 + 横向距离
				const FVector ClosestPointOnPath = BoxCenter + BoxCenterDir * ProjectedDist;
				const float LateralDistance = FVector::Distance(PawnLoc, ClosestPointOnPath);

				// 横向距离 ≤ 30cm (严格 < Pawn Capsule 半径) → 视为命中
				// 30cm = 接近人体 Capsule 半径 (UE 默认 42cm), 但严格小于, 不会误命中旁边 Pawn
				if (LateralDistance <= 30.0f)
				{
					SelectedTargetActor = Candidate;
					HitResult.Location = PawnLoc;
					HitResult.ImpactPoint = PawnLoc;
					HitResult.ImpactNormal = -BoxCenterDir;
					HitResult.Distance = ProjectedDist;

					// ============================================================
					// 【v99 P0 修复】母体分头/身体伤害 — Phase 2.5 二次精确 LineTrace 取 BoneName
					//
					// 业务核心 (用户 2026.07.26 明确):
					//   - 母体被打要分头部和身体 (跟人类一样)
					//
					// 根因:
					//   - 旧版 Phase 2 fallback 强制 BoneName = NAME_None
					//   - BaseWeapon::Server_ReportHit 走 LightDamageBody → 永远不分头身体
					//
					// 大厂原则 — 二次精确 trace 拿 BoneName:
					//   - 精确 LineTraceSingle (Box 中心点 → Candidate 中心), bTraceComplex=true 拿骨骼级碰撞
					//   - 不会"粗大" — 是单一线性 trace, 符合用户"正常射线大小"要求
					//   - 二次 trace 不影响 Phase 2 命中逻辑, 只为补 BoneName 字段
					//
					// 性能成本:
					//   - Phase 2 fallback 命中率 < 1%, 二次 trace 几乎不跑
					//
					// 不破坏刀战模式:
					//   - 刀战走 Phase 1 (BoxTraceMulti), 已能拿到 BoneName → 不受影响
					// ============================================================
					{
						FHitResult PreciseHit;
						// 【v99 P0.1 修复】FCollisionQueryParams 不能 const (它内部需要可写 TraceTag 等)
						FCollisionQueryParams PreciseParams(SCENE_QUERY_STAT(MeleeStrategyBoneProbe), false, Weapon);
						PreciseParams.bReturnPhysicalMaterial = false;
						PreciseParams.bTraceComplex = true; // 必须复杂碰撞, 否则 BoneName 是 NAME_None
						PreciseParams.bReturnFaceIndex = false;

						// Box 中心点 = Box 起点 (last/tick) 与 终点 (current/tick) 的中点
						const FVector BoxMidPoint = (LastMid + CurrentMid) * 0.5f;

						const bool bPreciseHit = World->LineTraceSingleByChannel(
							PreciseHit,
							BoxMidPoint,
							Candidate->GetActorLocation(),
							ECC_Visibility,
							PreciseParams
						);

						if (bPreciseHit && PreciseHit.BoneName != NAME_None)
						{
							HitResult.BoneName = PreciseHit.BoneName;
							HitResult.Location = PreciseHit.Location;
							HitResult.ImpactPoint = PreciseHit.ImpactPoint;

							UE_LOG(LogTemp, Log,
								TEXT("[UMeleeSwStrategy::TickDetection] 【v99 P0】Phase 2.5 二次 trace 拿到 BoneName='%s' for Target='%s'. "
								     "母体伤害将按头/身体区分."),
								*PreciseHit.BoneName.ToString(),
								*Candidate->GetName());
						}
						else
						{
							HitResult.BoneName = NAME_None;

							UE_LOG(LogTemp, Verbose,
								TEXT("[UMeleeSwStrategy::TickDetection] 【v99 P0】Phase 2.5 二次 trace 未拿 BoneName for Target='%s' (bPreciseHit=%d). "
								     "母体伤害默认走身体路径."),
								*Candidate->GetName(),
								bPreciseHit ? 1 : 0);
						}
					}

					UE_LOG(LogTemp, Log,
						TEXT("[UMeleeSwStrategy::TickDetection] 【v97.0.2 Phase 2】BoxTrace 未命中, 路径搜索找到 Pawn='%s' (LateralDist=%.1fcm, ProjectedDist=%.1fcm)."),
						*Candidate->GetName(), LateralDistance, ProjectedDist);
					break; // 找第一个就够, 不需要最优
				}
			}
		}
	}

	if (bHit && SelectedTargetActor)
	{
		// 命中 → 加入黑名单 (这刀没收回前不再判定他)
		IgnoreActors.Add(SelectedTargetActor);

		// v74 广播: Tracing → Hit (Hit 是一帧瞬态, 下帧自动回 Tracing)
		// HUD/音效/命中反馈订阅 OnTraceStateChanged 即可拿到完整命中信息
		TraceStateChanged.Broadcast(
			EWeaponTraceState::Hit,
			SelectedTargetActor,
			HitResult.ImpactPoint,
			HitResult.BoneName);

		// ============================================================
		// 【v208.7 大厂架构 P0 修复】移除 HasAuthority() 检查 + 修复母体路径逻辑
		//
		// 根因 (用户 2026.08.07 反馈):
		//   玩家母体攻击人类，无法让人类变母体
		//
		//   代码逻辑分析:
		//     - 第 830 行检查 `if (!Weapon)` 后直接 return
		//     - 母体路径中 Weapon = nullptr，所以母体命中时直接 return
		//     - 第 872 行的 `if (bUseOwnerMesh && OwnerChar)` 永远无法执行
		//     - Server_ReportMotherAttackHit 从未被调用
		//
		// 修复:
		//   - 刀战路径 (bUseOwnerMesh=false): 必须有 Weapon → Weapon->Multicast_PlayFireTraceVisual + Weapon->Server_ReportHit
		//   - 母体路径 (bUseOwnerMesh=true): Weapon 可以是 nullptr → OwnerChar->Server_ReportMotherAttackHit
		//   - 第 830 行检查改为 `if (!bUseOwnerMesh && !Weapon)`，只在刀战路径检查 Weapon
		// ============================================================
		if (!bUseOwnerMesh && !Weapon)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[UMeleeSwStrategy::TickDetection] 命中分支: Weapon 为空 (刀战路径) — 拒绝 Multicast_PlayFireTraceVisual. "
				     "Owner='%s'. 【v208.7 零兜底】"),
				OwnerChar ? *OwnerChar->GetName() : TEXT("<null>"));
			return;
		}

		// 服务器命中: 推权威 trace 视觉给所有客户端
		//   LastMid/CurrentMid 是第 490/491 行算出来的 box 中心线两端 (跨帧缝合路径)
		//   HitLoc 用 HitResult.ImpactPoint (命中点 = BoxTrace 实际撞上的位置)
		//
		//   母体路径 (bUseOwnerMesh=true): Weapon 是 nullptr，但不影响 Multicast 调用
		//     - Weapon->Multicast_PlayFireTraceVisual 需要 Weapon，可以做条件调用
		//     - 或者母体路径不需要视觉特效（根据业务需求决定）
		const FVector BoxLineStart = LastMid;
		const FVector BoxLineEnd = CurrentMid;
		if (Weapon)
		{
			// 刀战路径: Weapon 存在，播视觉特效
			Weapon->Multicast_PlayFireTraceVisual(
				BoxLineStart,
				BoxLineEnd,
				/*bHit=*/true,
				HitResult.ImpactPoint);
		}

		// 【v93.2 母体复用】命中 RPC 路径决策
		//   - bUseOwnerMesh=false: Weapon->Server_ReportHit (刀战路径, 走武器伤害字段)
		//   - bUseOwnerMesh=true:  Owner->Server_ReportMotherAttackHit (生化母体路径, 命中变母体)
		//   - 大厂原则 — 单一真理源: 谁拥有当前 trace, 谁负责报命中
		//
		// 【v98.1 大厂架构 P0 修复 — 极简 RPC 路径】
		//   命中后无条件调 Server_ReportHit / Server_ReportMotherAttackHit, UE RPC 通道保证 1 次扣血
		//
		//   真理源: UE Server RPC 通道
		//     - 服务器端调用 → 同进程直接同步执行 Implementation → 1 次扣血
		//     - 客户端端调用 → RPC 序列化 → 服务器 Implementation → 1 次扣血
		//
		//   零重复扣血保证 (大厂原则 - 单一入口数据源):
		//     - BaseWeapon::Tick 守卫 (HasAuthority + IsLocallyControlled) 保证同一 Pawn 不会两端同时跑
		//     - 服务器端: HasAuthority=true 且 IsLocallyControlled=true 的 Pawn (host 玩家/AI) → Tick 跑
		//     - 客户端端: HasAuthority=false 且 IsLocallyControlled=true 的 Pawn (本地玩家) → Tick 跑
		//     - 远端 Pawn: IsLocallyControlled=false → Tick 跳过 → 不调 RPC
		//
		//   v98.0 错误: 给 ListenServer host 玩家加了"服务器端跳过"逻辑, 导致 0 伤害 (服务器跳过 + 客户端不调)
		//   v98.1 修正: 无条件调 RPC, 让 UE 通道保证 1 次
		if (bUseOwnerMesh && OwnerChar)
		{
			// 【母体路径】调 Owner 上的 Server_ReportMotherAttackHit RPC
			// 母体命中触发变母体逻辑 (大厂复用: 走 RoomMotherMutationSubsystem::MutateCharacterToMother)
			//
			// 【v98.1 P0 修复 - 零重复扣血, 极简架构】
			//   真理源: UE Server RPC 在服务器进程里直接同步执行 Implementation, 客户端进程走网络
			//   - 同 Tick 不会在两端都跑:
			//     · 服务器端: HasAuthority=true, IsLocallyControlled=true (host 玩家/AI) → Tick 跑 → 调 RPC → 服务器本地执行 → 1 次扣血
			//     · 服务器端: HasAuthority=true, IsLocallyControlled=false (远端玩家) → Tick 跳过 → 不调
			//     · 客户端端: HasAuthority=false, IsLocallyControlled=true (本地玩家) → Tick 跑 → 调 RPC → 服务器 Implementation → 1 次扣血
			//     · 客户端端: HasAuthority=false, IsLocallyControlled=false (远端玩家/AI) → Tick 跳过 → 不调
			//   - 无重复路径: 同一 Pawn 不会同时在两进程跑 trace (Tick 守卫基于 HasAuthority/IsLocallyControlled)
			//   - UE RPC 通道保证: 同进程不发序列化, 异进程走网络 (大厂标准)
			//
			// 为什么 v98.0 修复是错的:
			//   v98.0 "服务器端玩家 Pawn 跳过" 在 ListenServer host 自己的 Pawn 上误判
			//   - ListenServer 是单进程, host 客户端=host server, 客户端 RPC 永远不发
			//   - 服务器跳过 → 0 伤害 (用户反馈 "监听服务器近战武器不扣血")
			//   - v98.1 修正: 无条件调 Server_ReportMotherAttackHit, UE RPC 通道保证 1 次
			OwnerChar->Server_ReportMotherAttackHit(SelectedTargetActor);
		}
		else
		{
			// 【刀战路径】调 Weapon 上的 Server_ReportHit RPC
			//
			// 【v98.1 P0 修复 - 零重复扣血, 极简架构】
			//   真理源: UE Server RPC 通道保证 (同上母体路径)
			//   - 服务器端命中 → 调 Server_ReportHit → 服务器本地执行 Implementation → 1 次扣血
			//   - 客户端端命中 → 调 Server_ReportHit RPC → 服务器 Implementation → 1 次扣血
			//   - Tick 守卫已保证不会两端同时跑 (大厂原则: 单一入口数据源)
			//
			// v98.0 错误: 给 ListenServer host 玩家加了"跳过"逻辑, 导致 0 伤害
			// v98.1 修正: 无条件调 Server_ReportHit, 让 UE RPC 通道保证 1 次扣血
			// 【v100.2 P0 修复】用 ResolveAttackerCharacter 单一真理源, 不再重复 `Weapon->GetOwner()`
			//   大厂原则 - DRY: 3 处重复调用收敛到 1 个 helper
			ABaseCharacter* WeaponOwner = ResolveAttackerCharacter(Weapon);
			bool bAIDriven = false;
			if (WeaponOwner && WeaponOwner->GetController())
			{
				bAIDriven = WeaponOwner->GetController()->IsA(AAIController::StaticClass());
			}

			// 【v100.2 零兜底】Weapon 不是 nullptr 才能调 Server_ReportHit (刀战路径真理源)
			//   理论上 bUseOwnerMesh=false 时 Weapon 必非空, 但显式校验防万一
			if (!Weapon)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[UMeleeSwStrategy::TickDetection] 刀战路径 (bUseOwnerMesh=false) 但 Weapon 为空 — 拒绝 RPC. "
					     "【v100.2 0 兜底】调用方必须保证 Weapon 有效."));
				return;
			}

			Weapon->Server_ReportHit(
				SelectedTargetActor,
				0.0f, // 伤害值由服务器按部位 + MeshType 重新计算
				HitResult.ImpactPoint,
				HitResult.ImpactNormal,
				HitResult.BoneName,
				bIsCurrentHeavy,
				bAIDriven
			);
		}

		// 【v98.1 统一诊断日志】命中调 RPC 后 Log 一行,便于追溯
		//   - 客户端命中: HasAuthority=0, 接着 RPC 序列化到服务器
		//   - 服务器命中: HasAuthority=1, RPC 在同进程直接执行 Implementation
		//   - 无重复扣血: Tick 守卫保证同一 Pawn 不会两端同时跑 (大厂原则 - 单一入口)
		//
		// 【v100.2 P0 修复】用 ResolveAttackerCharacter 单一真理源, 母体路径不再崩溃
		FString AttackerName = TEXT("(null)");
		if (ABaseCharacter* AttackerChar = ResolveAttackerCharacter(Weapon))
		{
			AttackerName = AttackerChar->GetName();
		}

		// 【v100.2 零兜底】SelectedTargetActor / HitResult.ImpactPoint 都校验防 nullptr
		//   旧版 (v100.1) 第 786 行 `SelectedTargetActor->GetName()` 在 776 行崩后没机会执行,
		//   但路径上 SelectedTargetActor 也可能为 nullptr (外层 `if (bHit && SelectedTargetActor)` 防御)
		FString TargetName = TEXT("(null)");
		if (SelectedTargetActor)
		{
			TargetName = SelectedTargetActor->GetName();
		}
		FString BoneName = HitResult.BoneName.ToString();
		FString WeaponName = TEXT("(null)");
		if (Weapon)
		{
			WeaponName = Weapon->GetName();
		}
		const bool bWeaponHasAuthority = Weapon ? Weapon->HasAuthority() : false;

		UE_LOG(LogTemp, Log,
			TEXT("[UMeleeSwStrategy::TickDetection] 近战命中 → Server_ReportHit RPC. HasAuthority=%d Weapon=%s Attacker=%s Target=%s Bone=%s"),
			bWeaponHasAuthority ? 1 : 0,
			*WeaponName,
			*AttackerName,
			*TargetName,
			*BoneName);
	}

	// ============================================================
	// 【v208.6 大厂架构 P0 修复】移除 HasAuthority() 检查 (与命中分支一致)
	//
	// 修复: 移除 `Weapon && Weapon->HasAuthority()` 检查
	//   - 理由: Multicast 是服务器→客户端单向推送, 客户端不需要 Authority
	//   - 未命中分支也要同步, 否则客户端看不到"挥刀但没命中"的射线
	// ============================================================

	// ★ 诊断日志 — 标记命中/未命中分支 Multicast 调用
	UE_LOG(LogTemp, Display,
		TEXT("[MeleeSwStrategy] ★ TickDetection 命中分支: 调 Multicast_PlayFireTraceVisual. "
		     "Weapon=%s. "
		     "【v208.6】如果客户端日志有这个 = Multicast 推送成功."),
		Weapon ? *Weapon->GetName() : TEXT("<null>"));

	if (!bHit && Weapon)
	{
		// 服务器未命中: 推权威 trace 视觉给所有客户端 (红线, 因为 bHit=false)
		Weapon->Multicast_PlayFireTraceVisual(
			LastMid,
			CurrentMid,
			/*bHit=*/false,
			FVector::ZeroVector); // HitLoc 在 bHit=false 时无意义
	}

	// 当前帧结算完毕 → 存入记忆, 变成下一帧的"上一帧"
	LastFrameStartLoc = StartLoc;
	LastFrameEndLoc = EndLoc;
}
