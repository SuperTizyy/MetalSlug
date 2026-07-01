// ==========================================
// 头文件包含说明
// ==========================================
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Data/Account/AccountSaveGame.h"
#include "LocalAccountStore.generated.h"


/**
 * @class ULocalAccountStore
 * @brief L0 存储层 - 本地账号存档的纯 IO 封装
 *
 * 【大厂架构原则：Storage Layer 三大铁律】
 * 1) 单一职责: 只做 Load/Save 的物理 IO，不做任何业务校验。
 * 2) 进程唯一: 一个进程（一个客户端窗口）一个 Store 实例，
 *    槽位名由 Initialize 时基于本地稳定 GUID 生成，永不依赖 PlayerController/GameInstance 索引。
 * 3) 缓存与落盘分离: 内存缓存由 Repository 层持有，Store 只负责"序列化为 USaveGame"。
 *
 * 【双开隔离根因修复】
 * 旧实现槽位 = "LocalAccountDataSlot_<PlayerUniqueID>"。
 * PIE 双开时 PlayerID 通常为 0 / 1，但打包多开后进程内 ID 可能重复，导致两窗口写同一 .sav。
 * 新实现槽位 = "LocalAccountDataSlot_<PersistedLocalClientGuid>"，
 * GUID 由本 Store 在首次启动时生成并落到本地配置文件，
 * 同一台机器不同窗口获得不同 GUID → 槽位天然隔离 → 双开不会互相覆盖。
 */
UCLASS()
class METALSLUG01_API ULocalAccountStore : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem 生命周期
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * 获取当前 Store 持有的存档容器
	 * @return USaveGame 指针（不存在或加载失败时返回 nullptr）
	 */
	UFUNCTION(BlueprintCallable, Category = "Account|Store")
	UAccountSaveGame* GetCache() const { return Cache; }

	/**
	 * 物理写入硬盘
	 * @return true = 写入成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Account|Store")
	bool FlushToDisk();

	/**
	 * 从硬盘物理加载（用于跨进程同步场景；本进程内一般不需要）
	 * @return true = 加载成功（即便存档不存在也算成功，会得到空容器）
	 */
	UFUNCTION(BlueprintCallable, Category = "Account|Store")
	bool ReloadFromDisk();

	/**
	 * 当前存档的槽位名（仅诊断用）
	 */
	UFUNCTION(BlueprintCallable, Category = "Account|Store")
	FString GetSlotName() const { return SlotName; }

private:
	/**
	 * 计算本进程专属的存档槽位名。
	 * 策略:
	 *   - 优先读本地配置文件 (Saved/Config/LocalClient.identity) 里的稳定 GUID。
	 *   - 没有则生成一个新的 GUID，写回配置文件。
	 *   - 这样即便用户 PIE 反复启停，槽位名也固定不变。
	 */
	void EnsureLocalClientIdentity();

	/** 内存缓存（Load/Reload 时填充，Save 时序列化此对象） */
	UPROPERTY()
	TObjectPtr<UAccountSaveGame> Cache = nullptr;

	/** 槽位名（进程启动时计算一次） */
	FString SlotName;
};