// ==========================================
// 头文件包含说明
// ==========================================
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AccountService.generated.h"

class ULocalAccountRepository;


/**
 * @enum EAccountLoginResult
 * @brief 账号登录结果（V 层友好枚举）
 */
UENUM(BlueprintType)
enum class EAccountLoginResult : uint8
{
	Success             UMETA(DisplayName = "Success"),
	InvalidCredentials  UMETA(DisplayName = "Invalid Username or Password"),
	EmptyInput          UMETA(DisplayName = "Empty Username or Password"),
	AccountLocked       UMETA(DisplayName = "Account Locked (server-side)"),
	InternalError       UMETA(DisplayName = "Internal Error")
};


/**
 * @class UAccountService
 * @brief 账号业务服务（V 层的统一门面）
 *
 * 【大厂架构原则：V 层只调 Service，不直接调 Subsystem / Repository】
 * - Login / Register / Logout / GetCurrentUser 是 V 层唯一允许调用的入口。
 * - Service 内部编排: Repository (业务校验) + AccountSubsystem (会话状态) + GameFlowSubsystem (状态机)。
 *
 * 【双开隔离根因修复】
 * - 旧 Service 调用 AccountSubsystem->IsAccountOnline(), 走的是"读 .sav 全表再判断 bIsOnline" 的伪互踢逻辑。
 *   因为每个客户端是独立 .sav, 互踢完全失效。
 * - 新 Service 不再做"账号是否在线" 的本地判断; 如未来需要, 应该问后端, 不再问本地 SaveGame。
 */
UCLASS()
class METALSLUG01_API UAccountService : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ==========================================
	// 静态访问器
	// ==========================================

	UFUNCTION(BlueprintCallable, Category = "AccountService", meta = (WorldContext = "WorldContextObject"))
	static UAccountService* Get(const UObject* WorldContextObject);

	// ==========================================
	// 业务接口 (V 层唯一入口)
	// ==========================================

	/**
	 * 登录 (V 层调用入口)
	 * @return 登录结果枚举（V 层根据枚举显示不同提示）
	 */
	UFUNCTION(BlueprintCallable, Category = "AccountService")
	EAccountLoginResult Login(const FString& Username, const FString& Password);

	/**
	 * 注册 (V 层调用入口)
	 * @return true = 成功, false = 失败 (账号已存在 / 输入非法)
	 */
	UFUNCTION(BlueprintCallable, Category = "AccountService")
	bool Register(const FString& Username, const FString& Password);

	/**
	 * 登出 (清理会话 + 跳转回登录页面)
	 */
	UFUNCTION(BlueprintCallable, Category = "AccountService")
	void Logout();

	/**
	 * 获取当前登录的用户名 (空字符串表示未登录)
	 */
	UFUNCTION(BlueprintCallable, Category = "AccountService")
	FString GetCurrentUser() const;

	// ==========================================
	// V 层偏好查询接口 (避免 V 层直接调 Repository)
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
	/** 获取本进程内的 Repository (懒加载防御) */
	ULocalAccountRepository* GetRepository() const;

	/** 登录成功时切换 UI 状态 */
	void OnLoginSuccess(const FString& Username);
};