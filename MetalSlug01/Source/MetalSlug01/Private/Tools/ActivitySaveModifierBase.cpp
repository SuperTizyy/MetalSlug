// ==========================================
// UActivitySaveModifierBase 实现 【2026-06-15 重构: 实际持有共享状态】
// ==========================================
#include "Tools/ActivitySaveModifierBase.h"
#include "Data/ActivitySaveGame.h" // UActivitySaveGame
#include "Kismet/GameplayStatics.h"

UActivitySaveModifierBase::UActivitySaveModifierBase()
{
	// 字段已在头文件中默认初始化 (TWeakObjectPtr=nullptr, CachedSaveGame=nullptr, bIsInitialized=false)
}


bool UActivitySaveModifierBase::InitializeBase(UObject* WorldContext)
{
	if (!WorldContext)
	{
		UE_LOG(LogAccount, Error, TEXT("[ActivitySaveModifierBase] WorldContext 为空"));
		bIsInitialized = false;
		return false;
	}

	// 【2026-06-15 修复】: 实际赋值共享字段
	// 修复前: 字段永远是空值, 导致后续所有操作 100% 失败
	WorldContextObject = WorldContext;
	CachedSaveGame = nullptr; // 延迟到子类 GetOrCreateSaveGame 时再加载
	bIsInitialized = true;

	UE_LOG(LogAccount, Log, TEXT("[ActivitySaveModifierBase] 初始化成功: %s"), *GetClass()->GetName());
	return true;
}


void UActivitySaveModifierBase::DestroyBase()
{
	// 【2026-06-15 修复】: 实际清理共享状态
	// 修复前: 字段永远不清理, 引用泄漏
	WorldContextObject.Reset();
	CachedSaveGame = nullptr;
	bIsInitialized = false;

	UE_LOG(LogAccount, Log, TEXT("[ActivitySaveModifierBase] 销毁: %s"), *GetClass()->GetName());
}


UActivitySaveGame* UActivitySaveModifierBase::GetOrCreateSaveGameBase(int32 ActivityID, const FString& SlotName, int32 UserIndex)
{
	// 【2026-06-15 修复】: 如果已有缓存直接返回
	// 修复前: 基类方法被定义但从未被调用, 子类各自实现
	if (CachedSaveGame)
	{
		return CachedSaveGame;
	}

	// 1. 尝试加载已存在存档
	if (UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
	{
		UActivitySaveGame* Loaded = Cast<UActivitySaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex));
		if (Loaded)
		{
			CachedSaveGame = Loaded;
			return Loaded;
		}
	}

	// 2. 没有则创建新存档
	UActivitySaveGame* NewSave = Cast<UActivitySaveGame>(UGameplayStatics::CreateSaveGameObject(UActivitySaveGame::StaticClass()));
	if (NewSave)
	{
		CachedSaveGame = NewSave;
		UE_LOG(LogAccount, Log, TEXT("[ActivitySaveModifierBase] 创建新存档: Slot=%s, ActivityID=%d"), *SlotName, ActivityID);
	}
	return NewSave;
}
