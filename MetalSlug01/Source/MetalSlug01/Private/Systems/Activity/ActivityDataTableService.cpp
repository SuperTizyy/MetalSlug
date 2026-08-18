// ========================================================================
// ActivityDataTableService.cpp — 活动 DataTable 服务 UObject 容器实现文件
// ========================================================================
//
// 文件功能总览:
//   - 实现工厂函数 Create:用 NewObject 创建 UObject 子对象
//   - 内部持有 FActivityDataTableService 实例(在 .h 中)
//   - 通过 UObject 子对象机制保证生命周期跟随 Owner(ActivitySubsystem)
//
// 大厂原则:
//   - 单一真理源:所有活动 DataTable 加载入口集中在这里
//   - GC 安全:TStrongObjectPtr 在 FActivityDataTableService 内持有 DataTable
//   - 弱依赖:外部调用方只持有 UActivityDataTableService 弱引用,析构顺序可控
// ========================================================================

// ==========================================
// UActivityDataTableService 实现
// ==========================================
#include "Systems/Activity/ActivityDataTableService.h"

UActivityDataTableService* UActivityDataTableService::Create(UObject* Outer)
{
	// NewObject<>() 由 GC 体系管理, 防止静态对象生命周期问题
	UActivityDataTableService* Instance = NewObject<UActivityDataTableService>(Outer);
	return Instance;
}
