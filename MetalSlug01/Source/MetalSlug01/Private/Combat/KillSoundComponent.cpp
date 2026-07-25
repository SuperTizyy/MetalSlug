// ==========================================
// 击杀音效 Component 实现【v100 大厂架构 — 零等待 / 零兜底】(2026.07.26)
//
// 大厂架构 — 与 UMotherSpawnSoundComponent 同款模式(本地播放,不复制):
//   - 组件不复制(音效本地播放,服务器 / 客户端各自独立)
//   - 服务器 / 客户端各自本地订阅 Multicast_NotifyKill → 各自播放一次
//   - 双发保证: 服务器本地 RPC 实现也跑一次,客户端 RPC 到达后再跑一次
//
// 【v100 关键决策: 复用 FKillStreakIconInfo 数据表】
//   - DT_KillStreakIconInfo 是项目已有资产(v41 大厂架构),按 EKillStreakType 行驱动
//   - v100 在 FKillStreakIconInfo 加 KillSound 字段 → 同一行同时管图标 + 音效
//   - 零新建 DataTable / 零新建枚举 — 大厂原则: 不重复架构,改一起改
//
// 【音效 3D 衰减】
//   - UGameplayStatics::PlaySoundAtLocation 自带 3D 衰减
//   - 玩家能从声音方向感判断 Victim 被击杀的位置
//   - 与 MotherSpawnSound"听母体从哪来" 完全对称
//
// 【零兜底 — 严禁】
//   - 缺表 / 缺 Row / 缺 Sound → Log Error + 拒绝播放
//   - 缺 World 时不允许 fallback 到世界原点
//   - 不允许"找不到 Row 时静默跳过"(必须显式报错,让策划修 DT)
// ==========================================
#include "Combat/KillSoundComponent.h"

// UE 引擎 API
#include "Engine/World.h"                 // GetWorld
#include "Engine/DataTable.h"             // UDataTable + FindRow
#include "Kismet/GameplayStatics.h"       // PlaySoundAtLocation
#include "Sound/SoundBase.h"              // USoundBase (RPC 序列化需要完整类型)

// 项目内
#include "Characters/BaseCharacter.h"     // Owner + GetActorLocation
#include "Data/Tables/KillIconTableRow.h" // FKillStreakIconInfo 行结构
#include "Systems/RoomGameMode.h"          // ARoomGameMode (v100 单一真理源)


// ==========================================
// 1. 构造函数
// ==========================================
/**
 * UKillSoundComponent 构造函数
 *
 * 大厂原则 - 零 Tick:
 *   - 音效一次性播放,不依赖 Component Tick
 *   - 不持有生命周期 Timer (与 Mother 粒子不同 — 粒子附 5 秒,音效只是一瞬间)
 *   - 不持有任何状态字段(无 LastKillTimeSeconds 节流 / 无 bIsPlaying 等)
 *     —— 因为本组件对每次 Multicast_NotifyKill 调用都查表播放一次,无状态需要管理
 */
UKillSoundComponent::UKillSoundComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// 不需要 SetIsReplicatedByDefault:
	//   - 音效本地播放,服务器 / 客户端各自独立
	//   - 双发保证靠 Multicast RPC 跨网络同步(组件不参与复制)
}


// ==========================================
// 2. 配置注入 — 单一真理源
// ==========================================
/**
 * SetKillStreakIconDataTable — 由 ARoomGameMode 启动时调一次
 *
 * 大厂原则(与 v37 WeaponAttachmentDataTable 同款):
 *   - 真理源 = GM.KillStreakIconDataTable
 *   - 组件字段不暴露 EditDefaultsOnly(BP 不能配 — 真理源在 GM)
 *   - 注入时机: GameMode::InitGameState / PreInitializeComponents
 *   - 立即生效 — PlayKillSound 立刻能查到表
 *
 * @param InDataTable 关联 DT_KillStreakIconInfo
 */
void UKillSoundComponent::SetKillStreakIconDataTable(UDataTable* InDataTable)
{
	if (!InDataTable)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillSound] SetKillStreakIconDataTable: 传入空指针! "
				 "Owner=%s. 必须在 ARoomGameMode 派生类配置 KillStreakIconDataTable."),
			*GetNameSafe(GetOwner()));
		// 零兜底: 不接受空指针,保留原值(若有)
		// 这样后续 PlayKillSound 仍能感知"未注入",继续 Log Error
		return;
	}

	KillStreakIconDataTable = InDataTable;

	UE_LOG(LogTemp, Log,
		TEXT("[KillSound] SetKillStreakIconDataTable: 注入成功. DataTable=%s, Owner=%s"),
		*InDataTable->GetName(), *GetNameSafe(GetOwner()));
}


// ==========================================
// 3. 公共 API — 播放
// ==========================================
/**
 * PlayKillSound — 在 Owner 当前位置播放对应连杀音效
 *
 * 触发方: ABaseCharacter::Multicast_NotifyKill_Implementation(单一入口)
 *        经 UKillSoundComponent Resolver 调到这里
 *
 * 【v100 大厂架构 — 校验链精简】
 *   1. Owner(ABaseCharacter)有效
 *   2. World 有效
 *   3. KillStreakIconDataTable 已注入(由 GM Init)
 *   4. EKillStreakType 合法(非 None)
 *   5. DataTable 找到对应 Row(按枚举名)
 *   6. Row.KillSound 资产已配置
 *
 * 任一失败 → Log Error + 拒绝播放(不抛异常,不影响 Multicast 其他环节)
 * 全部通过 → UGameplayStatics::PlaySoundAtLocation(3D 衰减,本地播放)
 */
void UKillSoundComponent::PlayKillSound(EKillStreakType InStreakType)
{
	// ===== 校验层 0: StreakType 合法性 =====
	//   None 是默认值,触发层不该传它 — 显式拒绝,提示配错
	if (InStreakType == EKillStreakType::None)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillSound] PlayKillSound: 收到 EKillStreakType::None, 拒绝播放. "
				 "Owner=%s. 触发层应在调用前判 Headshot / OneKill / TwoKills / ThreeKills / FourKills / FiveKills."),
			*GetNameSafe(GetOwner()));
		return;
	}

	// ===== 校验层 1: Owner =====
	ABaseCharacter* OwnerChar = ResolveOwnerCharacter();
	if (!OwnerChar)
	{
		// ResolveOwnerCharacter 内部已 Log Error
		return;
	}

	// ===== 校验层 2: World =====
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillSound] PlayKillSound: World 无效. Owner=%s, StreakType=%d. 拒绝播放."),
			*OwnerChar->GetName(), static_cast<int32>(InStreakType));
		return;
	}

	// ===== 校验层 3a: Lazy 注入 DataTable (v100 单一真理源) =====
	//   - 不在 BeginPlay 注入 (v40.6 BP archetype 陷阱)
	//   - 第一次调用时若未注入, 从 GM 拉取一次
	//   - 失败 → bHasTriedLazyInject=true 标记,后续直接拒绝(不刷日志)
	if (!EnsureKillStreakDataTable())
	{
		// EnsureKillStreakDataTable 内部已 Log Error
		return;
	}

	// ===== 校验层 3b: DataTable(注入后再次确认) =====
	if (!KillStreakIconDataTable)
	{
		// 防御性 — EnsureKillStreakDataTable 已保证这里必非空
		UE_LOG(LogTemp, Error,
			TEXT("[KillSound] PlayKillSound: EnsureKillStreakDataTable 返回 true 但 KillStreakIconDataTable 仍为空. 这是内部 bug."),
			*OwnerChar->GetName(), static_cast<int32>(InStreakType));
		return;
	}

	// ===== 校验层 4: 查找 Row =====
	//   UE 5.6 FindRow 要求 FName 行名 — 我们约定 EKillStreakType 的 DisplayName = Row Name
	//   大厂原则: 表必须按枚举名精确命名(Headshot / OneKill / TwoKills / ThreeKills / FourKills / FiveKills)
	//   找不到 → 显式报错(BP 策划忘配 Row)
	const FString EnumValueName = UEnum::GetValueAsString(InStreakType);
	FString RowNameStr = EnumValueName;
	// 去掉 "EKillStreakType::" 前缀
	int32 ColonPos = INDEX_NONE;
	if (RowNameStr.FindLastChar(TEXT(':'), ColonPos))
	{
		RowNameStr = RowNameStr.RightChop(ColonPos + 1);
	}

	const FName RowName(*RowNameStr);
	static const FString ContextString(TEXT("KillSoundLookup"));

	FKillStreakIconInfo* Row = KillStreakIconDataTable->FindRow<FKillStreakIconInfo>(
		RowName, ContextString, false);

	if (!Row)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillSound] PlayKillSound: DataTable '%s' 找不到 Row '%s' (StreakType=%d). "
				 "Owner=%s. "
				 "【修复路径】在 DT_KillStreakIconInfo.uasset 加上 RowName='%s' 的行 "
				 "并填 KillSound 字段."),
			*KillStreakIconDataTable->GetName(),
			*RowNameStr,
			static_cast<int32>(InStreakType),
			*OwnerChar->GetName(),
			*RowNameStr);
		return;
	}

	// ===== 校验层 5: Sound =====
	if (!Row->KillSound)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillSound] PlayKillSound: DataTable '%s' Row '%s' 的 KillSound 为空. "
				 "Owner=%s. "
				 "【修复路径】在 DT_KillStreakIconInfo.uasset 的 RowName='%s' 行 KillSound 字段配 USoundBase."),
			*KillStreakIconDataTable->GetName(),
			*RowNameStr,
			*OwnerChar->GetName(),
			*RowNameStr);
		return;
	}

	// ===== 执行层: 3D 衰减播放 =====
	//
	// 大厂原则 — 3D 位置而非 2D:
	//   - 击杀音效应该带空间感 (玩家能听到 Victim 被击杀的方向)
	//   - PlaySoundAtLocation 自动 3D 衰减
	//   - 与 MotherSpawnSound"听母体从哪来"完全对称
	//
	// 大厂原则 — 显式优于隐式:
	//   - Owner->GetActorLocation() 一次性取坐标
	//   - 不缓存 Location (与 RPC 同步链独立)
	//   - VolumeMultiplier / PitchMultiplier 不在此调 — SoundCue 自带
	UGameplayStatics::PlaySoundAtLocation(
		World,
		Row->KillSound,
		OwnerChar->GetActorLocation());

	UE_LOG(LogTemp, Display,
		TEXT("[KillSound] PlayKillSound: 音效已本地播放. Owner=%s, StreakType=%s, Sound=%s"),
		*OwnerChar->GetName(),
		*RowNameStr,
		*GetNameSafe(Row->KillSound));
}


// ==========================================
// 4. 私有辅助 — Lazy Resolve Owner
// ==========================================
/**
 * ResolveOwnerCharacter — 按需 lazy 解析 Owner,避开 BeginPlay 缓存陷阱
 *
 * 大厂原则(同 v40.6 / v99.3 / v100 MotherSpawnSoundComponent):
 *   - 不缓存 raw pointer: BP archetype 会覆写 C++ 默认字段
 *   - 每次 GetOwner() + Cast<T>: 真理源 = UE 标准 API
 *   - Cast 失败 / GetOwner 无效 → Log Error + return nullptr
 */
ABaseCharacter* UKillSoundComponent::ResolveOwnerCharacter() const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillSound] ResolveOwnerCharacter: GetOwner() 返回 null. "
				 "Component 必须挂在 Actor 上."));
		return nullptr;
	}

	const ABaseCharacter* OwnerChar = Cast<ABaseCharacter>(OwnerActor);
	if (!OwnerChar)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillSound] ResolveOwnerCharacter: Owner '%s' 不是 ABaseCharacter 派生. "
				 "本组件必须挂在 BP_*.uasset (继承 ABaseCharacter) 上. "
				 "实际 OwnerClass=%s."),
			*OwnerActor->GetName(),
			*OwnerActor->GetClass()->GetName());
		return nullptr;
	}

	// Cast 在 const 上下文返回 const,工程上不修改字段,这里做一次解包
	return const_cast<ABaseCharacter*>(OwnerChar);
}


// ==========================================
// 5. 私有辅助 — 单一真理源 Lazy 注入
// ==========================================
/**
 * EnsureKillStreakDataTable — v100 单一真理源懒注入
 *
 * 大厂原则 — 懒注入 (Lazy Injection):
 *   - 不在 BeginPlay 主动注入(BeginPlay 时序不可靠,见 v40.6 BP archetype 陷阱)
 *   - 在 PlayKillSound 第一次调用前检查;若未注入,从 ARoomGameMode 拉取一次
 *   - 注入成功 → 标记已尝试 + KillStreakIconDataTable 非空 + return true
 *   - 注入失败 → 标记已尝试 + return false + 仅 Log Error 一次
 *
 * 与 v37 WeaponAttachmentDataTable 模式同款:
 *   - 真理源 = GameMode.KillStreakIconDataTable (我们即将加上)
 *   - 运行时: 本组件 lazy 向 GM 拉
 *   - 找不到 GM / GM 字段为空 → Log Error + 强制修复 (零兜底)
 */
bool UKillSoundComponent::EnsureKillStreakDataTable()
{
	// 已经持有 → fast path
	if (KillStreakIconDataTable)
	{
		return true;
	}

	// 已经尝试过且失败 → 直接拒绝,不再刷日志
	if (bHasTriedLazyInject)
	{
		return false;
	}

	// 标记已尝试(无论成败)
	bHasTriedLazyInject = true;

	// ===== 从 GM 拉 =====
	ABaseCharacter* OwnerChar = ResolveOwnerCharacter();
	if (!OwnerChar)
	{
		// 已有 Log Error,不再重复
		return false;
	}

	UWorld* World = OwnerChar->GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillSound] EnsureKillStreakDataTable: Owner->World 为 null. Owner=%s."),
			*OwnerChar->GetName());
		return false;
	}

	ARoomGameMode* GM = World->GetAuthGameMode<ARoomGameMode>();
	if (!GM)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillSound] EnsureKillStreakDataTable: AuthGameMode 不是 ARoomGameMode (实际=%s). "
				 "Owner=%s. "
				 "【v100 单一真理源】连杀 DataTable 只能在 ARoomGameMode 派生类中配."),
			*GetNameSafe(World->GetAuthGameMode()),
			*OwnerChar->GetName());
		return false;
	}

	UDataTable* ResultTable = GM->KillStreakIconDataTable;
	if (!ResultTable)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillSound] EnsureKillStreakDataTable: GM->KillStreakIconDataTable 为空! "
				 "Owner=%s. "
				 "【v100 修复路径】必须在 BP_GM_RoomGameMode.uasset 的 Class Defaults → Room|Data → "
				 "Kill Streak Icon DataTable 字段配 DT_KillStreakIconInfo. "
				 "【零兜底】不许默认分配, 不许 fallback. "
				 "【共享真理源】此表与 HUD 连杀图标共用, DataTable 加 KillSound 列即可."),
			*OwnerChar->GetName());
		return false;
	}

	// 注入成功
	KillStreakIconDataTable = ResultTable;
	UE_LOG(LogTemp, Log,
		TEXT("[KillSound] EnsureKillStreakDataTable: 注入成功. DataTable=%s, Owner=%s"),
		*ResultTable->GetName(),
		*OwnerChar->GetName());
	return true;
}
