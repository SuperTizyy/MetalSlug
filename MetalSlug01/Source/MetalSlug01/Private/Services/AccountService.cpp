// ==========================================
// AccountService.cpp — 账号业务服务实现文件
// ==========================================
//
// 文件功能总览:
//   - 实现 Login / Register / Logout / GetCurrentUser 业务接口
//   - 实现偏好查询(角色/武器)的 V 层接口(委托给 Repository)
//   - 实现 GetRepository 私有 helper(懒加载防御)
//   - 实现 OnLoginSuccess 私有 helper(写会话 + 切状态)
//
// 大厂原则:
//   - V 层零感知:V 层不直接调 Repository/Subsystem,只调本 Service
//   - 零兜底(用户偏好):配错/无存档一律返回空串,而不是 "Default" 占位字符串(避免污染 Spawn)
// ==========================================
#include "Services/AccountService.h"
#include "Systems/Account/AccountSubsystem.h"
#include "Systems/Account/LocalAccountRepository.h"
#include "Systems/GameFlowSubsystem.h"
#include "Engine/GameInstance.h"


// ==========================================
// 1. 静态访问器
// ==========================================

UAccountService* UAccountService::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = WorldContextObject->GetWorld();
	if (!World) return nullptr;
	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return nullptr;
	return GI->GetSubsystem<UAccountService>();
}


// ==========================================
// 2. 业务接口
// ==========================================

EAccountLoginResult UAccountService::Login(const FString& Username, const FString& Password)
{
	// 2.1 前置校验: 输入
	if (Username.IsEmpty() || Password.IsEmpty())
	{
		return EAccountLoginResult::EmptyInput;
	}

	// 2.2 拿到 Repository (业务层)
	ULocalAccountRepository* Repo = GetRepository();
	if (!Repo) return EAccountLoginResult::InternalError;

	// 2.3 业务校验: 本地是否存在 + 密码是否匹配
	// 【关键】这里不再做"账号是否在线" 的本地判断
	// 因为双开客户端是独立 .sav, 本地判断必然错误, 应留给后端权威
	if (!Repo->VerifyCredentials(Username, Password))
	{
		return EAccountLoginResult::InvalidCredentials;
	}

	// 2.4 业务成功: 写会话 + 切状态
	OnLoginSuccess(Username);
	return EAccountLoginResult::Success;
}

bool UAccountService::Register(const FString& Username, const FString& Password)
{
	ULocalAccountRepository* Repo = GetRepository();
	if (!Repo) return false;

	const EAccountRegisterResult Result = Repo->Register(Username, Password);
	return Result == EAccountRegisterResult::Success;
}

/**
 * @brief 登出 — 清空会话并将全局状态机切回 Login
 *
 * 两段顺序保证:先清空 AccountSubsystem 会话(纯内存),再切 GameFlow 状态
 * 切状态后 UIViewService 监听会销毁主菜单页面并显示登录页
 */
void UAccountService::Logout()
{
	// 1. 清理会话 (纯内存, 不写硬盘)
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* Session = GI->GetSubsystem<UAccountSubsystem>())
		{
			Session->ClearSession();
		}
	}

	// 2. 状态机回到 Login
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
		{
			FlowSubsystem->TransitToState(EMatchState::Login);
		}
	}
}

/**
 * @brief 获取当前已登录的用户名(从 AccountSubsystem 会话读)
 * @return 当前用户名,未登录或子系统缺失返回空串
 *
 * 零兜底:返回空串表示"未登录",调用方应自行判断是否需要显示登录页
 */
FString UAccountService::GetCurrentUser() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* Session = GI->GetSubsystem<UAccountSubsystem>())
		{
			return Session->GetCurrentLoggedInUser();
		}
	}
	return TEXT("");
}


// ==========================================
// 3. 偏好查询 (委托给 Repository)
// ==========================================

FString UAccountService::GetLastSelectedCharacter() const
{
	// 【v213+ 大厂架构修复 — 消除默认污染源】
	//   旧版: User 空或 Repo 缺失时返回 TEXT("Default") → 占位字符串污染下游 Spawn 链
	//   新版: 返回空串 → 与 PS/Init 默认值一致 → 不会污染 Spawn
	//   大厂原则 - 零兜底: "没用户/没存档" 必须明确返回空, 而不是返回魔法字符串
	const FString User = GetCurrentUser();
	if (User.IsEmpty()) return TEXT("");
	ULocalAccountRepository* Repo = GetRepository();
	return Repo ? Repo->GetLastSelectedCharacter(User) : TEXT("");
}

/**
 * @brief 保存当前用户上次选择的角色名到本地仓库
 * @param CharacterName 角色 RowName(对应 DT_CharacterInfo)
 *
 * 用户未登录或 Repo 缺失时静默跳过,不报错(偏好持久化非关键路径)
 */
void UAccountService::SaveLastSelectedCharacter(const FString& CharacterName)
{
	const FString User = GetCurrentUser();
	if (User.IsEmpty()) return;
	if (ULocalAccountRepository* Repo = GetRepository())
	{
		Repo->SaveLastSelectedCharacter(User, CharacterName);
	}
}

/**
 * @brief 获取指定背包槽位上次选择的武器 RowName
 * @param BackpackSlot 背包槽位索引
 * @return 武器 RowName,用户未登录/无存档/Repo 缺失返回空串
 *
 * 大厂原则 - 零兜底:统一返回空串而非魔法默认值,避免污染下游 Spawn 链
 */
FString UAccountService::GetLastSelectedWeapon(int32 BackpackSlot) const
{
	// 【v213+ 大厂架构修复 — 消除默认污染源】
	//   旧版: User 空或 Repo 缺失时返回 TEXT("Default") → 占位字符串污染下游 Spawn 链
	//   新版: 返回空串 → 与 PS/Init 默认值一致 → 不会污染 Spawn
	const FString User = GetCurrentUser();
	if (User.IsEmpty()) return TEXT("");
	ULocalAccountRepository* Repo = GetRepository();
	return Repo ? Repo->GetLastSelectedWeapon(User, BackpackSlot) : TEXT("");
}

/**
 * @brief 保存指定背包槽位的武器 RowName 到本地仓库
 * @param BackpackSlot 背包槽位索引
 * @param WeaponRowName 武器 RowName(对应 DT_WeaponInfo)
 *
 * 用户未登录或 Repo 缺失时静默跳过,不影响主流程
 */
void UAccountService::SaveLastSelectedWeapon(int32 BackpackSlot, const FString& WeaponRowName)
{
	const FString User = GetCurrentUser();
	if (User.IsEmpty()) return;
	if (ULocalAccountRepository* Repo = GetRepository())
	{
		Repo->SaveLastSelectedWeapon(User, BackpackSlot, WeaponRowName);
	}
}


// ==========================================
// 4. 私有工具
// ==========================================

ULocalAccountRepository* UAccountService::GetRepository() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<ULocalAccountRepository>();
	}
	return nullptr;
}

void UAccountService::OnLoginSuccess(const FString& Username)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) return;

	// 1) 写会话
	if (UAccountSubsystem* Session = GI->GetSubsystem<UAccountSubsystem>())
	{
		Session->SetCurrentUser(Username);
	}

	// 2) 状态机跳转到主菜单
	// UIViewService 监听状态变化 → 销毁 LoginPage + 创建 GameMenuPage
	if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
	{
		FlowSubsystem->TransitToState(EMatchState::MainMenu, TEXT("AccountService::OnLoginSuccess"));
	}
}