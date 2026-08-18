// ==========================================
// 能量 Component 【2026-06-15 重构: 升级为数据权威持有者 + 网络复制】
// 目的: ABaseCharacter 的能量管理全部下沉到本 Component
// 优势:
//   1. 数据权威唯一: 消除 ABaseCharacter 与 Component 双轨不一致
//   2. 网络复制: CurrentEnergy 同步到所有客户端, 客户端可读取能量百分比
//   3. 事件广播: OnEnergyChanged 供 HUD/特效订阅, 替代 OnRep 轮询
//   4. 委托接口: Consume/Add/GetCurrent 等供 Actor 调用
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnergyComponent.generated.h"

// 能量改变多播: 供 UI/HUD/特效订阅
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnergyChanged, float, NewEnergy);

/**
 * @class UEnergyComponent
 * @brief 能量管理组件 - 玩家技能消耗系统（冲刺/技能等）
 *
 * 单一职责: 持有并管理角色的能量值 (CurrentEnergy/MaxEnergy)
 *
 * 大厂架构中的角色:
 *   - 数据权威: 真理源在此组件, BaseCharacter 不再持有重复的能量字段
 *   - 网络复制: CurrentEnergy (ReplicatedUsing=OnRep_CurrentEnergy) 同步到所有客户端
 *   - 事件总线: OnEnergyChanged 供 HUD/特效订阅 (替代 OnRep 轮询的反模式)
 *
 * 关键设计:
 *   - 服务器调 Consume/Add → 直接修改 CurrentEnergy → 触发 Broadcast + 复制
 *   - 客户端收到复制 → OnRep_CurrentEnergy → Broadcast 给本地 HUD
 *   - InitializeEnergy 接收配置参数 (策划在 BP 配置)
 *
 * 使用方式:
 *   - ABaseCharacter::ConsumeEnergy / AddEnergy 转发到本组件
 *   - HUD (PlayerStatusWidget) 订阅 OnEnergyChanged 刷新能量条
 */
UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UEnergyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnergyComponent();

	/**
	 * 初始化能量
	 * @param InMax 最大能量值
	 * @param InCurrent 当前能量值（默认等于最大值，即满状态）
	 */
	UFUNCTION(BlueprintCallable, Category = "Energy")
	void InitializeEnergy(float InMax, float InCurrent);

	/**
	 * 消耗能量（服务器调用，自动同步）
	 * @param Amount 要消耗的能量值
	 * @return 是否消耗成功（能量不足时返回 false）
	 */
	UFUNCTION(BlueprintCallable, Category = "Energy")
	bool Consume(float Amount);

	/**
	 * 增加能量（服务器调用，自动同步）
	 * @param Amount 要增加的能量值
	 */
	UFUNCTION(BlueprintCallable, Category = "Energy")
	void Add(float Amount);

	// ===== 访问器 =====
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Energy")
	float GetCurrent() const { return CurrentEnergy; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Energy")
	float GetMax() const { return MaxEnergy; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Energy")
	float GetPercent() const { return MaxEnergy > 0.0f ? CurrentEnergy / MaxEnergy : 0.0f; }

	// ===== 事件订阅 =====
	/** 能量改变事件: HUD/特效订阅 */
	UPROPERTY(BlueprintAssignable, Category = "Energy")
	FOnEnergyChanged OnEnergyChanged;

protected:
	// ===== 复制支持 =====
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * OnRep 回调: 客户端收到能量同步时触发, 广播给 HUD
	 * 注意: OnRep 只在"值变化"时触发, 初值同步不触发
	 *       所以 HUD 初值刷新应通过 InitializeEnergy 后手动调用
	 */
	UFUNCTION()
	void OnRep_CurrentEnergy();

private:
	/** 最大能量值 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Energy", meta = (AllowPrivateAccess = "true"))
	float MaxEnergy = 100.0f;

	/**
	 * 当前能量值
	 * ReplicatedUsing: 同步到所有客户端, 客户端通过 OnRep_CurrentEnergy 刷新 HUD
	 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentEnergy, VisibleAnywhere, BlueprintReadOnly, Category = "Energy", meta = (AllowPrivateAccess = "true"))
	float CurrentEnergy = 100.0f;
};
