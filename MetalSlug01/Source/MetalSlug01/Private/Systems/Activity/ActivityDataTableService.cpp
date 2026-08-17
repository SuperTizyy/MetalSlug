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
