// ========================================================================
// AccountSubsystem.h — 本地账号会话层头文件
// ========================================================================
//
// 文件功能总览:
//   - 声明 UAccountSubsystem 类(继承 UGameInstanceSubsystem)
//   - 提供会话层 API: SetCurrentUser / GetCurrentLoggedInUser / ClearSession
//   - 提供会话 Token: GetSessionToken (启动时生成, 全程不变)
//   - 保留 bIsReturningFromRoom (UI 跳转逻辑仍在用)
//   - 保留 MockLoginForTesting (开发期跳过登录页)
//
// 大厂架构角色 — Session Layer (与 Persistence 分离):
//   - 旧实现把"会话状态" (bIsOnline) 写进账号档案, 混了 Session 和 Persistence 两层
//   - 新实现把 bIsOnline 彻底迁移到本 Subsystem 的内存字段
//   - 本类不再做 Load/Save, 不再做账号注册/校验
//   - 只负责:维护当前客户端的"已登录账号" + "会话 Token" + "返回登录清理"
//
// 双开隔离根因修复:
//   - 旧实现每次 Login 都 LoadDataFromDisk + SaveDataToDisk 全表, 双开覆盖
//   - 新实现:每个客户端只关心自己当前登录了谁, 不再做跨进程"互踢"
//   - 互踢能力应该由真正的后端服务提供, 而不是单机 SaveGame
// ========================================================================

// ==========================================
// 头文件包含说明
// ==========================================
#pragma once
// 包含虚幻引擎核心最小化组件
#include "CoreMinimal.h"
// 包含游戏实例子系统的基类头文件
#include "Subsystems/GameInstanceSubsystem.h"
// 自动生成的反射头文件，必须放在最后一行
#include "AccountSubsystem.generated.h"


/**
 * @class UAccountSubsystem
 * @brief 本地账号会话层 (Session Layer)
 *
 * 【大厂架构原则：Session 独立于 Persistence】
 * - 旧实现把"会话状态"(bIsOnline) 写进账号档案, 混了 Session 和 Persistence 两层关注点。
 * - 新实现把 bIsOnline 彻底迁移到本 Subsystem 的内存字段。
 * - 本类不再做 Load/Save, 不再做账号注册/校验, 只负责:
 *     1) 维护当前客户端的"已登录账号" (CurrentUser)
 *     2) 维护当前客户端的"会话 Token" (SessionToken) - 便于未来扩展服务端校验
 *     3) 处理"返回登录"时清理会话状态 (纯内存操作, 不写硬盘)
 *
 * 【双开隔离根因修复】
 * 旧实现每次 Login 都 LoadDataFromDisk + SaveDataToDisk 全表, 导致:
 *   客户端1 登录 alice → slot_0.bIsOnline=true
 *   客户端2 登录 alice → 读 slot_1 → alice.bIsOnline=false → 登录成功
 *   客户端2 写 slot_1.bIsOnline=true (不覆盖 slot_0)
 *   → 两个客户端都认为 alice 在自己这在线
 * 新实现:
 *   每个客户端只关心自己当前登录了谁, 不再做跨进程"互踢"。
 *   互踢能力应该由真正的后端服务提供, 而不是单机 SaveGame。
 *
 * 兼容说明:
 * - 保留 bIsReturningFromRoom 字段 (UI 跳转逻辑仍在用)。
 * - 保留 GetCurrentLoggedInUser / MockLoginForTesting / GetAccountRecord 接口,
 *   让现有蓝图调用零改动。
 * - 不再对外暴露 TryLogin / TryRegister / IsAccountOnline / SaveLastSelectedCharacter 等
 *   持久化相关接口, 这些接口已迁移到 ULocalAccountRepository 和 UAccountService。
 */
UCLASS()
class METALSLUG01_API UAccountSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * 标记玩家是否刚从房间里退出来
	 * 用途: LoginPage 决定是否要跳过大厅
	 */
	UPROPERTY(BlueprintReadWrite, Category = "State")
	bool bIsReturningFromRoom = false;

	// USubsystem 生命周期
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// ==========================================
	// 1. 会话接口 (纯内存, 不写硬盘)
	// ==========================================

	/**
	 * 设置当前会话登录的账号 (内存操作, 不持久化)
	 */
	UFUNCTION(BlueprintCallable, Category = "Account|Session")
	void SetCurrentUser(const FString& Username);

	/**
	 * 获取当前登录的账号名 (无则返回空字符串)
	 */
	UFUNCTION(BlueprintCallable, Category = "Account|Session")
	FString GetCurrentLoggedInUser() const { return CurrentLoggedInUser; }

	/**
	 * 获取当前会话的 Token (GUID, 启动时生成)
	 * 用途: 未来扩展"客户端-服务端握手"时, 用 Token 标识当前会话
	 */
	UFUNCTION(BlueprintCallable, Category = "Account|Session")
	FString GetSessionToken() const { return SessionToken; }

	/**
	 * 清除当前会话 (返回登录界面 / 切换账号前调用)
	 * 注意: 纯内存操作, 不写硬盘。
	 */
	UFUNCTION(BlueprintCallable, Category = "Account|Session")
	void ClearSession();

	// ==========================================
	// 2. 兼容保留接口 (旧蓝图调用点)
	// ==========================================

	/**
	 * 测试用: 跳过登录直接分配临时身份
	 * 保留以便开发期跳过登录页
	 */
	UFUNCTION(BlueprintCallable, Category = "Account|Debug")
	void MockLoginForTesting();

	/**
	 * 占位返回 nullptr, 仅供旧蓝图引用, 真实查询请走 ULocalAccountRepository::FindRecord
	 * (保留是为了不破坏现有 Widget 中可能存在的 GetAccountRecord 调用)
	 */
	const struct FAccountRecord* GetAccountRecord(const FString& AccountName) const;

private:
	/** 当前会话登录的账号 */
	FString CurrentLoggedInUser;

	/** 当前会话 Token (启动时生成, 全程不变) */
	FString SessionToken;
};