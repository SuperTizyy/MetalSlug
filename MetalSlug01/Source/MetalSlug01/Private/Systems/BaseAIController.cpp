#include "Systems/BaseAIController.h"

#include "Perception/AIPerceptionComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Characters/BaseCharacter.h"

ABaseAIController::ABaseAIController()
{
	// 1. 实例化感知组件 (具体配什么感官，留给子类去配)
	AIPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));
}

void ABaseAIController::BeginPlay()
{
	Super::BeginPlay();

	// 2. 绑定感知事件
	if (AIPerception)
	{
		AIPerception->OnTargetPerceptionUpdated.AddDynamic(this, &ABaseAIController::OnTargetDetected);
	}

	// 3. 如果在蓝图里配了行为树，出生就直接跑起来！
	if (AIBehaviorTree)
	{
		RunBehaviorTree(AIBehaviorTree);
	}
}

void ABaseAIController::OnTargetDetected(AActor* Actor, FAIStimulus Stimulus)
{
	// 【核心逻辑上移】：不管是拿刀还是丧尸，只要成功感知到了敌人！
	if (Stimulus.WasSuccessfullySensed() && IsEnemy(Actor))
	{
		// 【防崩金牌】：确保自己现在确确实实附身在一个肉体上！
		if (GetPawn())
		{
			float Distance = FVector::Dist(Actor->GetActorLocation(), GetPawn()->GetActorLocation());
		
			// 只要在极近距离 (比如 250) 内
			if (Distance < 250.0f) 
			{
				// 强行把这个倒霉蛋写入黑板的 ImmediateTarget 里，直接打断行为树去砍他！
				if (GetBlackboardComponent())
				{
					GetBlackboardComponent()->SetValueAsObject(FName("ImmediateTarget"), Actor);
				}
			}
		}
	}
}

bool ABaseAIController::IsEnemy(AActor* TargetActor)
{
	// 1. 既然自己不能打自己
	if (TargetActor == GetPawn()) return false;

	// 2. 尝试把对方转换成我们的基础角色类
	ABaseCharacter* OtherCharacter = Cast<ABaseCharacter>(TargetActor);
	if (OtherCharacter)
	{
		// 【神级判定】：只要咱们俩阵营不一样，你就是我的敌人！
		// (假设你在 BaseCharacter 里也写了一个 GetTeamID 的函数)
		return this->TeamID != OtherCharacter->GetTeamID();
	}

	return false;
}