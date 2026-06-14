// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"

// 引入 UE 原生 AAIController 类（基类）
#include "AIController.h"

// 感知类型所需 (FAIStimulus 等)
#include "Perception/AIPerceptionTypes.h"

// UE 自动生成的头文件
#include "BaseAIController.generated.h"

// 前置声明（加快编译）
class UAIPerceptionComponent;
class UBehaviorTree;

/**
 * @class ABaseAIController
 * @brief 项目所有 AI 控制器的 C++ 基类
 *
 * 职责说明:
 * - 提供全局通用的 AI 感知系统（眼睛/耳朵）
 * - 提供行为树（Behavior Tree）启动入口
 * - 提供阵营（TeamID）系统和敌我判定接口
 * - 触发"近距离目标"机制（写入黑板 ImmediateTarget）
 *
 * 架构理念:
 * 1. 统一入口: 所有 AI 共享感知和行为树驱动逻辑
 * 2. 敌我判定: 基于 TeamID 阵营比对，子类可覆写
 * 3. 行为树中断: 极近距离检测到敌人时，强行写入黑板打断 BT
 */
UCLASS()
class METALSLUG01_API ABaseAIController : public AAIController
{
	GENERATED_BODY()

public:
	/**
	 * 构造函数
	 * 目的: 创建 AI 感知组件（具体感官配置留给子类）
	 */
	ABaseAIController();

protected:
	/**
	 * UE 原生生命周期: 在 AI 控制器被初始化时调用
	 * 目的: 绑定感知事件、启动行为树
	 */
	virtual void BeginPlay() override;

	// ==========================================
	// 1. 全局通用组件
	// ==========================================

	/**
	 * AI 感知系统（眼睛/耳朵等）
	 * 用途: 检测周围的 Actor 状态变化
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
	UAIPerceptionComponent* AIPerception;

	/**
	 * AI 行为树
	 * 用途: 在 BeginPlay 时自动 RunBehaviorTree
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "AI")
	UBehaviorTree* AIBehaviorTree;

	// ==========================================
	// 2. 留给子类重写的通用接口 (Virtual)
	// ==========================================

	/**
	 * 感知触发时的统一入口
	 * 子类可覆写以扩展特殊处理
	 * @param Actor 感知到的 Actor
	 * @param Stimulus 刺激信息
	 */
	UFUNCTION()
	virtual void OnTargetDetected(AActor* Actor, FAIStimulus Stimulus);

	/**
	 * 判断敌我的通用逻辑
	 * 子类可覆写以实现特殊判定规则
	 * @param TargetActor 目标 Actor
	 * @return 是否为敌人
	 */
	virtual bool IsEnemy(AActor* TargetActor);

	// ==========================================
	// 阵营系统 (0=人类, 1=丧尸)
	// ==========================================

	/**
	 * AI 阵营 ID
	 * 0 = 攻方/人类
	 * 1 = 守方/丧尸
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Team")
	uint8 TeamID = 0; // 默认是 0

public:
	/**
	 * 获取阵营 ID（供其他人读取）
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Team")
	uint8 GetTeamID() const { return TeamID; }

	/**
	 * 修改阵营 ID（生化模式感染时调用）
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Team")
	void SetTeamID(uint8 NewTeamID) { TeamID = NewTeamID; }
};
