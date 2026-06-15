// ==========================================
// 活动存档修改器基类 【2026-06-15 重构: 真正持有共享字段】
// 作用: 统一 UDailyLoginSaveModifier 和 UUpgradeActivitySaveModifier
//       的共享逻辑, 消除两套相似代码
// ==========================================
// 共享能力:
//   1. WorldContext 弱引用 (成员字段)
//   2. CachedSaveGame 缓存 (成员字段)
//   3. bIsInitialized 状态 (成员字段)
//   4. GetOrCreateSaveGame 公共实现
//   5. 控制台命令注册/注销的统一模式 (模板方法)
//
// 【2026-06-15 修复】:
// 原设计"故意不持字段"导致子类各自重复声明 WorldContextObject/CachedSaveGame/bIsInitialized
// 实际使用中, 子类 InitializeModifier 只调了基类 InitializeBase, 没有任何赋值
// 导致 bIsInitialized 永远 = false, 整个 Modifier 系统实际不可用
// 现改为基类真正持有共享状态, 子类删除重复字段
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Logs/MetalSlugLogChannels.h"
#include "ActivitySaveModifierBase.generated.h"

class UDailyLoginSaveGame;

/**
 * @class UActivitySaveModifierBase
 * @brief 活动存档修改器基类 (统一两个具体 Modifier)
 * @note 【2026-06-15】 持有共享 WorldContext / CachedSaveGame / bIsInitialized
 */
UCLASS(Abstract, BlueprintType)
class METALSLUG01_API UActivitySaveModifierBase : public UObject
{
	GENERATED_BODY()

public:
	UActivitySaveModifierBase();

	/**
	 * @brief 初始化基类公共部分 (实际赋值 WorldContext / 置 bIsInitialized=true)
	 * @note 子类的 InitializeModifier 必须在最开头调用此方法
	 */
	virtual bool InitializeBase(UObject* WorldContext);

	/**
	 * @brief 销毁基类公共部分 (解除 WorldContext 引用, 清 CachedSaveGame, 置 bIsInitialized=false)
	 * @note 子类应先清理自己的资源, 再调用此方法
	 */
	virtual void DestroyBase();

	/**
	 * @brief 查询是否已初始化
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Activity Save")
	bool IsInitialized() const { return bIsInitialized; }

	/**
	 * @brief 获取 WorldContext 弱引用
	 * 【2026-06-15 修复】: 返回真实值 (之前永远返回 nullptr)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Activity Save")
	UObject* GetWorldContext() const { return WorldContextObject.Get(); }

	/**
	 * @brief 通过基类加载/创建存档 (子类的 GetOrCreateSaveGame 应委托到此)
	 * @details 内部用 Slot/UserIndex 通过 UGameplayStatics 加载
	 */
	UDailyLoginSaveGame* GetOrCreateSaveGameBase(int32 ActivityID, const FString& SlotName, int32 UserIndex = 0);

	/**
	 * @brief 注册控制台命令 (子类重写以注册自己的命令)
	 */
	virtual void RegisterConsoleCommands() {}

	/**
	 * @brief 注销控制台命令 (子类重写以注销自己的命令)
	 */
	virtual void UnregisterConsoleCommands() {}

protected:
	// ==========================================
	// 【2026-06-15 重构】: 共享字段提升到基类
	// 原因: 两个子类重复声明这三字段, 但 InitializeBase 从不赋值
	//       导致 bIsInitialized 永远 = false, 整套系统失效
	// 修复: 基类持有, InitializeBase 实际赋值, 子类直接继承使用
	// ==========================================

	/**
	 * 世界上下文对象 (弱引用, 防止阻挡 GC)
	 */
	UPROPERTY()
	TWeakObjectPtr<UObject> WorldContextObject;

	/**
	 * 缓存的存档实例 (供子类共享, 避免每次 LoadGameFromSlot)
	 */
	UPROPERTY()
	UDailyLoginSaveGame* CachedSaveGame;

	/**
	 * 是否已初始化
	 * 注: 不可为 UPROPERTY (非 USTRUCT/UCLASS), 仅 bool
	 *      也不需要 Replicated, 这是本地工具状态
	 */
	bool bIsInitialized = false;
};
