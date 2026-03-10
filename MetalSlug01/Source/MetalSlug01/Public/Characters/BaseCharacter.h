#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "BaseCharacter.generated.h"

UCLASS()
class METALSLUG01_API ABaseCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// 构造函数
	ABaseCharacter();

protected:
	// 游戏开始时调用
	virtual void BeginPlay() override;

public:	
	// 每帧调用
	virtual void Tick(float DeltaTime) override;

	// 初始化组件
	virtual void PostInitializeComponents() override;

	// 输入绑定
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// 移动输入处理
	void MoveForward(float Value);
	void MoveRight(float Value);

	// 跳跃输入处理
	void JumpInput();
	void StopJumping();

	// 基础移动能力
	UFUNCTION(BlueprintCallable, Category = "Movement")
	virtual void BasicMove(const FVector& Direction);

	// 预留复杂移动接口 - 爬墙功能
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Advanced Movement")
	void WallClimb();
	virtual void WallClimb_Implementation();

	// 预留复杂移动接口 - 滑铲功能
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Advanced Movement")
	void Slide();
	virtual void Slide_Implementation();

	// 预留复杂移动接口 - 潜水功能
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Advanced Movement")
	void Dive();
	virtual void Dive_Implementation();

	// 获取移动速度
	UFUNCTION(BlueprintPure, Category = "Movement")
	FORCEINLINE float GetMovementSpeed() const { return MovementSpeed; }

	// 设置移动速度
	UFUNCTION(BlueprintCallable, Category = "Movement")
	virtual void SetMovementSpeed(float NewSpeed);

	// 获取最大生命值
	UFUNCTION(BlueprintPure, Category = "Health")
	FORCEINLINE float GetMaxHealth() const { return MaxHealth; }

	// 获取当前生命值
	UFUNCTION(BlueprintPure, Category = "Health")
	FORCEINLINE float GetCurrentHealth() const { return CurrentHealth; }

	// 设置当前生命值
	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetCurrentHealth(float NewHealth);

	// 受伤函数
	UFUNCTION(BlueprintCallable, Category = "Combat")
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;

	// 死亡函数
	UFUNCTION(BlueprintCallable, Category = "Combat")
	virtual void Die();

	// 是否存活
	UFUNCTION(BlueprintPure, Category = "Combat")
	FORCEINLINE bool IsAlive() const { return bIsAlive; }

	// 获取角色等级
	//UFUNCTION(BlueprintPure, Category = "Character Stats")
	FORCEINLINE int32 GetLevel() const { return Level; }

	// 提升角色等级
	UFUNCTION(BlueprintCallable, Category = "Character Stats")
	void LevelUp();

	// 获取当前动画状态
	UFUNCTION(BlueprintPure, Category = "Animation")
	FORCEINLINE ECharacterAnimationState GetAnimationState() const { return AnimationState; }

	// 设置动画状态 - 用于蓝图中切换动画
	UFUNCTION(BlueprintCallable, Category = "Animation")
	void SetAnimationState(ECharacterAnimationState NewState);

	// 获取角色类型
	UFUNCTION(BlueprintPure, Category = "Character Info")
	FORCEINLINE ECharacterType GetCharacterType() const { return CharacterType; }

	// 设置角色类型
	UFUNCTION(BlueprintCallable, Category = "Character Info")
	void SetCharacterType(ECharacterType NewType);

	// 获取移动状态
	UFUNCTION(BlueprintPure, Category = "Movement")
	FORCEINLINE EMovementState GetMovementState() const { return MovementState; }

	// 更新移动状态
	UFUNCTION(BlueprintCallable, Category = "Movement")
	void UpdateMovementState();

	// 获取当前移动速度
	UFUNCTION(BlueprintPure, Category = "Movement")
	FORCEINLINE float GetCurrentMovementSpeed() const { return CurrentMovementSpeed; }

	// 检查是否在地面
	UFUNCTION(BlueprintPure, Category = "Movement")
	FORCEINLINE bool IsOnGround() const { return GetCharacterMovement()->IsMovingOnGround(); }

	// 检查是否在空中
	UFUNCTION(BlueprintPure, Category = "Movement")
	FORCEINLINE bool IsInAir() const { return !GetCharacterMovement()->IsMovingOnGround(); }

	// 检查是否在水中
	UFUNCTION(BlueprintPure, Category = "Movement")
	FORCEINLINE bool IsInWater() const { return GetCharacterMovement()->IsSwimming(); }

	// 检查是否在爬墙
	UFUNCTION(BlueprintPure, Category = "Movement")
	FORCEINLINE bool IsClimbing() const { return GetCharacterMovement()->GetCurrentAcceleration().Size() > 0 && 
										  GetCharacterMovement()->IsCrouching(); }

	// 检查是否在滑行
	UFUNCTION(BlueprintPure, Category = "Movement")
	FORCEINLINE bool IsSliding() const { return GetCharacterMovement()->Velocity.Size() > MovementSpeed * 1.5f && 
										 GetCharacterMovement()->IsCrouching(); }

	// 获取角色ID
	UFUNCTION(BlueprintPure, Category = "Character Control")
	FORCEINLINE EControlledCharacterID GetCharacterID() const { return CharacterID; }

	// 设置角色ID
	UFUNCTION(BlueprintCallable, Category = "Character Control")
	void SetCharacterID(EControlledCharacterID NewID);

	// 获取玩家索引
	UFUNCTION(BlueprintPure, Category = "Player Control")
	FORCEINLINE EPlayerIndex GetPlayerIndex() const { return PlayerIndex; }

	// 设置玩家索引
	UFUNCTION(BlueprintCallable, Category = "Player Control")
	void SetPlayerIndex(EPlayerIndex NewPlayerIndex);

	// 检查是否为指定玩家控制的角色
	UFUNCTION(BlueprintPure, Category = "Player Control")
	FORCEINLINE bool IsControlledByPlayer(EPlayerIndex CheckPlayerIndex) const { return PlayerIndex == CheckPlayerIndex; }

	// 获取玩家ID（从控制器获取）
	UFUNCTION(BlueprintPure, Category = "Player Control")
	int32 GetPlayerID() const;

	// 切换到下一个角色
	UFUNCTION(BlueprintCallable, Category = "Character Control")
	void SwitchToNextCharacter();

	// 切换到上一个角色
	UFUNCTION(BlueprintCallable, Category = "Character Control")
	void SwitchToPreviousCharacter();

	// 检查是否为当前控制的角色
	UFUNCTION(BlueprintPure, Category = "Character Control")
	FORCEINLINE bool IsCurrentControlled() const { return bIsCurrentControlled; }

	// 设置为当前控制的角色
	UFUNCTION(BlueprintCallable, Category = "Character Control")
	void SetAsCurrentControlled(bool bNewValue);

	// 检查是否为本地玩家控制的角色
	//UFUNCTION(BlueprintPure, Category = "Player Control")
	FORCEINLINE bool IsLocallyControlled() const { return Controller && Controller->IsLocalPlayerController(); }

	// 检查是否为远程玩家控制的角色
	UFUNCTION(BlueprintPure, Category = "Player Control")
	FORCEINLINE bool IsRemotelyControlled() const { return Controller && !Controller->IsLocalPlayerController(); }

	// 获取相机偏移
	UFUNCTION(BlueprintPure, Category = "Camera")
	FVector GetCameraOffset() const;

protected:
	// 移动速度变量
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = 0.0f))
	float MovementSpeed;

	// 当前移动速度（实际使用的速度）
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	float CurrentMovementSpeed;

	// 最大生命值
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health", meta = (ClampMin = 0.0f))
	float MaxHealth;

	// 当前生命值
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Health")
	float CurrentHealth;

	// 角色等级
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Stats", meta = (ClampMin = 1))
	int32 Level;

	// 是否存活
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	bool bIsAlive;

	// 当前动画状态 - 用于蓝图中切换动画
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (AllowPrivateAccess = "true"))
	ECharacterAnimationState AnimationState;

	// 角色类型
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Info", meta = (AllowPrivateAccess = "true"))
	ECharacterType CharacterType;

	// 角色ID - 用于区分可控制的角色（角色1、2、3）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Control", meta = (AllowPrivateAccess = "true"))
	EControlledCharacterID CharacterID;

	// 玩家索引 - 用于分屏游戏中区分不同玩家
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Control", meta = (AllowPrivateAccess = "true"))
	EPlayerIndex PlayerIndex;

	// 移动状态
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement", meta = (AllowPrivateAccess = "true"))
	EMovementState MovementState;

	// 是否为当前控制的角色
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Character Control")
	bool bIsCurrentControlled;

	// 移动组件引用
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	class UCharacterMovementComponent* CharacterMovementComp;

	// 相机组件引用
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	class USpringArmComponent* SpringArmComp;

	// 相机跟随组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	class UCameraComponent* CameraComp;

	// 角色网格体组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh")
	class USkeletalMeshComponent* CharacterMesh;

	// 更新动画状态的函数
	UFUNCTION(BlueprintImplementableEvent, Category = "Animation")
	void OnAnimationStateChanged(ECharacterAnimationState NewState);

	// 更新移动状态的函数
	UFUNCTION(BlueprintImplementableEvent, Category = "Movement")
	void OnMovementStateChanged(EMovementState NewState);

	// 角色切换的函数
	UFUNCTION(BlueprintImplementableEvent, Category = "Character Control")
	void OnCharacterSwitched(bool bNowControlled);

	// 玩家控制状态变化的函数
	UFUNCTION(BlueprintImplementableEvent, Category = "Player Control")
	void OnPlayerControlChanged();

	// 获取下一个角色ID
	UFUNCTION(BlueprintPure, Category = "Character Control")
	EControlledCharacterID GetNextCharacterID() const;

	// 获取上一个角色ID
	UFUNCTION(BlueprintPure, Category = "Character Control")
	EControlledCharacterID GetPreviousCharacterID() const;

	// 获取相机偏移位置
	UFUNCTION(BlueprintPure, Category = "Camera")
	FVector GetDesiredCameraOffset() const;
};