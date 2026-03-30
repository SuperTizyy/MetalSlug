#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "BaseAnimInstance.generated.h"

class ABaseCharacter;

UCLASS()
class METALSLUG01_API UBaseAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// 相当于蓝图里的 Event Blueprint Initialize Animation (只运行一次)
	virtual void NativeInitializeAnimation() override;

	// 相当于蓝图里的 Event Blueprint Update Animation (每帧运行)
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	// 缓存角色的引用
	UPROPERTY(BlueprintReadOnly, Category = "Character")
	ABaseCharacter* Character;

	// ==========================================
	// 给蓝图输出的终极数据：直接带有黄色闪电 (Fast Path) 优化！
	// ==========================================
	
	// 角色的移动速度
	UPROPERTY(BlueprintReadOnly, Category = "Movement")
	float Speed;
	
	// 专门喂给混合空间的“电竞级”速度
	UPROPERTY(BlueprintReadOnly, Category = "Movement")
	float AnimSpeed;
	
	// 角色移动的方向角度 (-180 到 180)
	UPROPERTY(BlueprintReadOnly, Category = "Movement")
	float Direction;
	
	// 角色是否在空中？(跳跃或掉落)
	UPROPERTY(BlueprintReadOnly, Category = "Movement")
	bool bIsFalling;
	
	// Z轴速度 (大于0说明在往上跳，小于0说明在往下掉)
	UPROPERTY(BlueprintReadOnly, Category = "Movement")
	float VelocityZ;

	// 磁铁在世界中的精确坐标
	UPROPERTY(BlueprintReadOnly, Category = "IK")
	FTransform LeftHandIKTransform;

	// 磁铁的吸附开关 (1=吸住，0=松开，防止没拿刀时左手抽筋)
	UPROPERTY(BlueprintReadOnly, Category = "IK")
	float LeftHandIKAlpha;
	
	// 角色当前是否处于下蹲状态？
	UPROPERTY(BlueprintReadOnly, Category = "Movement")
	bool bIsCrouching;
	
	// ==========================================
	// 瞄准偏移系统
	// ==========================================
	
	// 上下看的角度 (Pitch)
	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	float AimPitch;
	
	// //左右扭腰的补偿角度 (Yaw)，用于修复越肩视差
	// UPROPERTY(BlueprintReadOnly, Category = "Aim")
	// float AimYaw;
};