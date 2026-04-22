#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h" // 增强输入系统所需
#include "UI/Login/Data/StaticTable.h"
#include "BaseCharacter.generated.h"


// 前置声明，避免在此处引入不必要的头文件导致互相包含报错
class UInputMappingContext;
class USpringArmComponent;	
class UInputAction;
class UCameraComponent;
class USkeletalMeshComponent;
class ABaseWeapon; // 我们新建的武器基类
class UGameHUDWidget;

UCLASS()
class METALSLUG01_API ABaseCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ABaseCharacter();

	// 必须重写这个函数来同步网络变量（如生命值）
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
	// 当前的攻击动作是否锁死了移动？
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	bool bIsMovementLocked = false;
	
	// 暴露给外部或蓝图调用的生死状态获取接口
	UFUNCTION(BlueprintCallable, Category = "Stats")
	bool GetIsDead() const { return bIsDead; }
	
	// ==========================================
	// 阵营系统 (0=人类, 1=丧尸)
	// ==========================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Team")
	uint8 TeamID = 0; // 默认是人类

	// 提供给 AI 和 GameMode 调用的公开接口
	UFUNCTION(BlueprintCallable, Category = "Combat|Team")
	uint8 GetTeamID() const { return TeamID; }
	
	// 开放给外部检查生死的只读接口
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "State")
	bool IsDead() const { return bIsDead; }

protected:
	virtual void BeginPlay() override;

public:	
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;

	// ==========================================
	// 1. 核心组件 (第三人称视角)
	// ==========================================
protected:
	// 第三人称专属的“自拍杆” (弹簧臂)，防止相机穿墙
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Camera")
	USpringArmComponent* CameraBoom;

	// 挂在自拍杆后面的跟随摄像机
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Camera")
	UCameraComponent* FollowCamera;
	
	// ==========================================
	// 2. 战斗属性 (网络同步)
	// ==========================================
protected:
	// 最大血量 (暴露给蓝图，可以随时修改)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats")
	float MaxHealth;

	// 当前生命值 (ReplicatedUsing 表示当服务器改变这个值时，会自动通知客户端执行 OnRep_Health)
	UPROPERTY(ReplicatedUsing = OnRep_Health, VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	float CurrentHealth;

	// 生命值改变时，客户端自动调用的回调函数 (用来刷新屏幕上的血条UI)
	UFUNCTION()
	void OnRep_Health();

	// ==========================================
	// 能量系统 (网络同步)
	// ==========================================
	// 最大能量值
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats|Energy")
	float MaxEnergy;

	// 当前能量值
	UPROPERTY(ReplicatedUsing = OnRep_Energy, VisibleAnywhere, BlueprintReadOnly, Category = "Stats|Energy")
	float CurrentEnergy;

	// 能量改变时的回调
	UFUNCTION()
	void OnRep_Energy();

	// 消耗能量（释放技能时调用）
	UFUNCTION(BlueprintCallable, Category = "Stats|Energy")
	bool ConsumeEnergy(float Amount);

	// 增加能量
	UFUNCTION(BlueprintCallable, Category = "Stats|Energy")
	void AddEnergy(float Amount);

	// 获取当前能量百分比
	UFUNCTION(BlueprintCallable, Category = "Stats|Energy")
	float GetEnergyPercent() const { return (MaxEnergy > 0.0f) ? (CurrentEnergy / MaxEnergy) : 0.0f; }

public:
	// 获取当前血量（供 UI 直接访问）
	UFUNCTION(BlueprintCallable, Category = "Stats")
	float GetCurrentHealth() const { return CurrentHealth; }

	// 获取最大血量（供 UI 直接访问）
	UFUNCTION(BlueprintCallable, Category = "Stats")
	float GetMaxHealth() const { return MaxHealth; }

	// 获取当前能量（供 UI 直接访问）
	UFUNCTION(BlueprintCallable, Category = "Stats|Energy")
	float GetCurrentEnergy() const { return CurrentEnergy; }

	// 获取最大能量（供 UI 直接访问）
	UFUNCTION(BlueprintCallable, Category = "Stats|Energy")
	float GetMaxEnergy() const { return MaxEnergy; }

protected:

	// ==========================================
	// 助攻追踪系统
	// ==========================================
public:
	// 助攻判定时间窗口（秒）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats|Assist")
	float AssistTimeWindow = 5.0f;

	// 最后一次攻击目标的时间戳（用于判定助攻）
	TMap<TWeakObjectPtr<AActor>, float> LastHitTimestamps;

	// 检查是否可以给予助攻
	bool CanGrantAssist(AActor* PotentialAssistant) const;

	// 授予符合条件的玩家助攻得分
	static void GrantAssistsToEligiblePlayers(ABaseCharacter* Victim, ABaseCharacter* Killer);

	// 当受到伤害时，通知可能的助攻者（供武器系统调用）
	UFUNCTION(BlueprintCallable, Category = "Stats|Assist")
	void NotifyDamageDealtTo(AActor* Victim);

protected:

	// ==========================================
	// 生命/能量回复系统
	// ==========================================
protected:
	// 停止移动后开始回复的时间（秒）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats|Regeneration")
	float RegenerationDelay = 2.0f;

	// 生命回复速度（每秒）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats|Regeneration")
	float HealthRegenRate = 5.0f;

	// 能量回复速度（每秒）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats|Regeneration")
	float EnergyRegenRate = 10.0f;

	// 最后移动时间
	float LastMoveTime;

	// 是否正在回复
	bool bIsRegenerating;

	// 开始回复
	void StartRegeneration();

	// 停止回复
	void StopRegeneration();
	
	// 死亡状态锁 (防止鞭尸)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Stats")
	bool bIsDead = false;

	// 记录最后的击杀方式（用于 HUD 击杀信息显示）
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Stats")
	EKillMethod LastKillMethod = EKillMethod::MeleeWeapon;
	
	// 死亡动画蒙太奇
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	UAnimMontage* DeathMontage;

	// 死亡逻辑与布娃娃定时器
	void Die();
	void EnableRagdoll();
	FTimerHandle RagdollTimerHandle;

	// 网络多播死亡（让所有玩家都看到你变成布娃娃）
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_Die();

	// ==========================================
	// 击杀奖励系统
	// ==========================================
public:
	// 击杀奖励接口
	UFUNCTION(BlueprintCallable, Category = "Stats")
	void OnKill(ABaseCharacter* KilledCharacter);

	// 击杀增加的血量
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats|KillReward")
	float HealthRewardPerKill = 10.0f;

	// 击杀增加的能量
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats|KillReward")
	float EnergyRewardPerKill = 25.0f;

	// ==========================================
	// UI连接
	// ==========================================
protected:
	// 游戏HUD引用
	UPROPERTY()
	UGameHUDWidget* GameHUDWidget;

	// ==========================================
	// 3. 增强输入系统 (UE5 标配)
	// ==========================================
protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputMappingContext* DefaultMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* MoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* LookAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* JumpAction;
	
	//下蹲输入动作
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* CrouchAction;

	// 刀战核心输入
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* LightAttackAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* HeavyAttackAction;

	// 释放技能输入动作
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* UseSkillAction;

	// 基础输入回调
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	
	// 下蹲回调
	void StartCrouch();
	void StopCrouch();

	// ==========================================
	// 4. 武器与自动连斩系统核心变量
	// ==========================================
protected:
	// 角色手里当前拿着的武器！(通过它来获取动画和执行判定)
	UPROPERTY(Replicated,VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	class ABaseWeapon* CurrentWeapon;

	bool bIsAttacking;            // 全局攻击锁：正在挥刀吗？
	bool bCanReceiveInput;        // 绿灯亮起：现在处于输入区间内吗？
	bool bSaveAttack;             // 缓存区：玩家在这个区间内点击过鼠标吗？
	bool bIsHoldingLightAttack;   // 按住状态：玩家是一直死死按着左键没松吗？
	int32 ComboIndex;             // 当前连招段数 (1 或 2)

	// 连招核心执行逻辑
	void ExecuteComboSequence();
	
	// 轻击长按/松开事件
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void LightAttack_Pressed();
	
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void LightAttack_Released();
	
	// 重击事件（单击）
	void HeavyAttack();

	// 释放技能事件
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void UseSkill();

	// ==========================================
	// 5. 动画通知 (Anim Notify) 专用接口
	// ==========================================
public:
	// 供动画蓝图区间 (Anim Notify State) 调用的接口
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void EnableComboWindow(); // 区间开始时触发

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void CheckCombo();        // 区间结束时触发

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void EndAttackState();    // 动画彻底结束时触发
	
	
	// ==========================================
	// 6. 战斗 RPC (网络动作同步)
	// ==========================================
protected:
	// 客户端向服务器请求挥刀（带着连击序号，告诉大家播第几个动作）
	UFUNCTION(Server, Reliable)
	void Server_PlayAttackAnim(bool bIsHeavy, int32 InComboIndex);

	// 服务器向所有客户端广播：“大家都播放这个人的挥刀动画！”
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayAttackAnim(bool bIsHeavy, int32 InComboIndex);
	
	// ==========================================
	// 供上帝 (GameMode) 调用的装备武器接口
	// ==========================================
public:
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void EquipWeapon(TSubclassOf<ABaseWeapon> WeaponClassToEquip);
	
	// ==========================================
	// 公开获取当前武器的接口，给动画系统用
	// ==========================================
	UFUNCTION(BlueprintCallable, Category = "Combat")
	class ABaseWeapon* GetCurrentWeapon() const { return CurrentWeapon; }
	
protected:
	// --- 溶解系统配置 ---

	// 死亡多久后开始融化 (默认10秒)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float DissolveDelay = 5.0f;

	// 融化的速度
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float DissolveSpeed = 0.5f;

	// 动态材质实例数组（因为一个模型可能有多个材质槽）
	UPROPERTY()
	TArray<class UMaterialInstanceDynamic*> DynamicMaterials;

	// 内部标记
	bool bStartDissolving = false;
	float CurrentDissolveValue = 0.0f; // 0=正常, 1=完全融化

	// 定时器回调
	void StartDissolveProcess();
	
	// ==========================================
	// 3A 级摄像机减震系统
	// ==========================================

	// 重写虚幻原生自带的下蹲与起立回调
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	void PossessedBy(AController* NewController);
	void SpawnAndEquipWeapon(FString WeaponID);

	// 摄像机平滑过渡的速度 (越大越快，越小越像慢动作)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CrouchCameraSmoothSpeed = 12.0f;

	// ==========================================
	// AC/ACE系统 (类似CS中的得分系统)
	// ==========================================
public:
	// 最大AC值（蓝图可配置，默认100）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats|AC", meta = (ClampMin = "1"))
	int32 MaxAC = 100;

	// 当前AC值（网络同步）
	UPROPERTY(ReplicatedUsing = OnRep_ACValue, BlueprintReadOnly, Category = "Stats|AC")
	int32 ACValue;

	// ACE值（连续获胜回合数，网络同步）
	UPROPERTY(ReplicatedUsing = OnRep_ACEValue, BlueprintReadOnly, Category = "Stats|AC")
	int32 ACEValue;

	// 增加AC值
	UFUNCTION(BlueprintCallable, Category = "Stats|AC")
	void AddAC(int32 Amount);

	// 扣除AC值（用于结算时扣分）
	UFUNCTION(BlueprintCallable, Category = "Stats|AC")
	void TakeAC(int32 Amount);

	// 重置AC到0（同时重置ACE）
	UFUNCTION(BlueprintCallable, Category = "Stats|AC")
	void ResetAC();

	// 获取当前AC值
	UFUNCTION(BlueprintCallable, Category = "Stats|AC")
	int32 GetAC() const { return ACValue; }

	// 获取最大AC值
	UFUNCTION(BlueprintCallable, Category = "Stats|AC")
	int32 GetMaxAC() const { return MaxAC; }

	// 获取AC百分比（0.0~1.0）
	UFUNCTION(BlueprintCallable, Category = "Stats|AC")
	float GetACPercent() const { return MaxAC > 0 ? static_cast<float>(ACValue) / static_cast<float>(MaxAC) : 0.0f; }

	// 获取ACE值
	UFUNCTION(BlueprintCallable, Category = "Stats|AC")
	int32 GetACE() const { return ACEValue; }

	// 根据当前 PlayerState 的 SelectedCharacterID，从 CharacterDataTable 查出头像并刷新 HUD
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void RefreshCharacterIcon();

	// 查询当前 ACE 排名并刷新 HUD 上的 ACE 文字颜色
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void RefreshACEWithRank();

	// ==========================================
	// 计分板系统 (Scoreboard)
	// ==========================================
public:
	// 服务器广播击杀信息给所有客户端（用于更新计分板）
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_NotifyKill(AActor* VictimActor, AActor* AssistantActor);

	// 获取击杀者 PlayerState
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	class ARoomPlayerState* GetRoomPlayerState() const;

private:
	// HUD 还未就绪时延迟刷新的辅助方法
	void RetryRefreshHUD();

protected:
	// 从 CharacterDataTable 查指定角色 ID 的头像贴图
	class UTexture2D* GetCharacterAvatarFromTable(const FString& CharID);

protected:
	// AC/ACE 改变时的回调（网络复制通知）
	UFUNCTION()
	void OnRep_ACValue();

	UFUNCTION()
	void OnRep_ACEValue();
};