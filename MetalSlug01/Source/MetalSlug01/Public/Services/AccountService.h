// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AccountService.generated.h"

/**
 * @enum EAccountLoginResult
 * @brief 账号登录结果（V 层友好枚举）
 */
UENUM(BlueprintType)
enum class EAccountLoginResult : uint8
{
	Success             UMETA(DisplayName = "Success"),
	InvalidCredentials  UMETA(DisplayName = "Invalid Username or Password"),
	AccountOnline       UMETA(DisplayName = "Account Already Online"),
	EmptyInput          UMETA(DisplayName = "Empty Username or Password"),
	InternalError       UMETA(DisplayName = "Internal Error")
};

/**
 * @class UAccountService
 * @brief 账号业务服务（V 层的统一入口）
 *
 * 【大厂架构原则：L2 Service Layer - V 层的统一门面】
 * - V 层（LoginPage/GameMenuPage）只调 AccountService，不直接调 AccountSubsystem
 * - AccountService 内部委托 AccountSubsystem 执行业务
 * - 返回 V 层友好枚举（EAccountLoginResult）而非 bool（避免 V 层要懂内部细节）
 *
 * 拆分背景:
 * - AccountSubsystem 是 L1 数据+业务混合层（含 LoadDataFromDisk 等底层）
 * - V 层不应该知道"数据如何加载"
 * - 通过 AccountService 隔离，V 层只关心"做什么"
 *
 * 【大厂对应】
 * - Clean Architecture 的 UseCase 层
 * - CQRS 的 Command 端
 */
UCLASS()
class METALSLUG01_API UAccountService : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ==========================================
	// 静态访问器（V 层调用入口）
	// ==========================================

	UFUNCTION(BlueprintCallable, Category = "AccountService", meta = (WorldContext = "WorldContextObject"))
	static UAccountService* Get(const UObject* WorldContextObject);

	// ==========================================
	// 业务接口
	// ==========================================

	/**
	 * 登录（V 层调用入口）
	 * @return 登录结果枚举（V 层根据枚举显示不同提示）
	 */
	UFUNCTION(BlueprintCallable, Category = "AccountService")
	EAccountLoginResult Login(const FString& Username, const FString& Password);

	/**
	 * 注册
	 * @return true=成功，false=账号已存在
	 */
	UFUNCTION(BlueprintCallable, Category = "AccountService")
	bool Register(const FString& Username, const FString& Password);

	/**
	 * 登出（解锁账号，让其他客户端可登录）
	 */
	UFUNCTION(BlueprintCallable, Category = "AccountService")
	void Logout();

	/**
	 * 获取当前登录的用户名
	 */
	UFUNCTION(BlueprintCallable, Category = "AccountService")
	FString GetCurrentUser() const;

	// ==========================================
	// V 层偏好查询接口（避免 V 层直接调 AccountSubsystem）
	// ==========================================

	UFUNCTION(BlueprintCallable, Category = "AccountService")
	FString GetLastSelectedCharacter() const;

	UFUNCTION(BlueprintCallable, Category = "AccountService")
	void SaveLastSelectedCharacter(const FString& CharacterName);

	UFUNCTION(BlueprintCallable, Category = "AccountService")
	FString GetLastSelectedWeapon(int32 BackpackSlot) const;

	UFUNCTION(BlueprintCallable, Category = "AccountService")
	void SaveLastSelectedWeapon(int32 BackpackSlot, const FString& WeaponRowName);

private:
	/** 登录成功时切换状态（V 层无感知） */
	void OnLoginSuccess();
};