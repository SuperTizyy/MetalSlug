#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h" // 感知类型所需
#include "BaseAIController.generated.h"

class UAIPerceptionComponent;
class UBehaviorTree;

UCLASS()
class METALSLUG01_API ABaseAIController : public AAIController
{
	GENERATED_BODY()

public:
	ABaseAIController();

protected:
	virtual void BeginPlay() override;

	// ==========================================
	// 1. 全局通用组件
	// ==========================================
	// 无论什么 AI，都需要感知系统（眼睛/耳朵）
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
	UAIPerceptionComponent* AIPerception;

	// 无论什么 AI，都需要一棵行为树来驱动
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "AI")
	UBehaviorTree* AIBehaviorTree;

	// ==========================================
	// 2. 留给子类重写的通用接口 (Virtual)
	// ==========================================
	// 感知触发时的统一入口
	UFUNCTION()
	virtual void OnTargetDetected(AActor* Actor, FAIStimulus Stimulus);

	// 判断敌我的通用逻辑
	virtual bool IsEnemy(AActor* TargetActor);
	
	// ==========================================
	// 阵营系统 (0=人类, 1=丧尸)
	// ==========================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Team")
	uint8 TeamID = 0; // 默认是 0
	
public:
	// 开放一个获取阵营的接口，方便其他人读取
	UFUNCTION(BlueprintCallable, Category = "AI|Team")
	uint8 GetTeamID() const { return TeamID; }

	// 开放一个修改阵营的接口 (生化模式感染时调用它！)
	UFUNCTION(BlueprintCallable, Category = "AI|Team")
	void SetTeamID(uint8 NewTeamID) { TeamID = NewTeamID; }
};