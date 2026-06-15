// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"

// 引入 UE 原生 AActor 类（基类）
#include "GameFramework/Actor.h"

// 引入 EKillMethod 击杀方式枚举
#include "Data/Enums/CombatEnums.h"

// UE 自动生成的头文件
#include "BaseWeapon.generated.h"

/**
 * @class ABaseWeapon
 * @brief 项目所有武器的 C++ 基类
 *
 * 职责说明:
 * - 武器静态模型（StaticMesh）
 * - 刀刃扫掠判定（每帧 SphereTrace 检测命中）
 * - 伤害计算（轻击/重击/爆头）
 * - 武器专属动画库（轻击连斩 + 重击）
 * - 网络同步（Server RPC 报告命中）
 *
 * 架构理念:
 * 1. 数据驱动：每把武器在蓝图中配置自己的伤害/范围/动画
 * 2. 服务器权威：所有伤害判定走 Server RPC
 * 3. 一刀多伤防护：使用 IgnoreActors 列表防止重复伤害
 * 4. 轻击连斩：每把武器可配置多个连击动画（左划/右划/下劈）
 */
UCLASS()
class METALSLUG01_API ABaseWeapon : public AActor
{
	GENERATED_BODY()

public:
	// ==========================================
	// 构造函数与生命周期
	// ==========================================

	/**
	 * 构造函数: 设置默认值
	 * 目的: 创建武器组件、初始化属性
	 */
	ABaseWeapon();

	/**
	 * UE 原生生命周期: 每帧调用
	 * 用途: 武器激活时进行刀刃扫掠判定
	 */
	virtual void Tick(float DeltaTime) override;


	// ==========================================
	// 武器激活接口
	// ==========================================

	/**
	 * 开启武器刀刃伤害判定
	 * @param bIsHeavyAttack 是否为重击（决定伤害数值和动画）
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void StartWeaponTrace(bool bIsHeavyAttack);

	/**
	 * 关闭武器刀刃伤害判定
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void StopWeaponTrace();


	// ==========================================
	// 基础属性
	// ==========================================

	/**
	 * 武器基础伤害（兜底用）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float BaseDamage = 20.0f;

	/**
	 * 轻击时允许移动吗？
	 * 例: 短剑 = true（轻击快）, 大锤 = false（重击慢）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Config")
	bool bCanMoveWhileLightAttack = true;

	/**
	 * 重击时允许移动吗？
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Config")
	bool bCanMoveWhileHeavyAttack = false;

	/**
	 * 下蹲时允许攻击吗？(统管轻重击)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Config")
	bool bCanAttackWhileCrouched = true;

protected:
	/**
	 * 内部标记: 武器当前是否激活（具有杀伤力）
	 */
	bool bIsWeaponActive = false;

	/**
	 * 内部标记: 当前这刀是轻击还是重击
	 */
	bool bIsCurrentAttackHeavy = false;

	/**
	 * 记录这一刀已经砍中过的人，防止一刀对同一个人造成多次伤害
	 */
	UPROPERTY()
	TArray<AActor*> IgnoreActors;

	/**
	 * 记录上一帧刀刃的起点和终点（用于刀刃扫掠追踪）
	 */
	FVector LastFrameStartLoc;
	FVector LastFrameEndLoc;

public:
	/**
	 * 获取最后造成的击杀方式（供 HUD 显示用）
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	EKillMethod GetLastKillMethod() const { return LastKillMethod; }

	/**
	 * 报告命中（Server RPC）
	 * 流程: 客户端检测到命中后调用，服务器执行伤害
	 */
	UFUNCTION(Server, Reliable)
	void Server_ReportHit(AActor* HitActor, float Damage, FVector HitLocation, FVector HitNormal, FName BoneName, bool bIsHeavy);

protected:
	/**
	 * 最后造成的击杀方式（由 Tick 中判定并存储）
	 * 供 HUD 调用显示击杀图标
	 */
	UPROPERTY()
	EKillMethod LastKillMethod = EKillMethod::MeleeWeapon;


	// ==========================================
	// 1. 武器核心组件
	// ==========================================
protected:
	/**
	 * 武器的静态模型（比如一把小刀、斧头、尼泊尔）
	 * 注意: 如果需要武器有变形动画，可以换成 USkeletalMeshComponent
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	class UStaticMeshComponent* WeaponMesh;


	// ==========================================
	// 2. 武器核心战斗数值（蓝图里可以针对不同武器随便改）
	// ==========================================
public:
	/**
	 * 轻击打身体的伤害
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Stats")
	float LightDamageBody;

	/**
	 * 轻击爆头伤害
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Stats")
	float LightDamageHead;

	/**
	 * 重击伤害 (一击毙命)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Stats")
	float HeavyDamage;

	/**
	 * 攻击判定距离（例如尼泊尔比小刀长）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Stats")
	float AttackRange;

	/**
	 * 攻击判定范围的粗细（比如铁锹的横扫范围比小刀大）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Stats")
	float AttackRadius;


	// ==========================================
	// 3. 武器专属动画库（角色挥刀时，会找武器借这些动画播）
	// ==========================================
protected:
	/**
	 * 【核心升级】: 轻击连斩动画数组！
	 * 你可以往里面塞 左划、右划、下劈 3个不同的动画
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Animations")
	TArray<class UAnimMontage*> LightAttackMontages;

	/**
	 * 重击通常只有一个动画
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Animations")
	class UAnimMontage* HeavyAttackMontage;


	// ==========================================
	// 4. 核心功能接口（供拿着它的 Character 调用）
	// ==========================================
public:
	/**
	 * 根据角色的连击进度（ComboIndex），返回对应的那一段挥刀动画
	 * @param bIsHeavy 是否为重击
	 * @param ComboIndex 连击段数（轻击使用）
	 * @return 对应的 AnimMontage
	 */
	UAnimMontage* GetAttackMontage(bool bIsHeavy, int32 ComboIndex = 0) const;
};
