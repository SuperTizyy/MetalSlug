// ==========================================
// 活动 DataTable 服务 — UObject 容器 (v231 大厂架构)
// ==========================================
// v231 重构要点:
//   - FActivityDataTableService (struct, 在 FActivityDataTableService.h) 持有实际的缓存
//   - 本文件定义的 UActivityDataTableService (UObject 子类) 是 FActivityDataTableService 的 UObject 容器
//     - 由 UActivitySubsystem 创建为子对象
//     - 生命周期绑定到 Subsystem, GC 友好
//     - 不需要 .generated.h 外的额外 include
//
// 拆分原因:
//   - UE UHT 对"同一头文件里既有 struct 又有 UCLASS" 处理严格
//   - 拆开后, FActivityDataTableService 可以保持纯 struct, 模板实现灵活
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Data/FActivityDataTableService.h" // FActivityDataTableService 定义
#include "ActivityDataTableService.generated.h"

/**
 * @class UActivityDataTableService
 * @brief 活动 DataTable 服务的 UObject 容器
 * @details 持有 FActivityDataTableService 实例, 绑定生命周期到 Subsystem (创建者)
 *          UObject 子对象机制保证:
 *            - 自身不被 GC 回收
 *            - 持有的 FActivityDataTableService 也不被 GC 回收
 *            - 内部 TStrongObjectPtr<UDataTable> 进一步保证 DataTable 资产不被 GC
 *            - 析构顺序: 子对象 → Owner → 任何外部缓存
 */
UCLASS()
class METALSLUG01_API UActivityDataTableService : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * @brief 获取内部的 FActivityDataTableService 实例 (引用)
	 * @note 返回引用, 生命周期由 UActivityDataTableService 保证 (UObject 子对象)
	 */
	FActivityDataTableService& GetService() { return Service; }

	/**
	 * @brief 工厂: 创建 UActivityDataTableService UObject 子对象
	 * @param Outer 拥有者 (必须是 UActivitySubsystem)
	 * @return 创建的实例
	 */
	static UActivityDataTableService* Create(UObject* Outer);

	UActivityDataTableService() = default;
	virtual ~UActivityDataTableService() override = default;

private:
	/**
	 * @brief 持有实际的 DataTable 服务 (生命周期锚定)
	 */
	FActivityDataTableService Service;
};
