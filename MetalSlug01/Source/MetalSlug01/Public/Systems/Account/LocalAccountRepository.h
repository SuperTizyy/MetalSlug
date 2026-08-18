// ========================================================================
// LocalAccountRepository.h — L1 仓储层 - 账号 CRUD 头文件
// ========================================================================
//
// 文件功能总览:
//   - 声明 EAccountRegisterResult 枚举 (仓储层友好枚举)
//   - 声明 ULocalAccountRepository 类(继承 UGameInstanceSubsystem)
//   - 提供账号 CRUD 接口: VerifyCredentials / Register / FindRecord
//   - 提供战备偏好接口: Get/SaveLastSelectedCharacter / Weapon
//
// 大厂架构角色 — Repository Pattern (业务层 + 数据层之间):
//   - 上层 Service 通过 Repository 操作账号数据, 不直接接触 USaveGame
//   - Repository 内部委托 Store (ULocalAccountStore) 完成物理 IO
//   - 业务校验(密码匹配、用户名查重)都在 Repository 完成
//
// 关键变化(移除 IsAccountOnline / bIsOnline 字段):
//   - 双开场景下每个客户端是独立进程, 物理上不可见彼此的内存状态
//   - "账号是否在线" 必须是后端权威, 客户端无法独立判断
//   - 本地仓储层只回答"这个客户端本地注册过哪些账号, 哪个账号存在, 密码对不对"
//
// 自愈式 Save 设计:
//   - SaveLastSelectedCharacter/Weapon 在 Record 不存在时自动创建(用于 MockLogin 等临时账号)
//   - 关键修复:RoomInsidePage::InitDefaultWeapons 依赖 SaveLastSelectedWeapon 写入默认值
// ========================================================================

// ==========================================
// 头文件包含说明
// ==========================================
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Data/Account/AccountSaveTypes.h"
#include "LocalAccountRepository.generated.h"

class ULocalAccountStore;


/**
 * @enum EAccountRegisterResult
 * @brief 注册结果（仓储层友好枚举）
 */
UENUM(BlueprintType)
enum class EAccountRegisterResult : uint8
{
	Success         UMETA(DisplayName = "Success"),
	AlreadyExists   UMETA(DisplayName = "Account Already Exists"),
	InvalidInput    UMETA(DisplayName = "Invalid Input"),
	InternalError   UMETA(DisplayName = "Internal Error")
};

/**
 * @brief 错误兜底枚举值 — 仓储层内部未识别错误时返回, Service 层映射到登录结果
 */


/**
 * @class ULocalAccountRepository
 * @brief L1 仓储层 - 账号 CRUD + 战备偏好的业务接口
 *
 * 【大厂架构原则：Repository Pattern】
 * - 上层 Service 通过 Repository 操作账号数据, 不直接接触 USaveGame。
 * - Repository 内部委托 Store 完成物理 IO。
 * - 业务校验（密码匹配、用户名查重）都在 Repository 完成。
 *
 * 【关键变化】移除 IsAccountOnline / bIsOnline 字段
 * 原因:
 *   - 双开场景下每个客户端是独立进程, 物理上不可见彼此的内存状态。
 *   - "账号是否在线" 必须是后端权威, 客户端无法独立判断。
 *   - 本地仓储层只回答"这个客户端本地注册过哪些账号, 哪个账号存在, 密码对不对"。
 */
UCLASS()
class METALSLUG01_API ULocalAccountRepository : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * @brief Subsystem 启动时自动调用 — 自动注入 SessionManagerSubsystem 依赖
	 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/**
	 * @brief Subsystem 销毁时自动调用 — 解绑所有 SessionManager 委托
	 */
	virtual void Deinitialize() override;

	// ==========================================
	// 静态访问器
	// ==========================================

	UFUNCTION(BlueprintCallable, Category = "Account|Repository", meta = (WorldContext = "WorldContextObject"))
	static ULocalAccountRepository* Get(const UObject* WorldContextObject);

	// ==========================================
	// 账号 CRUD
	// ==========================================

	/**
	 * 校验账号是否存在且密码匹配
	 * @return true = 本地存在且密码正确
	 */
	UFUNCTION(BlueprintCallable, Category = "Account|Repository")
	bool VerifyCredentials(const FString& Username, const FString& Password) const;

	/**
	 * 注册新账号
	 */
	UFUNCTION(BlueprintCallable, Category = "Account|Repository")
	EAccountRegisterResult Register(const FString& Username, const FString& Password);

	/**
	 * 查询账号档案（只读视图，外部不要写）
	 *
	 * 【架构约束】本接口为 C++ Only:
	 *   UHT 不允许 UFUNCTION 返回 USTRUCT* (会报 "Inappropriate '*' on variable of type ...")
	 *   蓝图请走 UAccountService 提供的字符串型偏好查询接口 (GetLastSelectedCharacter / Weapon)。
	 */
	const FAccountRecord* FindRecord(const FString& Username) const;

	// ==========================================
	// 战备偏好 (Repository 层统一管理)
	// ==========================================

	UFUNCTION(BlueprintCallable, Category = "Account|Repository")
	FString GetLastSelectedCharacter(const FString& Username) const;

	/**
	 * @brief 按 Username + BackpackSlot 持久化武器选择 (Record 不存在自动创建)
	 *
	 * 自愈式 Save: 不存在账号 → 自动创建空 Record → 写入默认值
	 * 这是 RoomInsidePage::InitDefaultWeapons 的依赖路径
	 */
	UFUNCTION(BlueprintCallable, Category = "Account|Repository")
	void SaveLastSelectedCharacter(const FString& Username, const FString& CharacterName);

	UFUNCTION(BlueprintCallable, Category = "Account|Repository")
	FString GetLastSelectedWeapon(const FString& Username, int32 BackpackSlot) const;

	UFUNCTION(BlueprintCallable, Category = "Account|Repository")
	void SaveLastSelectedWeapon(const FString& Username, int32 BackpackSlot, const FString& WeaponRowName);

private:
	/** 获取本进程专属 Store (懒加载防御) */
	ULocalAccountStore* GetStore() const;

	/**
	 * 兜底保存:
	 * 任何对账号记录的修改, 都通过该函数走一次 FlushToDisk。
	 * Repository 不在内存里冗余存 AccountData, 直接复用 Store 的 Cache。
	 */
	void Persist();
};