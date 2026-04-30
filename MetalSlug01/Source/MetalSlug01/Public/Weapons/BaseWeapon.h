#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UI/Login/Data/StaticTable.h"
#include "BaseWeapon.generated.h"

UCLASS()
class METALSLUG01_API ABaseWeapon : public AActor
{
	GENERATED_BODY()
	
public:	
	// 构造函数：设置默认值
	ABaseWeapon();
	
	// 每帧更新 (必须重写Tick)
	virtual void Tick(float DeltaTime) override;
	
	// 开刃与收刃接口
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void StartWeaponTrace(bool bIsHeavyAttack);

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void StopWeaponTrace();

	// 武器基础伤害
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float BaseDamage = 20.0f;
	
	// 轻击时允许移动吗？(比如：短剑=true，大锤=false)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Config")
	bool bCanMoveWhileLightAttack = true;

	// 重击时允许移动吗？
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Config")
	bool bCanMoveWhileHeavyAttack = false;
	
	// 下蹲时允许攻击吗？(统管轻重击)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Config")
	bool bCanAttackWhileCrouched = true;
	
protected:
	// 标记武器当前是否具有杀伤力
	bool bIsWeaponActive = false;
	
	//记录当前这刀是轻击还是重击
	bool bIsCurrentAttackHeavy = false;

	// 记录这一刀已经砍中过的人，防止一刀对同一个人造成多次伤害
	UPROPERTY()
	TArray<AActor*> IgnoreActors;

	// 记录上一帧刀刃的起点和终点
	FVector LastFrameStartLoc;
	FVector LastFrameEndLoc;

public:
	// 获取最后造成的击杀方式（供 HUD 显示用）
	UFUNCTION(BlueprintCallable, Category = "Combat")
	EKillMethod GetLastKillMethod() const { return LastKillMethod; }

	// 报告命中（Server RPC）：客户端检测到命中后调用，服务器执行伤害
	UFUNCTION(Server, Reliable)
	void Server_ReportHit(AActor* HitActor, float Damage, FVector HitLocation, FVector HitNormal, FName BoneName, bool bIsHeavy);

protected:
	// 最后造成的击杀方式（由 Tick 中判定并存储）
	UPROPERTY()
	EKillMethod LastKillMethod = EKillMethod::MeleeWeapon;
	
	
	// ==========================================
	// 1. 武器核心组件
	// ==========================================
protected:
	// 武器的静态模型（比如一把小刀、斧头、尼泊尔）
	// 注意：如果你需要武器有变形动画，可以换成 USkeletalMeshComponent
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	class UStaticMeshComponent* WeaponMesh;

	// ==========================================
	// 2. 武器核心战斗数值（蓝图里可以针对不同武器随便改）
	// ==========================================
public:
	// 轻击打身体的伤害
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Stats")
	float LightDamageBody; 
	
	// 轻击爆头伤害
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Stats")
	float LightDamageHead; 

	// 重击伤害 (一击毙命)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Stats")
	float HeavyDamage; 

	// 攻击判定距离（例如尼泊尔比小刀长）
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Stats")
	float AttackRange; 

	// 攻击判定范围的粗细（比如铁锹的横扫范围比小刀大）
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Stats")
	float AttackRadius; 

	// ==========================================
	// 3. 武器专属动画库（角色挥刀时，会找武器借这些动画播）
	// ==========================================
protected:
	// 【核心升级】：轻击连斩动画数组！你可以往里面塞 左划、右划、下劈 3个不同的动画
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Animations")
	TArray<class UAnimMontage*> LightAttackMontages;

	// 重击通常只有一个动画
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Animations")
	class UAnimMontage* HeavyAttackMontage;

	// ==========================================
	// 4. 核心功能接口（供拿着它的 Character 调用）
	// ==========================================
public:
	// 根据角色的连击进度（ComboIndex），返回对应的那一段挥刀动画
	UAnimMontage* GetAttackMontage(bool bIsHeavy, int32 ComboIndex = 0) const;
	
};