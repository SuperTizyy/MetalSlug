#pragma once

#include "CoreMinimal.h"
#include "Characters/BaseCharacter.h"
#include "WarriorCharacter.generated.h"

/**
 * 战士角色类 - 继承自基础角色类
 * 实现了战士特有的冲刺技能，并保留了基础角色的所有功能
 */
UCLASS()
class METALSLUG01_API AWarriorCharacter : public ABaseCharacter
{
	GENERATED_BODY()

public:
	// 构造函数 - 初始化战士角色的默认参数
	AWarriorCharacter();

protected:
	// 游戏开始时调用 - 用于初始化角色状态
	virtual void BeginPlay() override;

public:	
	// 每帧调用 - 用于更新角色状态
	virtual void Tick(float DeltaTime) override;

	// 输入绑定 - 设置玩家输入映射
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// 冲刺相关函数 - 提供给蓝图调用的接口
	UFUNCTION(BlueprintCallable, Category = "Warrior Abilities")
	void StartSprint();                               // 开始冲刺

	UFUNCTION(BlueprintCallable, Category = "Warrior Abilities")
	void StopSprint();                                // 停止冲刺

	// 冲刺输入处理函数 - 处理玩家的冲刺输入
	void SprintInput();                               // 冲刺按键按下时调用

	// 冲刺冷却结束回调函数 - 冷却结束后自动调用
	UFUNCTION()
	void OnSprintCooldownEnd();

	// 获取冲刺状态 - 返回当前冲刺状态
	UFUNCTION(BlueprintPure, Category = "Warrior Abilities")
	FORCEINLINE ESprintState GetSprintState() const { return SprintState; }

	// 获取冲刺剩余时间 - 返回当前冲刺还剩多少时间
	UFUNCTION(BlueprintPure, Category = "Warrior Abilities")
	FORCEINLINE float GetSprintRemainingTime() const { return SprintDuration - SprintTimer; }

	// 获取冲刺冷却剩余时间 - 返回冲刺冷却还剩多少时间
	UFUNCTION(BlueprintPure, Category = "Warrior Abilities")
	FORCEINLINE float GetSprintCooldownRemainingTime() const { return SprintCooldownDuration - SprintCooldownTimer; }

	// 检查是否可以冲刺 - 判断当前状态下是否允许冲刺
	UFUNCTION(BlueprintPure, Category = "Warrior Abilities")
	bool CanSprint() const;

	// 重载基础移动函数 - 根据冲刺状态调整移动速度
	virtual void BasicMove(const FVector& Direction) override;

	// 重载设置移动速度 - 同步更新冲刺相关速度
	virtual void SetMovementSpeed(float NewSpeed) override;

protected:
	// 冲刺相关变量 - 暴露给蓝图编辑器进行调试和配置
	// 冲刺速度倍率 - 冲刺时速度是正常速度的多少倍
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warrior Abilities", meta = (ClampMin = 0.0f))
	float SprintSpeedMultiplier;                      
	
	// 冲刺持续时间 - 冲刺可以维持多长时间（秒）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warrior Abilities", meta = (ClampMin = 0.0f))
	float SprintDuration;                             

	// 冲刺冷却时间 - 冲刺结束后需要等待多久才能再次冲刺（秒）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warrior Abilities", meta = (ClampMin = 0.0f))
	float SprintCooldownDuration;                     

	// 冲刺状态变量 - 仅在C++中可见，用于跟踪冲刺状态
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Warrior Abilities")
	ESprintState SprintState;                         // 当前冲刺状态

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Warrior Abilities")
	float SprintTimer;                                // 冲刺计时器 - 记录当前冲刺已持续的时间

	// 冲刺冷却计时器 - 记录当前冷却已持续的时间
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Warrior Abilities")
	float SprintCooldownTimer;                        

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Warrior Abilities")
	float NormalMovementSpeed;                        // 正常移动速度 - 保存冲刺前的移动速度

	// 冲刺相关的蓝图事件 - 在蓝图中可以重写这些事件来添加特定逻辑
	UFUNCTION(BlueprintImplementableEvent, Category = "Warrior Abilities")
	void OnSprintStarted();                           // 冲刺开始时触发的蓝图事件

	UFUNCTION(BlueprintImplementableEvent, Category = "Warrior Abilities")
	void OnSprintStopped();                           // 冲刺停止时触发的蓝图事件

	UFUNCTION(BlueprintImplementableEvent, Category = "Warrior Abilities")
	void OnSprintCooldownStarted();                   // 冲刺冷却开始时触发的蓝图事件

	UFUNCTION(BlueprintImplementableEvent, Category = "Warrior Abilities")
	void OnSprintReady();                             // 冲刺准备好时触发的蓝图事件

	// 更新冲刺状态的私有函数
	void UpdateSprintState(ESprintState NewState);    // 更新冲刺状态并触发相应逻辑

	// 冲刺计时器更新函数
	void UpdateSprintTimer(float DeltaTime);          // 更新冲刺计时器

	// 冲刺冷却计时器更新函数
	void UpdateSprintCooldownTimer(float DeltaTime);  // 更新冲刺冷却计时器
};
