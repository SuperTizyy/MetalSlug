// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"

// 引入父类 BaseAIController 头文件
#include "BaseAIController.h"

// 引入 AIController 头文件（明确依赖）
#include "Runtime/AIModule/Classes/AIController.h"

// UE 自动生成的头文件
#include "MeleeAIController.generated.h"

/**
 * @class AMeleeAIController
 * @brief 刀战 AI 控制器
 *
 * 职责说明:
 * - 继承自 ABaseAIController
 * - 配置刀战专属的"超强视觉"感官（UAISenseConfig_Sight）
 * - 大视野 + 大视角，让 AI 能更主动地发起攻击
 *
 * 架构理念:
 * 1. 复用: 直接拿父类创建的 AIPerception 来配置，无需重复创建
 * 2. 专属: 刀战特有的近距离反应（250 内打断行为树）已在父类实现
 */
UCLASS()
class METALSLUG01_API AMeleeAIController : public ABaseAIController
{
	GENERATED_BODY()

public:
	/**
	 * 构造函数
	 * 目的: 创建并配置刀战独有的视觉感官
	 */
	AMeleeAIController();

protected:
	/**
	 * 刀战独有的感官: 超强视觉
	 * 视距 1500，丢失视距 1800，周视角 90 度
	 */
	UPROPERTY()
	class UAISenseConfig_Sight* SightConfig;
};
