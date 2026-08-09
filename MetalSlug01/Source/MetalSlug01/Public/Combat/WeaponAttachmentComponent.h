// ==========================================
// 版权声明：在项目设置的描述页面填写您的版权信息。
// ==========================================
//
// @file WeaponAttachmentComponent.h
// @brief 武器网络复制 + 挂载 + SyncWeapon 子系统 — 从 ABaseCharacter 抽离
//
// 【架构定位 — 2026.07.12 P0 大厂重构】
//   BaseCharacter.cpp 已 3957 行, 单文件维护成本爆炸.
//   本文件把 BaseCharacter 中"武器生成/挂载/网络同步"相关逻辑抽出到独立 ActorComponent.
//
// 【职责 (What)】
//   1. 武器网络复制 (CurrentWeapon: Replicated Using OnRep_CurrentWeapon)
//   2. OnRep_CurrentWeapon 客户端挂载 (Phase 1.2 已修: 零兜底)
//   3. EquipWeapon 服务器换枪
//   4. RequestWeaponSpawn 公开入口 (关卡预放 AI)
//   5. SpawnAndEquipWeapon 服务器权威生成
//   6. SetSpawnLoadout 写入字段
//   7. FindWeaponAttachmentConfig 严格匹配 (Phase 1.3 已修: 零兜底)
// 8. SyncWeaponFromConfig 从 ConfigSO 同步
// 9. SetMeleeConfig / GetSpawnWeaponID / SetCharacterID / SetSpawnWeaponID
//
// 【不负责 (What NOT)】
//   - 武器本身的数据 (在 ABaseWeapon)
//   - 武器溶解 (在 WeaponDissolveComponent, BaseWeapon 内嵌)
//   - 武器伤害 (在 ABaseWeapon::Server_ReportHit / ABaseCharacter::Server_ReportAIAttackHit)
//   - 武器动画 (在 ABaseWeapon::GetAttackMontage)
//
// 【大厂原则 - 单一真理源】
//   - CurrentWeapon 在本组件, 不再在 BaseCharacter (迁移后)
//   - CharacterID/SpawnWeaponID 在本组件, 跟 BaseCharacter 旧字段共存 (后续迁移)
//   - WeaponDataTable 来自 GM->WeaponDataTable (不要复制到本组件)
//
// 【大厂原则 - 零兜底】
//   - OnRep_CurrentWeapon: WeaponID 反查失败 → Log Error + return (Phase 1.2)
//   - FindWeaponAttachmentConfig: 找不到精确匹配 → Log Error + return nullptr (Phase 1.3)
//   - 不允许"通配 fallback" — 配置错必须显式化
//
// 【Owner 访问约定】
//   本组件通过 Owner (ABaseCharacter) 访问:
//     - Owner->GetMesh() (挂载用)
//     - Owner->GetController<APlayerController>() (HUD 刷新)
//     - GM->WeaponDataTable (经 Cast<ARoomGameMode>)
//
// 【v54 大厂架构重构】UAIProfileAsset 已删除
//   - Owner 不再有 MeleeProfile 字段
//   - 关卡预放 AI 走 ConfigSO (AMeleeAIController::SetupMeleeAI 末尾调 SetMeleeConfig)
//
// 【UE 5.6 注意事项】
//   .generated.h 必须用**裸文件名** (UE 5.6 UHT 字面 OrdinalIgnoreCase 比对, 不处理路径前缀)
// ==========================================
#pragma once

// ==========================================
// 头文件包含区
// ==========================================
#include "CoreMinimal.h"                          // UE 引擎核心最小化
#include "Components/ActorComponent.h"            // UActorComponent 基类

// DataTable 行结构 (FWeaponInfo / FWeaponAttachmentConfig)
#include "Data/Tables/WeaponTableRow.h"

// 武器槽位类型 (v51 — 生化模式运行时切换)
#include "Weapons/WeaponSlotType.h"

// UE 自动生成的头文件 (必须最后, 且用裸文件名 — UE 5.6 严格模式)
#include "WeaponAttachmentComponent.generated.h"

// 前置声明 (加快编译)
class ABaseCharacter;                            // 拥有者 (Owner)
class ABaseWeapon;                                // 武器
class APlayerController;                          // UE 原生玩家控制器
class UDataTable;                                // 数据表
class UAIBehaviorConfigSO;                       // AI 行为配置 (v54 替代 UAIProfileAsset)


/**
 * 武器槽位类型前置声明 (v52 — 生化模式运行时切换)
 *
 * 完整定义见: Weapons/WeaponSlotType.h
 * 大厂原则 — 单一真理源:
 *   - 真理源 = PlayerState.SelectedWeaponID1/2/3 (Primary / Secondary / Melee)
 *   - 大厅 UI 已支持选 3 把武器, 运行时可切换
 *   - AI 路径 (关卡预放 AI / 大厅入队 AI) 不强制多槽, 走兼容路径
 */
enum class EWeaponSlotType : uint8;


/**
 * 【v78 大厂架构 — 简化版】武器状态结构体
 *
 * 大厂原则 - 单一复制链:
 *   旧问题 (v51-v77):
 *     - CurrentWeapon 和 CurrentWeaponSlot 是两个独立 UPROPERTY(Replicated)
 *     - CurrentWeaponSlot 用 DOREPLIFETIME(Replicated) 没有 OnRep → 客户端永远是默认值 Primary
 *     - 导致武器切换时 CurrentWeaponSlot 不更新 → 所有武器切命令都失败
 *   新方案 (v78):
 *     - 合并 ActiveWeapon + ActiveSlot 为 FWeaponState struct，通过 DOREPLIFETIME 普通复制
 *     - CurrentWeaponSlot 保留 ReplicatedUsing = OnRep_CurrentWeaponSlot 触发客户端回调
 *     - OnRep_CurrentWeaponSlot 读取 WeaponState 作为真理源
 *
 * 大厂原则 - 向后兼容:
 *   - CurrentWeapon / CurrentWeaponSlot 字段保留（作为缓存视图）
 *   - WeaponState 是真 Truth Source，OnRep_CurrentWeaponSlot 读取 WeaponState 并回写字段
 *   - Server_SwitchToWeaponSlot 写入 WeaponState + CurrentWeaponSlot
 */
USTRUCT(BlueprintType)
struct FWeaponState
{
	GENERATED_BODY()

	// 当前激活武器指针（nullptr = 空槽位）
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<ABaseWeapon> ActiveWeapon = nullptr;

	// 当前激活槽位类型
	UPROPERTY(BlueprintReadOnly)
	EWeaponSlotType ActiveSlot = EWeaponSlotType::Primary;
};


/**
 * 【v81 大厂架构 — 客户端武器姿态修复】武器挂载运行时数据
 *
 * 根因 (v77-v80):
 *   - 服务器 SpawnAndEquipWeapon 调 AttachToComponent + SetActorRelativeLocation/Rotation/Scale
 *   - UE Actor 复制会同步 Attach 关系 (RootComponent 挂在哪), 但**不会**同步 RelativeLocation/Rotation
 *   - 客户端的武器 Actor 已经"挂"在角色 Mesh 上 (Socket 位置 = 默认姿态), 但没有策划配置的偏移
 *   - IsWeaponAttachedToSocket 返回 true → OnRep_CurrentWeapon 跳过补偏移 → 客户端姿态错
 *
 * 修复 (v81 大厂架构):
 *   - 真理源 = 服务器写入 Replicated RuntimeData 字段
 *   - OnRep_CurrentWeaponSlot / OnRep_CurrentWeapon 读取 RuntimeData 强制应用偏移
 *   - 删除 "IsWeaponAttachedToSocket 跳过" 反模式 (兜底行为)
 *   - 零兜底: 客户端必须严格应用服务器配置的偏移, 不允许"已经挂上就跳过"
 *
 * 复制策略:
 *   - 作为 Component 的 UPROPERTY(Replicated) 字段
 *   - 服务器 Server_SpawnAllWeapons / SpawnAndEquipWeapon 写入
 *   - 客户端 OnRep_* 触发时读取并应用
 */
USTRUCT(BlueprintType)
struct FWeaponAttachmentRuntime
{
	GENERATED_BODY()

	// 武器挂载的 Socket 名 (策划在 DT_WeaponAttachmentConfig 配置)
	UPROPERTY(BlueprintReadOnly)
	FName SocketName = NAME_None;

	// 武器相对角色的本地空间位置 (厘米)
	UPROPERTY(BlueprintReadOnly)
	FVector RelativeLocation = FVector::ZeroVector;

	// 武器相对角色的本地空间旋转
	UPROPERTY(BlueprintReadOnly)
	FRotator RelativeRotation = FRotator::ZeroRotator;

	// 武器相对角色的本地空间缩放
	UPROPERTY(BlueprintReadOnly)
	FVector RelativeScale3D = FVector(1.0f, 1.0f, 1.0f);

	// 服务器是否已完成应用 (用于客户端 OnRep 守卫 — 只有 true 才是合法配置)
	UPROPERTY(BlueprintReadOnly)
	bool bIsValid = false;
};

/**
 * @class UWeaponAttachmentComponent
 * @brief 武器网络复制 + 挂载子系统 — BaseCharacter 的"武器持有层"
 *
 * 设计模式: ActorComponent 自治子系统
 *   - Owner 暴露: 通过 Owner->MeleeProfile / Owner->GetMesh() 访问
 *   - 网络复制: FWeaponState 通过 DOREPLIFETIME; CurrentWeaponSlot 通过 ReplicatedUsing = OnRep_CurrentWeaponSlot
 *   - 服务器权威: 所有生成/销毁走服务器
 *
 * 使用方法:
 *   1. BaseCharacter 构造函数中 CreateDefaultSubobject<UWeaponAttachmentComponent>
 *   2. GetLifetimeReplicatedProps 添加: DOREPLIFETIME(UWeaponAttachmentComponent, CurrentWeapon)
 *   3. BeginPlay 中 Owner 缓存
 *
 * 后续迁移:
 *   - BaseCharacter::CurrentWeapon → 迁移到本组件
 *   - BaseCharacter::OnRep_CurrentWeapon → 转发到本组件
 *   - BaseCharacter::EquipWeapon → 转发到本组件
 *   - BaseCharacter::RequestWeaponSpawn → 转发到本组件
 *   - BaseCharacter::SpawnAndEquipWeapon → 转发到本组件
 *   - BaseCharacter::FindWeaponAttachmentConfig → 转发到本组件
 *   - BaseCharacter::SetSpawnLoadout → 转发到本组件
 *   - BaseCharacter::SetMeleeConfig → 转发到本组件 (v54 改名, 原 SetMeleeProfile)
 *   - BaseCharacter::SyncWeaponFromConfig → 转发到本组件 (v54 改名, 原 SyncWeaponFromProfile)
 */
UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UWeaponAttachmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// ==========================================
	// 构造函数
	// ==========================================

	/**
	 * 构造函数: 设置默认值
	 * 目的: 不创建子组件 (本组件没有子组件), 仅初始化字段默认值
	 */
	UWeaponAttachmentComponent();

	// 【2026.07.12 P0 重构】Friend 双向授权:
	//   - ABaseCharacter 调本组件 protected 方法 (OnRep_CurrentWeapon / EquipWeapon 等)
	friend class ABaseCharacter;

	/**
	 * 必须重写此函数以注册需要网络同步的 UPROPERTY
	 * 同步内容: CurrentWeapon (武器指针)
	 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ==========================================
	// 网络复制回调
	// ==========================================

	/**
	 * 武器指针改变时的回调 (网络复制通知)
	 *
	 * 【Phase 1.2 修复 — 零兜底】反查 WeaponID 失败 → Log Error + return
	 *
	 * 触发场景:
	 *   - 服务器 PossessedBy → SpawnAndEquipWeapon → CurrentWeapon=新武器 → 复制到客户端
	 *   - 客户端 OnRep_CurrentWeapon 触发 → 走反查 + 挂载逻辑
	 *
	 * 大厂修复架构 (Phase 1.2):
	 *   - 服务器和本地控制的角色已在 PossessedBy 中处理 → 跳过
	 *   - 先脱挂旧武器 (防御: 复活场景 OldWeapon != null)
	 *   - CharacterID 未就绪时静默跳过 (网络复制优先级)
	 *   - 反查 WeaponID 失败 → Log Error + return (Phase 1.2 零兜底)
	 *   - 拒绝通配 fallback (Phase 1.3 零兜底 — FindWeaponAttachmentConfig 内)
	 *
	 * @param OldWeapon 网络复制前的旧武器指针 (用于脱挂)
	 */
	UFUNCTION()
	void OnRep_CurrentWeapon(ABaseWeapon* OldWeapon);

	// ==========================================
	// 服务器公开接口
	// ==========================================

	/**
	 * 装备武器 (供 GameMode 调用)
	 *
	 * 流程:
	 *   1. 仅服务器调用
	 *   2. 如果手里已经有武器 → 销毁旧的
	 *   3. SpawnActor 生成新武器
	 *   4. 挂载到默认 Socket "WeaponSocket_L"
	 *
	 * 注: 这是简化接口 (没有走 DataTable), GameMode 直接指定 Class
	 *     复杂武器 (DataTable 驱动) 走 RequestWeaponSpawn
	 *
	 * @param WeaponClassToEquip 要装备的武器蓝图类
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void EquipWeapon(TSubclassOf<ABaseWeapon> WeaponClassToEquip);

	/**
	 * 【v54.3 大厂架构重构 — 接受 Class 引用, 跳过 DT_WeaponInfo 中间层】武器生成请求入口
	 *
	 * 用户原话 2026.07.16:
	 *   "DefaultWeaponRowName 属性为什么不直接引用 DT_WeaponInfo 表的 WeaponBlueprint 内容啊"
	 *
	 * 旧设计 (v54.2):
	 *   - RequestWeaponSpawn(FString WeaponID) — 字符串 ID, 内部查 DT_WeaponInfo 反查 Class
	 *   - 中间层冗余, 2 个真理源 (Config + DT_WeaponInfo), 字符串拼写错永远不可见
	 *
	 * 新设计 (v54.3):
	 *   - RequestWeaponSpawn(TSubclassOf<ABaseWeapon> WeaponClass) — 直接 Class 强类型
	 *   - 编译期拒绝非 ABaseWeapon 派生资产 (字符串无法做到)
	 *   - 调用方在调用前 LoadSynchronous, 真理源直接传递
	 *
	 * 调用方:
	 *   - AMeleeAIController::SetupMeleeAI → ConfigSO.LevelPlacedWeaponClass.LoadSynchronous() → RequestWeaponSpawn
	 *   - URoomSpawnSubsystem::SpawnAIInternal → Request.WeaponID 查 DT_WeaponInfo 拿 Class → RequestWeaponSpawn
	 *   - URoomSpawnSubsystem::HandlePlayerRequestSpawn → PS->SelectedWeapon1ID 查 DT_WeaponInfo 拿 Class → RequestWeaponSpawn
	 *   - URoomSpawnSubsystem::RequestRespawn → 复活走 Controller.CachedWeaponID 查 DT_WeaponInfo 拿 Class → RequestWeaponSpawn
	 *
	 * 内部直接调 SpawnAndEquipWeaponByClass, 复用完整链路 (挂载配置/HUD)
	 *
	 * 零兜底:
	 *   - WeaponClass 必须非空 (调用方 LoadSynchronous 必须成功)
	 *   - null → Log Error + return (拒绝 Spawn)
	 *
	 * @param WeaponClass 武器 BP 类 (TSoftClassPtr<ABaseWeapon>::LoadSynchronous() 或 DT_WeaponInfo 反查)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|AI")
	void RequestWeaponSpawn(TSubclassOf<ABaseWeapon> WeaponClass);

	/**
	 * 【v93 大厂架构新增】卸下并销毁当前武器 — 公开 API
	 *
	 * 业务背景:
	 *   母体变异时 (URoomSpawnSubsystem::MutatePawnToMother), 旧 Pawn 的武器必须立即销毁
	 *   - 武器以旧 Pawn 为 Owner (FActorSpawnParameters::Owner = OldPawn), 旧 Pawn Destroy 会级联销毁
	 *   - 但 UE 销毁是延迟一帧, 期间武器 Mesh 仍可见 (挂在旧 Pawn 视觉坐标)
	 *   - 必须显式调 Destroy, 让客户端立刻收到 Destruction Bunch, 立即消失
	 *
	 * 大厂原则 — 单一销毁入口:
	 *   - 旧版 SpawnAIInternal 临时 inline 调 OldWeapon->Destroy() (v56.3 P0) — 重复架构
	 *   - 现在统一走本接口 (单一真理源), 母体变异 / 死亡重置 / 武器切换都能复用
	 *
	 * 服务器权威:
	 *   - 必须 HasAuthority() 才执行 (其他客户端不调用)
	 *   - 销毁后 CurrentWeapon=nullptr 自动 Replicate → 所有客户端 OnRep_CurrentWeapon 触发
	 *
	 * 零兜底:
	 *   - CurrentWeapon 已为 nullptr → Log Warning + return false (不报错, 业务正常)
	 *   - 销毁失败 (UE 内部错误) → Log Error + return false
	 *
	 * @return true=成功销毁或本来就无武器, false=销毁失败 (UE 内部异常)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	bool UnequipCurrentWeapon();

	// ==========================================
	// 公开获取/写入接口
	// ==========================================

	/**
	 * 获取当前装备的武器
	 *
	 * @return CurrentWeapon 指针 (可能为 nullptr)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat")
	ABaseWeapon* GetCurrentWeapon() const { return CurrentWeapon; }

	/**
	 * 【v51 大厂架构 — 生化模式】获取指定槽位的武器
	 *
	 * 适用场景:
	 *   - 双武器 Spawn 后, 客户端 HUD 需要分别显示 Primary 和 Melee 图标
	 *   - 切换槽位时, 调用方需要查目标槽位的武器是否存在
	 *
	 * 零兜底:
	 *   - 槽位未生成武器 → 返回 nullptr (合法状态, 调用方按 nullptr 跳过)
	 *
	 * @param Slot 槽位类型 (Primary / Melee)
	 * @return 该槽位的武器指针 (未生成则 nullptr)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Weapons|Slot")
	ABaseWeapon* GetWeaponInSlot(EWeaponSlotType Slot) const;

	/**
	 * 【v58 大厂架构 — FPS枪战】检查槽位是否有武器
	 *
	 * 用于 Server_RequestSwitchToSlot 校验目标槽位是否有效
	 *
	 * @param Slot 槽位类型 (Primary / Secondary / Melee)
	 * @return 槽位是否有武器 (非空)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Weapons|Slot")
	bool HasWeaponInSlot(EWeaponSlotType Slot) const;

	/**
	 * 【v51 大厂架构 — 生化模式】获取当前激活的槽位 (服务器 + 客户端同步)
	 *
	 * 用于 HUD 高亮当前槽位 / 输入处理查"我正在用哪个"
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Weapons|Slot")
	EWeaponSlotType GetCurrentWeaponSlot() const { return CurrentWeaponSlot; }

	/**
	 * 【v52 P0】获取槽位武器数组 (3 槽位, 只读访问)
	 *
	 * 调用方: BaseCharacter::Server_SwitchWeaponSlot (循环找下一个非空槽位)
	 *
	 * 零兜底:
	 *   - 返回的是 TArray 内部 const 引用, 不允许修改
	 *   - 槽位索引: Primary=0, Secondary=1, Melee=2 (与 EWeaponSlotType 严格对齐)
	 *
	 * 【v52 P1】无 UFUNCTION 标记: UHT 5.6 严格模式禁止 TObjectPtr 出现在 UFUNCTION
	 *                 边界 (函数参数/返回值), 纯 C++ helper, 调用方都是 C++.
	 */
	const TArray<TObjectPtr<ABaseWeapon>>& GetWeaponsInSlotArray() const { return WeaponsInSlot; }

	/**
	 * 【v51 大厂架构 — 生化模式】服务器权威切槽位 (大厂原则 — 服务器权威)
	 *
	 * 调用方: BaseCharacter::Server_SwitchToWeaponSlot (RPC) → 本方法
	 *
	 * 行为:
	 *   - 仅服务器调用 (HasAuthority 校验)
	 *   - 隐藏当前槽位武器 (SetVisibility(false) + SetActorHiddenInGame(true))
	 *   - 显示目标槽位武器 (SetVisibility(true) + SetActorHiddenInGame(false))
	 *   - 更新 CurrentWeapon 指向目标武器 (向后兼容单武器 API)
	 *   - 更新 CurrentWeaponSlot (Replicated → 客户端 OnRep 同步)
	 *
	 * 零兜底:
	 *   - 目标槽位无武器 → Log Error + 拒绝切 (强制修复 Spawn 链路)
	 *   - 调用方非服务器 → Log Error + 拒绝 (强制走 Server RPC)
	 *
	 * @param TargetSlot 目标槽位 (Primary / Melee)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapons|Slot")
	void Server_SwitchToWeaponSlot(EWeaponSlotType TargetSlot);

	/**
	 * 【v52 P0 扩展 — 3 槽位】一次性 Spawn 3 把武器到对应槽位 (服务器权威)
	 *
	 * 调用方: RoomSpawnSubsystem::HandlePlayerRequestSpawn / SpawnAIInternal 末尾
	 *
	 * 流程:
	 *   1. PrimaryClass   非空 → SpawnActor → 存入 WeaponsInSlot[Primary]
	 *   2. SecondaryClass 非空 → SpawnActor → 存入 WeaponsInSlot[Secondary]
	 *   3. MeleeClass     非空 → SpawnActor → 存入 WeaponsInSlot[Melee]
	 *   4. 任一生成失败 → 销毁已生成的武器 + Log Error (零兜底)
	 *   5. 服务器切到 Primary 槽位 (默认)
	 *
	 * 零兜底:
	 *   - 3 个 Class 同时为空 → Log Error + return (拒绝 Spawn)
	 *   - 至少主武器 (Primary) 必选 (玩家大厅必有)
	 *
	 * @param PrimaryClass   主武器 Class (大厅 SelectedWeaponID1 反查)
	 * @param SecondaryClass 副武器 Class (大厅 SelectedWeaponID2 反查, 允许为空)
	 * @param MeleeClass     近战武器 Class (大厅 SelectedWeaponID3 反查, 允许为空)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapons|Slot")
	void Server_SpawnAllWeapons(TSubclassOf<ABaseWeapon> PrimaryClass, TSubclassOf<ABaseWeapon> SecondaryClass, TSubclassOf<ABaseWeapon> MeleeClass);

	/**
	 * 【v56 内部辅助】Spawn 一把武器 + 注入 Strategy
	 *
	 * 调用方: Server_SpawnBothWeapons 内部
	 *
	 * 大厂原则 (单一职责):
	 *   - 不重复现有 SpawnAndEquipWeapon 链路 (复用挂载配置查表)
	 *   - 仅负责"双槽位系统特有的"额外装配: Strategy 注入
	 *
	 * 零兜底:
	 *   - 任何失败 → Log Error + return nullptr
	 *   - Strategy 未注入 → 调用方拒绝继续 (Spawn 链路中断)
	 *
	 * @param WeaponClass 武器 BP 类
	 * @param Slot        槽位 (Primary / Melee / Secondary) — 决定 MeshType
	 * @return 生成的武器指针 (失败 nullptr)
	 */
	ABaseWeapon* SpawnAndConfigureWeaponInSlot(TSubclassOf<ABaseWeapon> WeaponClass, EWeaponSlotType Slot);

	/**
	 * 统一的武器/角色预填入口
	 *
	 * @param InCharacterID 角色 ID (用于查找挂载配置)
	 * @param InWeaponID    武器 ID (用于查找 DataTable)
	 *
	 * 大厂原则 - 显式赋值优于隐式跳过:
	 *   无论是否为空都写入, 确保外部覆盖意图被尊重
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Attachment")
	void SetSpawnLoadout(const FString& InCharacterID, const FString& InWeaponID);

	/** 只读 getter, 给 AI Controller 用 */
	UFUNCTION(BlueprintPure, Category = "Combat|Attachment")
	const FString& GetSpawnWeaponID() const { return SpawnWeaponID; }

	/**
	 * 只读 getter, 给 ABaseCharacter::PossessedBy 调试日志用
	 * 注: 命名带 ForLog 后缀, 表明仅用于诊断输出, 业务逻辑不应依赖此方法
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Attachment")
	const FString& GetCharacterIDForLog() const { return CharacterID; }

	/** Setter (用于 ResetSpawnWeaponID 等场景) */
	UFUNCTION(BlueprintCallable, Category = "Combat|Attachment")
	void SetSpawnWeaponID(const FString& InWeaponID);

	/** Setter */
	UFUNCTION(BlueprintCallable, Category = "Combat|Attachment")
	void SetCharacterID(const FString& InCharacterID);

	/**
	 * 【v54 大厂架构重构 — Profile → Config】直接注入武器配置
	 *
	 * 调用方:
	 *   - AMeleeAIController::SetupMeleeAI 末尾 (关卡预放 AI 自举路径)
	 *   - AZombieAIController::SetupZombieAI 末尾
	 *
	 * 流程:
	 *   1. 解析 ConfigSO.DefaultWeaponRowName → SetSpawnLoadout
	 *   2. 调用 SetSpawnLoadout 后, 写入 Pawn.SpawnWeaponID (运行时真理源)
	 *
	 * 【v49 大厂重构】已删除 CharacterRowName 解析 (字段已废弃)
	 *
	 * @param InConfig AI 行为配置 DataAsset (DA_AIBehaviorConfig_*)
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Config")
	void SetMeleeConfig(UAIBehaviorConfigSO* InConfig);

	// ==========================================
	// 内部方法 (公开方便外部访问, 但不应该直接调)
	// ==========================================

	/**
	 * 根据 WeaponID 查表并生成+装备武器 (服务器权威)
	 *
	 * 流程:
	 *   1. 幂等检查: 已有 CurrentWeapon → 跳过
	 *   2. GM + WeaponDataTable 校验
	 *   3. 查表 FWeaponInfo (WeaponID → WeaponBlueprint)
	 *   4. 查表 FWeaponAttachmentConfig (CharacterID + WeaponID → Socket/Offset)
	 *   5. SpawnActor 生成武器
	 *   6. 挂载到 Socket (SnapToTargetNotIncludingScale)
	 *   7. 应用 RelativeLocation/Rotation/Scale
	 *   8. 写 CurrentWeapon (触发 Replicated)
	 *   9. 刷新 HUD 武器图标 (仅本地玩家)
	 *
	 * @param WeaponClass 武器 BP 类 (TSoftClassPtr<ABaseWeapon>::LoadSynchronous() 或 DT_WeaponInfo 反查)
	 */
	void SpawnAndEquipWeapon(TSubclassOf<ABaseWeapon> WeaponClass);

	/**
	 * 【v60 大厂架构】武器火控配置初始化 — 唯一入口
	 *
	 * 调用方:
	 *   - SpawnAndEquipWeapon (服务器 Spawn 末尾)
	 *   - SpawnAndConfigureWeaponInSlot (服务器多槽位生成末尾)
	 *
	 * 流程:
	 *   1. 按 WeaponClass 反查 DT_WeaponInfo (GM->WeaponDataTable)
	 *   2. 写入 Weapon->WeaponRowName (供 Server_ReportHit 读伤害)
	 *   3. 调 Weapon->WeaponFireComponent->InitializeFromWeaponConfig
	 *
	 * 零兜底:
	 *   - 字段缺失 → Log Warning / Error (强制修复)
	 *   - DT 找不到 → Log Error 跳过 (不静默)
	 */
	void InitializeWeaponFireConfigFromClass(ABaseWeapon* NewWeapon, TSubclassOf<ABaseWeapon> WeaponClass);

	/**
	 * 从 ConfigSO 同步武器数据 (v54 改名, 原 SyncWeaponFromProfile)
	 *
	 * 调用方: 应该在 SpawnAndEquipWeapon 之前调用 (PossessedBy 中)
	 *
	 * 逻辑:
	 *   1. 如果 SpawnWeaponID 为空, 从传入 ConfigSO.DefaultWeaponRowName 填充
	 *
	 * 【v49 大厂重构】删除 CharacterID 同步:
	 *   - 旧版: 从 Profile.CharacterRowName 同步到 Pawn.CharacterID
	 *   - 新版: FindWeaponAttachmentConfig 用 Pawn->GetClass() 强类型, 不需要 CharacterID
	 */
	void SyncWeaponFromConfig();

	/**
	 * 【v54 大厂架构重构 — UAIProfileAsset 已删除】SyncWeaponFromProfile 转发壳整个删除
	 *   旧: SyncWeaponFromProfile() 读已删除的 Owner->MeleeProfile 字段 (UAIProfileAsset 整个类不存在)
	 *   新: SyncWeaponFromConfig() — 不依赖任何 Profile 字段
	 *
	 * 【保留说明】SyncWeaponFromProfile 函数整个删除, 不留转发壳 (与 SetMeleeProfile 不同的处理)
	 *   - 因为 SyncWeaponFromProfile 内部完全依赖已删除的 MeleeProfile 字段
	 *   - 即使保留函数签名, 内部也无处读 Profile, 等于空实现
	 *   - 所有调用方已迁移到 SyncWeaponFromConfig
	 */

	/**
	 * 【v49 大厂架构重构 — 单一真理源】在 WeaponAttachmentDataTable 中查找挂载配置
	 *
	 * 大厂原则 (零兜底):
	 *   - 输入是真实 Pawn Class + Weapon Class (BP 强类型, 不会字符串错位)
	 *   - 匹配规则: TargetCharacter Class == PawnClass 且 WeaponBlueprint Class == WeaponClass
	 *   - 找不到精确 → Log Error + return nullptr
	 *   - 任何降级"通配兼容"被删除, 武器挂载配置错误必须显式化
	 *
	 * @param InPawnClass  角色 BP 类 (例如 BP_GruntAI_C)
	 * @param InWeaponClass 武器 BP 类 (例如 BP_Weapon_Knife_C)
	 * @return 匹配的配置行 (找不到精确匹配返回 nullptr, 已 Log Error)
	 */
	struct FWeaponAttachmentConfig* FindWeaponAttachmentConfig(
		TSubclassOf<ABaseCharacter> InPawnClass,
		TSubclassOf<ABaseWeapon> InWeaponClass) const;

	/**
	 * 【v37 单一真理源】获取武器挂载数据表
	 *
	 * 改用 ARoomGameMode::WeaponAttachmentDataTable, 不读组件字段 (v37 已删)
	 *
	 * 错误处理 (零兜底):
	 *   - Owner 无效 / 无 World → Log Error + return nullptr
	 *   - AuthGameMode 不是 ARoomGameMode → Log Error + return nullptr
	 *   - GM 上 WeaponAttachmentDataTable 字段没配 → Log Error + return nullptr
	 *
	 * @return 武器挂载数据表指针 (失败返回 nullptr)
	 */
	class UDataTable* GetWeaponAttachmentDataTable() const;

	/**
	 * 【v76 大厂架构】播放"拿起武器"音效 (按 EWeaponSlotType 查表)
	 *
	 * 单入口播放点 (服务器/客户端共一个调用点, 不在 BP ANS 散落):
	 *   - Server_SwitchToWeaponSlot 末尾 (服务器权威广播后)
	 *   - OnRep_CurrentWeaponSlot 末尾 (客户端 OnRep 触发, 仅 Auth 路径外补播)
	 *
	 * 大厂原则 - 单一真理源 (与 GetWeaponAttachmentDataTable 完全对称):
	 *   - 本函数通过 GM->GetWeaponSoundMapAsset() 查 DA_WeaponSoundMap
	 *   - 不在 BP 蓝图 / 组件字段 / 单个武器字段上重复配置
	 *
	 * 大厂原则 - 零兜底:
	 *   - GM 未配 → 已 Log Error (前面 accessor), 本函数不再重复
	 *   - Slot.Entry.Sound 为空 → Log Error + 拒绝播放 (强制修复 DA)
	 *
	 * 网络策略:
	 *   - 服务器播本地 + Multicast_RPC 让所有客户端也听到 (RPC 由 Server_SwitchToWeaponSlot 末尾发)
	 *   - OnRep 触发只补"远端玩家外其他人" 的客户端, 不重复自己 (通过 bIsLocallyControlled 守卫)
	 *
	 * @param NewSlot 切换到的目标槽位 (Primary / Secondary / Melee)
	 */
	void PlayEquipSoundForSlot(EWeaponSlotType NewSlot);

	// ==========================================
	// 【v200 大厂架构新增】丢弃武器到地面
	// ==========================================

	/**
	 * 【v200.2.18 大厂架构 — 重命名】将主武器丢弃到地面
	 *
	 * 旧版命名问题 (v200):
	 *   - "Server_DropPrimaryWeapon" 名字像 RPC (UE 大厂规范: Server_* 必须配 UFUNCTION(Server))
	 *   - 实际是普通 C++ 函数, 只在服务器进程内部被 ABaseCharacter::Server_DropPrimaryWeapon_Implementation 调用
	 *   - 命名误导 → 未来如果有人在客户端调用, 会静默失败, 根因不可见
	 *
	 * 新版 (v200.2.18) — 大厂原则 (零命名误导):
	 *   - 重命名 HandleDropPrimaryWeapon (Handle 前缀 = 业务逻辑处理器, 非 RPC)
	 *   - 与 HandleTryPickupWeapon 对称, 单一真理源命名约定
	 *
	 * 职责:
	 *   - 校验: 必须是服务器 + 手里有主武器
	 *   - 脱挂武器与角色 (DetachFromActor)
	 *   - 调用 WeaponDropComponent->StartDroppedState (启动掉落状态 + 物理)
	 *   - 设置当前武器为 null
	 *
	 * 调用方: ABaseCharacter::Server_DropPrimaryWeapon_Implementation (服务器进程内部)
	 *
	 * @return true=丢弃成功, false=失败
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	bool HandleDropPrimaryWeapon();

	/**
	 * 【v200.2.18 大厂架构 — 重命名】捡起地面上的武器
	 *
	 * 旧版命名问题: 同上 ("Server_TryPickupWeapon" 名字像 RPC 但不是)
	 * 新版: 重命名 HandleTryPickupWeapon
	 *
	 * 职责:
	 *   - 校验: 必须是服务器 + 没有主武器 + 目标武器在掉落状态
	 *   - 调用 WeaponDropComponent->CancelDroppedState (恢复弹药 + 停物理 + Multicast 客户端)
	 *   - 装备武器到主槽位
	 *   - 广播事件
	 *
	 * 调用方: ABaseCharacter::Server_TryPickupWeapon_Implementation (服务器进程内部)
	 *
	 * @param WeaponToPickup 要捡起的武器
	 * @return true=捡起成功, false=失败
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	bool HandleTryPickupWeapon(ABaseWeapon* WeaponToPickup);

	/**
	 * 【v200.2 大厂架构 — 零兜底】获取 Primary 槽位的武器
	 *
	 * 旧版 GetPrimaryWeapon() 返回 CurrentWeapon — 这是错误的!
	 *   CurrentWeapon 是"激活的武器"(任意槽位), 不是"主武器槽位的武器"
	 *   玩家丢弃主武器 → 槽位切到 Melee → CurrentWeapon = BP_Weapon_Knife_C_0 (近战)
	 *   此时 GetPrimaryWeapon() 返回 BP_Weapon_Knife_C_0 → WeaponDropComponent::OnOverlapBegin
	 *   第 380-388 行检查 "已有主武器" → 拒绝捡起
	 *   玩家永远无法捡起地上的主武器
	 *
	 * 修复: GetPrimaryWeapon() 改读 WeaponsInSlot[0] (Primary 槽位真理源)
	 *   - 大厂原则 - 单一真理源: WeaponsInSlot[] 是槽位的唯一权威
	 *   - 丢弃时 WeaponsInSlot[0] = nullptr → 玩家可捡
	 *   - 捡起后 WeaponsInSlot[0] = WeaponToPickup → 槽位被占用
	 *   - 武器切换不影响 WeaponsInSlot[], 只影响 CurrentWeapon/CurrentWeaponSlot
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	ABaseWeapon* GetPrimaryWeapon() const
	{
		return GetWeaponInSlot(EWeaponSlotType::Primary);
	}

protected:
	// ==========================================
	// UE 生命周期
	// ==========================================

	/**
	 * UE 组件生命周期: 组件被注册时调用 (类似 BeginPlay)
	 * 用途: 诊断日志 + 校验 Owner 类型 (一次性, 不缓存 — 见 v40.3 大厂重构)
	 */
	virtual void BeginPlay() override;

	/**
	 * UE 组件生命周期: 组件被注销时调用 (类似 EndPlay)
	 * 用途: 调试日志
	 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ==========================================
	// 内部辅助 — Owner 真理源
	// ==========================================

	/**
	 * 【v40.3 大厂重构】Owner 真理源 — 永远走 GetOwner() (UE 标准 API)
	 *
	 * 反模式 (v6-v40.2): 用 TWeakObjectPtr<ABaseCharacter> OwnerCharacter 缓存
	 *   - BeginPlay 中赋值 → 其他时序调用拿到 null
	 *   - 关卡预放 AI 的 OnPossess 比 Component::BeginPlay 早 → OwnerCharacter 未缓存
	 *   - RequestWeaponSpawn / SpawnAndEquipWeapon / SyncWeaponFromProfile 全部用 OwnerCharacter.Get()
	 *   - 结果: AI Pawn 的武器永远生成失败 (Session1.log line 87 错误)
	 *
	 * 新架构 (v40.3 — 单一真理源):
	 *   - 真理源 = UE 标准 API GetOwner() (Component 挂在 Actor 上, GetOwner 永远非空除非 Actor 销毁)
	 *   - **不缓存**: 缓存失效 = 隐性兜底 = 隐藏 bug
	 *   - 每次显式问 GetOwner() + Cast<ABaseCharacter>, 100% 准确
	 *   - Cast 失败立即 Log Error + return nullptr (零兜底: 失败就是失败, 强制修复 BP 组件挂载)
	 *
	 * 大厂原则:
	 *   - 与 BaseCharacter::ResolveWeaponAttach() 模板同源 (都走 GetOwner/FindComponentByClass)
	 *   - 真理源一致, 错误日志一致, 调用方逻辑统一
	 */
	static ABaseCharacter* GetOwnerCharacterChecked(const UActorComponent* Self);

	// ==========================================
	// 内部辅助函数 (private)
	// ==========================================

	/**
	 * 【v77 大厂架构】检查武器是否已挂载到 Socket
	 *
	 * 原理: UE AttachToComponent 后, Actor 的 RootComponent 会记录挂载的 Socket 名
	 * 如果 GetAttachSocketName() != NAME_None, 说明已挂载
	 *
	 * @param Weapon  待检查武器
	 * @param Owner   持有者角色
	 * @return true = 已挂载 (跳过重复 Attach), false = 未挂载 (需要补挂)
	 */
	bool IsWeaponAttachedToSocket(ABaseWeapon* Weapon, ABaseCharacter* Owner) const;

	/**
	 * 【v200.2.15 / v200.2.16 / v200.2.17 大厂架构】强制重置武器 Mesh 子组件的 RelativeTransform 为 identity.
	 *
	 *   根因: BP_Weapon_*.uasset 的 WeaponSkeletalMesh/WeaponStaticMesh 组件在蓝图里设了非零的
	 *         RelativeLocation/Rotation/Scale, 导致 attach 后武器 WorldTransform 被 Mesh.RelativeTransform
	 *         又叠加一次 → 武器位置/旋转严重错位.
	 *
	 *   大厂原则 (单一真理源):
	 *     - 武器 Mesh 在 socket 下的位置由 DT 的 Actor RelativeLocation/Rotation/Scale 唯一控制
	 *     - Mesh 组件自己的 RelativeTransform 必须强制为 0/identity (不依赖 BP 蓝图配对)
	 *
	 *   【v200.2.17 白名单】跳过 Magazine 子组件 (弹夹)
	 *     - 弹夹是武器的子组件 (命名包含 "Magazine" 如 "MagazineSkeletalMesh")
	 *     - BP 里配了具体的相对武器 Mesh 的位置 (弹夹在枪的左侧/底部)
	 *     - 清零它会导致弹夹不在枪上 → 破坏换弹动画
	 *     - 跳过规则: 与 ABaseWeapon::ResolveMagazineSkeletalMesh 识别规则完全一致 (名字包含 "Magazine")
	 *
	 *   调用时机:
	 *     - Spawn 后 (v200.2.15): SpawnActor 之后立即调用 (防御运行时生成的武器)
	 *     - Attach 前 (v200.2.16): ApplyAttachmentRuntime 入口 (防御关卡预放武器 + 捡起场景)
	 */
	static void ResetWeaponMeshRelativeTransforms(ABaseWeapon* Weapon);

	/**
	 * 【v78 大厂架构】数组索引 → 槽位类型
	 *
	 * 映射: 0→Primary, 1→Secondary, 2→Melee, 其他→None
	 * 在 RefreshWeaponVisibilityForSlotChange 中用于遍历 WeaponsInSlot
	 */
	FORCEINLINE static EWeaponSlotType ArrayIndexToSlotType(int32 Index)
	{
		switch (Index)
		{
		case 0: return EWeaponSlotType::Primary;
		case 1: return EWeaponSlotType::Secondary;
		case 2: return EWeaponSlotType::Melee;
		default: return EWeaponSlotType::None;
		}
	}

protected:
	/**
	 * 当前装备的武器指针 (Replicated)
	 *
	 * 网络同步:
	 *   - 服务器写 CurrentWeapon → 自动复制到客户端
	 *   - 客户端 OnRep_CurrentWeapon 触发 → 反查 WeaponID + 挂载
	 *
	 * 大厂原则 - 单一真理源: 本字段是组件内唯一武器指针
	 * 迁移期间: BaseCharacter 也持有同名字段, 后续 batch 删除
	 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon, VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	ABaseWeapon* CurrentWeapon;

	/**
	 * 当前角色 ID (用于查找挂载配置)
	 *
	 * 来源:
	 *   - 玩家: ARoomPlayerState::GetSelectedCharacterID (PossessedBy 中读)
	 *   - AI: SetSpawnLoadout / SetMeleeConfig 写入
	 *
	 * 【2026.07.12 P0 修复】Replicated: 客户端 OnRep_CurrentWeapon 反查挂载配置依赖此字段
	 *
	 * 网络同步: Replicated (自动同步到所有客户端)
	 */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Combat|Attachment")
	FString CharacterID;

	/**
	 * Spawn 时刻预定的武器 ID
	 *
	 * 设计 (大厂原则):
	 *   - AI 跟玩家走完全相同的武器 Spawn 链路 (SpawnAndEquipWeapon)
	 *   - 但 AI 没有 PlayerState, 不能像玩家那样从 PS 读 SelectionWeapon1
	 *   - 服务器 SpawnAIInternal 后 (Possess 之前) 写入: SpawnWeaponID = Request.WeaponID (来自 UI ComboBox)
	 *   - 关卡预放 AI: AMeleeAIController::SetupMeleeAI → SetMeleeConfig → 写入 ConfigSO.DefaultWeaponRowName
	 *   - 玩家路径不碰这字段, 互不干扰
	 *
	 * 【v54 大厂架构重构 — UAIProfileAsset 已删除】
	 *   - 大厅入队 AI: SpawnWeaponID 真理源 = Request.WeaponID (UI)
	 *   - 关卡预放 AI: SpawnWeaponID 真理源 = ConfigSO.DefaultWeaponRowName (DA 配的默认值)
	 *   - 两者写入路径完全独立, 不允许互相兜底
	 *
	 * 【2026.07.12 P0 修复】Replicated: 客户端 OnRep_CurrentWeapon 反查 WeaponID 依赖此字段
	 *
	 * 网络同步: Replicated (自动同步到所有客户端)
	 */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Combat|Attachment")
	FString SpawnWeaponID;

	/**
	 * 【v52 大厂架构 — 生化模式】3 槽位武器数组 (Replicated)
	 *
	 * 数据结构 (与 EWeaponSlotType 严格对齐):
	 *   - 索引 0 = EWeaponSlotType::Primary   (主武器, 玩家大厅 SelectedWeaponID1)
	 *   - 索引 1 = EWeaponSlotType::Secondary (副武器, 玩家大厅 SelectedWeaponID2, 允许为空)
	 *   - 索引 2 = EWeaponSlotType::Melee     (近战武器, 玩家大厅 SelectedWeaponID3, 允许为空)
	 *
	 * 槽位真理源 (大厂原则):
	 *   - 服务器 SpawnBothWeapons → 写入 WeaponsInSlot
	 *   - 服务器 Server_SwitchToWeaponSlot → 改 CurrentWeaponSlot + 切换显示
	 *   - 客户端 OnRep_WeaponsInSlot → 自动收到双武器引用 (UE 自动同步数组)
	 *
	 * 零兜底:
	 *   - 数组空 → 玩家没武器 (走 GameMode 重新 Spawn, 不静默兜底)
	 *   - 3 武器某槽位缺失 → 拒绝切换 (强制修复 Spawn 链路)
	 *
	 * 向后兼容:
	 *   - AI 路径 (关卡预放 AI / 大厅入队 AI) 不调用 SpawnAllWeapons
	 *   - AI 走旧 SpawnAndEquipWeapon → CurrentWeapon 字段, 与本数组不冲突
	 *   - 3 槽位系统仅玩家路径使用 (生化模式核心需求)
	 */
	UPROPERTY(Replicated)
	TArray<TObjectPtr<ABaseWeapon>> WeaponsInSlot;

	/**
	 * 【v78 大厂架构 — 简化版】武器状态真理源
	 *
	 * 替代旧 CurrentWeapon + CurrentWeaponSlot 两个独立 UPROPERTY(Replicated)
	 *
	 * 复制策略:
	 *   - CurrentWeaponSlot 通过 ReplicatedUsing = OnRep_CurrentWeaponSlot 触发客户端回调
	 *   - WeaponState 通过普通 DOREPLIFETIME 复制（同步 ActiveWeapon + ActiveSlot）
	 *   - OnRep_CurrentWeaponSlot 读取 WeaponState 并执行所有显示逻辑
	 *
	 * 向后兼容: 旧字段保留，OnRep_CurrentWeaponSlot 同步后回写
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Combat|Weapons|State")
	FWeaponState WeaponState;

	/**
	 * 【v51 大厂架构 — 生化模式】当前激活的槽位 (ReplicatedUsing)
	 *
	 * 默认值: EWeaponSlotType::Primary (玩家开局持有主武器)
	 *
	 * 同步链路:
	 *   - 服务器 Server_SwitchToWeaponSlot 写入 WeaponState + CurrentWeaponSlot
	 *   - CurrentWeaponSlot 通过 ReplicatedUsing → 客户端 OnRep_CurrentWeaponSlot 触发
	 *   - OnRep_CurrentWeaponSlot 读取 WeaponState.ActiveWeapon + ActiveSlot 并显示/隐藏
	 *
	 * 零兜底:
	 *   - 服务器切到空槽位 → 拒绝切 (Log Error)
	 *   - 客户端收到 None 槽位 → 拒绝切换 (强制修复服务器状态)
	 *
	 * 【v78 简化】CurrentWeaponSlot 是 OnRep 触发器，真理源在 WeaponState
	 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeaponSlot, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Weapons|Slot")
	EWeaponSlotType CurrentWeaponSlot = EWeaponSlotType::Primary;

	/**
	 * 【v219 大厂架构 — Per-Slot Runtime】武器挂载运行时数据 (3 个独立字段)
	 *
	 * 修复 (v220 大厂架构 — UE 5.6 Replicated Maps 不支持):
	 *   - 旧: UPROPERTY(Replicated) TMap<EWeaponSlotType, FWeaponAttachmentRuntime> → UE 5.6 编译失败
	 *   - 新: 3 个独立 FWeaponAttachmentRuntime 字段 (RuntimePrimary / RuntimeSecondary / RuntimeMelee)
	 *   - 每个槽位独立 Replicated, 服务器写入路径与 v219 一致
	 *   - 客户端 ApplyAttachmentRuntime 按 CurrentWeaponSlot 选对应字段
	 *
	 * 复制策略:
	 *   - 3 个 UPROPERTY(Replicated) 字段, 服务器 SpawnAndConfigureWeaponInSlot 末尾**无条件**写对应 Slot 字段
	 *   - 客户端 OnRep_CurrentWeapon / OnRep_CurrentWeaponSlot 读 RuntimeBySlot(CurrentSlot) 强制应用偏移
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Combat|Weapons|Attachment")
	FWeaponAttachmentRuntime RuntimePrimary;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Combat|Weapons|Attachment")
	FWeaponAttachmentRuntime RuntimeSecondary;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Combat|Weapons|Attachment")
	FWeaponAttachmentRuntime RuntimeMelee;

	/**
	 * 【v78 大厂架构】RefreshWeaponVisibilityForSlotChange — 槽位切换刷新可见性
	 *
	 * 职责: 根据新激活的槽位，隐藏旧槽位武器、显示新槽位武器
	 */
	void RefreshWeaponVisibilityForSlotChange(EWeaponSlotType NewActiveSlot);

	/**
	 * 【v219 大厂架构 — 客户端武器姿态修复】应用挂载运行时数据到武器 Actor
	 *
	 * 职责: 把 RuntimeBySlot[CurrentWeaponSlot] 字段 (SocketName + RelativeLocation/Rotation/Scale)
	 *       实际应用到武器 Actor 上 (Attach + SetActorRelative*).
	 *
	 * 调用方 (3 个, 都是 OnRep 入口):
	 *   - OnRep_CurrentWeapon 末尾 (远端玩家新武器复制过来时)
	 *   - OnRep_CurrentWeaponSlot 末尾 (远端玩家槽位切换时)
	 *   - 服务器 SpawnAndEquipWeapon 末尾 (服务器权威应用)
	 *
	 * 大厂原则 - 零兜底:
	 *   - Weapon 为空 → Log Error + return (强制修复 Spawn 链路)
	 *   - RuntimeData.bIsValid=false → Log Error + return (服务器没写入偏移, 强制修复服务器逻辑)
	 *   - 不允许"已挂载就跳过" (v77 反模式已删除)
	 *
	 * @param Weapon 要应用偏移的武器 Actor (通常 = CurrentWeapon)
	 */
	void ApplyAttachmentRuntime(ABaseWeapon* Weapon);

	/**
	 * 【v220 大厂架构 — Per-Slot runtime 访问器】按 Slot 返回对应 Runtime 字段指针
	 *
	 * 业务背景 (UE 5.6 Replicated Maps 不支持):
	 *   - 旧: TMap<EWeaponSlotType, FWeaponAttachmentRuntime> RuntimeBySlot → 编译失败
	 *   - 新: 3 个独立 UPROPERTY(Replicated) 字段 (RuntimePrimary/RuntimeSecondary/RuntimeMelee)
	 *   - 访问器封装备选, 调用方统一用 GetRuntimeBySlot(Slot) 拿指针
	 *
	 * 设计原则:
	 *   - 输入 Slot 无效 → 返回 nullptr, 调用方判空
	 *   - 不做隐式默认值, 零兜底
	 *
	 * @param Slot 槽位类型
	 * @return Runtime 字段指针 (无效 Slot 返回 nullptr)
	 */
	FWeaponAttachmentRuntime* GetRuntimeBySlot(EWeaponSlotType Slot);
	const FWeaponAttachmentRuntime* GetRuntimeBySlot(EWeaponSlotType Slot) const;

	/**
	 * 【v51 大厂架构】OnRep_CurrentWeaponSlot — 客户端槽位同步回调
	 *
	 * 【v78 简化】真理源移至 WeaponState，OnRep 读取 WeaponState.ActiveSlot + ActiveWeapon
	 */
	UFUNCTION()
	void OnRep_CurrentWeaponSlot(EWeaponSlotType OldSlot);

	/**
	 * 武器挂载配置数据表 — 【v37 单一真理源】从 GM 读取, 不在本组件持有
	 *
	 * 历史 (v32-v36) 反模式:
	 *   - 本组件持有字段, BP_BaseCharacter / BP_SWAT_C / BP_GruntAI 各自配一次
	 *   - 配置反模式: 3 个 BP 持有同一资产, 改一处忘一处 (用户实际踩坑)
	 *   - 字段值是 nullptr → 报错"必须在 BP_BaseCharacter 配置", 用户不知道要配
	 *
	 * 新架构 (v37 — 单一真理源):
	 *   - 真理源 = ARoomGameMode::WeaponAttachmentDataTable 字段
	 *   - 本组件不持有字段, 运行时通过 GetWeaponAttachmentDataTable() 从 GM 拿
	 *   - BP_GM_RoomGameMode 配一次, 所有 BP 角色共享
	 *   - 拿不到 / 为空 → Log Error + 强制修复 GM (零兜底)
	 *
	 * 行结构: FWeaponAttachmentConfig
	 */
};