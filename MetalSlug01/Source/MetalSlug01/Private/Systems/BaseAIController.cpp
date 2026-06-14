// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本类头文件
#include "Systems/BaseAIController.h"

// 引入 AI 感知组件
#include "Perception/AIPerceptionComponent.h"

// 引入行为树
#include "BehaviorTree/BehaviorTree.h"

// 引入黑板组件
#include "BehaviorTree/BlackboardComponent.h"

// 引入角色基类（用于 GetTeamID 阵营判定）
#include "Characters/BaseCharacter.h"


// ==========================================
// 1. 构造函数
// ==========================================

/**
 * ABaseAIController 构造函数
 *
 * 目的: 实例化 AI 感知组件
 * 关键: 具体感官配置（如视觉、听觉）留给子类去配
 */
ABaseAIController::ABaseAIController()
{
	// 1. 实例化感知组件 (具体配什么感官，留给子类去配)
	AIPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));
}


// ==========================================
// 2. BeginPlay
// ==========================================

/**
 * ABaseAIController::BeginPlay
 *
 * 目的: 绑定感知事件、启动行为树
 * 1. 绑定 OnTargetPerceptionUpdated 事件
 * 2. 如果配置了行为树，自动 RunBehaviorTree
 */
void ABaseAIController::BeginPlay()
{
	Super::BeginPlay();

	// 2. 绑定感知事件
	if (AIPerception)
	{
		AIPerception->OnTargetPerceptionUpdated.AddDynamic(this, &ABaseAIController::OnTargetDetected);
	}

	// 3. 如果在蓝图里配了行为树，出生就直接跑起来
	if (AIBehaviorTree)
	{
		RunBehaviorTree(AIBehaviorTree);
	}
}


// ==========================================
// 3. 感知触发入口
// ==========================================

/**
 * OnTargetDetected
 *
 * 感知触发时的统一入口
 * 核心逻辑上移: 不管是拿刀还是丧尸，只要成功感知到了敌人！
 * 1. 确认成功感知
 * 2. 确认是敌人
 * 3. 确认自己有 Pawn
 * 4. 极近距离 (250) 内，强行写入黑板 ImmediateTarget 打断行为树
 */
void ABaseAIController::OnTargetDetected(AActor* Actor, FAIStimulus Stimulus)
{
	// 【核心逻辑上移】: 不管是拿刀还是丧尸，只要成功感知到了敌人
	if (Stimulus.WasSuccessfullySensed() && IsEnemy(Actor))
	{
		// 【防崩金牌】: 确保自己现在确确实实附身在一个肉体上
		if (GetPawn())
		{
			float Distance = FVector::Dist(Actor->GetActorLocation(), GetPawn()->GetActorLocation());

			// 只要在极近距离 (比如 250) 内
			if (Distance < 250.0f)
			{
				// 强行把这个倒霉蛋写入黑板的 ImmediateTarget 里，直接打断行为树去砍他
				if (GetBlackboardComponent())
				{
					GetBlackboardComponent()->SetValueAsObject(FName("ImmediateTarget"), Actor);
				}
			}
		}
	}
}


// ==========================================
// 4. 敌我判定
// ==========================================

/**
 * IsEnemy
 *
 * 判断敌我的通用逻辑
 * 1. 既然自己不能打自己
 * 2. 尝试把对方转换成 ABaseCharacter
 * 3. 神级判定: 阵营不一样就是敌人
 */
bool ABaseAIController::IsEnemy(AActor* TargetActor)
{
	// 1. 既然自己不能打自己
	if (TargetActor == GetPawn()) return false;

	// 2. 尝试把对方转换成我们的基础角色类
	ABaseCharacter* OtherCharacter = Cast<ABaseCharacter>(TargetActor);
	if (OtherCharacter)
	{
		// 【神级判定】: 只要咱们俩阵营不一样，你就是我的敌人
		// (假设你在 BaseCharacter 里也写了一个 GetTeamID 的函数)
		return this->TeamID != OtherCharacter->GetTeamID();
	}

	return false;
}
