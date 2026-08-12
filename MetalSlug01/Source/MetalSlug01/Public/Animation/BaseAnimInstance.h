// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"

// 引入 UE 原生 UAnimInstance 类（基类）
#include "Animation/AnimInstance.h"

// UE 自动生成的头文件
#include "BaseAnimInstance.generated.h"

// 前置声明
class ABaseCharacter;

/**
 * @brief 武器身体姿势枚举 (UE 5 大厂主流做法)
 *
 * 用于驱动 AnimGraph 中的 Blend Poses by Enum 节点, 实现
 * 不同武器类别 → 不同身体姿势 (Locomotion BlendSpace) 的自动切换.
 *
 * 真理源 (Single Source of Truth):
 *   - 从 ABaseWeapon::MeshType (EWeaponMeshType) 派生
 *   - Melee → EWeaponStance::Melee (刀姿势)
 *   - Primary/Secondary → EWeaponStance::Rifle (枪姿势)
 *   - None / 无武器 → EWeaponStance::Unarmed (徒手姿势, 可选)
 *
 * 大厂原则 (UE 5 Blend Poses by Enum):
 *   - 加新姿势只需扩展枚举 + 在 AnimGraph 多接一个 Pin
 *   - 不改 C++ 状态机, 不改 Locomotion 状态机结构
 *   - 平滑混合 (UE 内置 Linear Interpolation), 切换自然
 */
UENUM(BlueprintType)
enum class EWeaponStance : uint8
{
	Unarmed    UMETA(DisplayName = "徒手 (没拿武器)"),
	Rifle      UMETA(DisplayName = "枪械姿势 (Primary/Secondary)"),
	Melee      UMETA(DisplayName = "近战姿势 (Melee)")
};

/**
 * @class UBaseAnimInstance
 * @brief 项目所有角色 AnimBP 的 C++ 基类
 *
 * 职责说明:
 * - 缓存 ABaseCharacter 引用（性能优化，避免每帧 TryGetPawnOwner）
 * - 提供给动画蓝图（AnimBP）的"终极数据"输出属性（Fast Path 优化）
 * - 每帧计算速度/方向/IK/瞄准偏移等
 *
 * 架构理念:
 * 1. 电竞级步法: 剥离物理与动画（按输入直接播满速动画）
 * 2. 动态锁步: 武器挥砍时强制 AnimSpeed=0
 * 3. 左手 IK: 自动搜寻武器上的 LeftHandSocket 插槽并吸附
 * 4. 瞄准偏移: 计算摄像机与身体的 Pitch 差，用于动画弯腰
 */
UCLASS()
class METALSLUG01_API UBaseAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/**
	 * 相当于蓝图里的 Event Blueprint Initialize Animation（只运行一次）
	 * 目的: 缓存 ABaseCharacter 引用
	 */
	virtual void NativeInitializeAnimation() override;

	/**
	 * 相当于蓝图里的 Event Blueprint Update Animation（每帧运行）
	 * 目的: 每帧计算速度/方向/IK/瞄准偏移
	 */
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	/**
	 * 缓存角色的引用
	 * 性能优化: 避免每帧都 TryGetPawnOwner
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Character")
	ABaseCharacter* Character;

	// ==========================================
	// 给蓝图输出的终极数据: 直接带有黄色闪电 (Fast Path) 优化
	// ==========================================

	/**
	 * 角色的移动速度（实际物理速度）
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Movement")
	float Speed;

	/**
	 * 专门喂给混合空间的"电竞级"速度
	 * 锁步时强制为 0，按下方向键时强制为满速
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Movement")
	float AnimSpeed;

	/**
	 * 角色移动的方向角度 (-180 到 180)
	 * 正前方=0，正右方=90，正左方=-90，正后方=180/-180
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Movement")
	float Direction;

	/**
	 * 角色是否在空中？（跳跃或掉落）
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Movement")
	bool bIsFalling;

	/**
	 * Z轴速度
	 * 大于0说明在往上跳，小于0说明在往下掉
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Movement")
	float VelocityZ;

	/**
	 * 磁铁在世界中的精确坐标（左手IK目标）
	 * 从武器根组件的 "LeftHandSocket" 插槽获取
	 */
	UPROPERTY(BlueprintReadOnly, Category = "IK")
	FTransform LeftHandIKTransform;

	/**
	 * 磁铁的吸附开关
	 * 1=吸住，0=松开（防止没拿刀时左手抽筋）
	 */
	UPROPERTY(BlueprintReadOnly, Category = "IK")
	float LeftHandIKAlpha;

	/**
	 * 角色当前是否处于下蹲状态？
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Movement")
	bool bIsCrouching;

	// ==========================================
	// 瞄准偏移系统
	// ==========================================

	/**
	 * 上下看的角度 (Pitch)
	 * 摄像机与身体的 Pitch 差（除以2，让弯腰更自然）
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	float AimPitch;

	// ==========================================
	// 武器身体姿势 (v59 — AnimGraph Blend Poses by Enum 驱动)
	// ==========================================

	/**
	 * 当前武器对应的身体姿势
	 *
	 * AnimGraph 里用 Blend Poses by Enum 节点, 把这个变量接进去,
	 * UE 会自动按枚举值挑 Pin (Unarmed / Rifle / Melee) — 大厂主流做法.
	 *
	 * 计算逻辑 (NativeUpdateAnimation):
	 *   - Character->GetCurrentWeapon() == nullptr → Unarmed
	 *   - Weapon->MeshType == Melee → Melee
	 *   - Weapon->MeshType == Primary / Secondary → Rifle
	 *   - 其他 → Unarmed
	 *
	 * 网络同步:
	 *   - BaseWeapon 本身已 Replicated (bReplicates = true)
	 *   - MeshType 是普通 UPROPERTY (随 Actor 复制)
	 *   - 所以客户端 AnimGraph 自动拿到正确的 stance, 无需额外同步代码
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Movement")
	EWeaponStance CurrentWeaponStance = EWeaponStance::Unarmed;
};
