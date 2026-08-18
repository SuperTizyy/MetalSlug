// ==========================================
// AccountService.h — 账号业务服务头文件
// ==========================================
//
// 文件功能总览:
//   - 声明 EAccountLoginResult 枚举(登录结果 V 层友好表达)
//   - 声明 UAccountService 类(继承 UGameInstanceSubsystem),为 V 层提供
//     登录/注册/登出/查询用户/查询偏好(角色/武器)的统一门面
//
// 大厂架构角色:
//   - L2 Service Layer(门面层):V 层只调 Service,不直接调 Subsystem/Repository
//   - Service 内部编排:Repository(业务校验) + AccountSubsystem(会话状态) + GameFlowSubsystem(状态机)
//   - V 层零感知:UI / Presenter 都不感知底层数据层细节
//
// 双开隔离设计(2026.07+ 重构):
//   - 旧 Service 调 AccountSubsystem->IsAccountOnline() 走"读 .sav 全表再判断" 的伪互踢逻辑
//   - 新 Service 不做"账号是否在线" 的本地判断; 如未来需要, 应该问后端, 不再问本地 SaveGame
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
 * @brief 错误兜底枚举值 — 服务内部未识别错误时返回, V 层显示 "Internal Error" 提示
 */


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

	/**
	 * @brief 持久化玩家上次选择的角色 ID 到 .sav
	 *
	 * V 层在玩家选角色 UI 确认时调用 — 玩家下次启动游戏,GetLastSelectedCharacter 自动恢复
	 * 不抛异常: 写入失败 → Log Error + return (让 V 层继续走默认路径)
	 *
	 * @param CharacterName 角色 RowName (DT_CharacterInfo 的 RowName, 不是显示名)
	 */
	UFUNCTION(BlueprintCallable, Category = "AccountService")
	void SaveLastSelectedCharacter(const FString& CharacterName);

	/**
	 * @brief 读取背包槽位的上次选择武器 ID (默认槽位 0 = 主武器, 1 = 副武器, 2 = 近战)
	 *
	 * 返回空字符串表示槽位无记录,V 层应走"未选择武器" 路径
	 *
	 * @param BackpackSlot 槽位索引 (0=主武器, 1=副武器, 2=近战)
	 * @return 武器 RowName (DT_WeaponInfo 的 RowName)
	 */
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