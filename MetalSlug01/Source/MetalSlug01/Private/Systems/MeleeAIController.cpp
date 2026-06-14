// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本类头文件
#include "Systems/MeleeAIController.h"

// 引入黑板组件
#include "BehaviorTree/BlackboardComponent.h"

// 引入 AI 感知组件
#include "Perception/AIPerceptionComponent.h"

// 引入 AI 感知类型
#include "Perception/AIPerceptionTypes.h"

// 引入 AI 视觉感官配置
#include "Perception/AISenseConfig_Sight.h"

// 引入角色基类（用于阵营判定）
#include "Characters/BaseCharacter.h"


// ==========================================
// 1. 构造函数
// ==========================================

/**
 * AMeleeAIController 构造函数
 *
 * 目的: 创建并配置刀战独有的"超强视觉"感官
 * 关键:
 * 1. 视距 1500，丢失视距 1800
 * 2. 周视角 90 度
 * 3. 检测敌/中/友
 * 4. 复用父类 AIPerception 配置（不要重复 Create）
 */
AMeleeAIController::AMeleeAIController()
{
	// 2. 自己创建属于刀战独有的"视力配置" (这个不冲突，因为爷爷没有)
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("Sight Config"));
	SightConfig->SightRadius = 1500.0f;
	SightConfig->LoseSightRadius = 1800.0f;
	SightConfig->PeripheralVisionAngleDegrees = 90.0f;
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

	// 3. 【重点】: 直接拿爷爷创建好的 AIPerception 来用! 不要去 Create 它
	if (AIPerception)
	{
		AIPerception->ConfigureSense(*SightConfig);
		AIPerception->SetDominantSense(SightConfig->GetSenseImplementation());
	}
}
