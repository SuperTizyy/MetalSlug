// ==========================================
// 头文件包含区
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
	const FString User = GetCurrentUser();
	if (User.IsEmpty()) return TEXT("Default");
	ULocalAccountRepository* Repo = GetRepository();
	return Repo ? Repo->GetLastSelectedCharacter(User) : TEXT("Default");
}

void UAccountService::SaveLastSelectedCharacter(const FString& CharacterName)
{
	const FString User = GetCurrentUser();
	if (User.IsEmpty()) return;
	if (ULocalAccountRepository* Repo = GetRepository())
	{
		Repo->SaveLastSelectedCharacter(User, CharacterName);
	}
}

FString UAccountService::GetLastSelectedWeapon(int32 BackpackSlot) const
{
	const FString User = GetCurrentUser();
	if (User.IsEmpty()) return TEXT("Default");
	ULocalAccountRepository* Repo = GetRepository();
	return Repo ? Repo->GetLastSelectedWeapon(User, BackpackSlot) : TEXT("Default");
}

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
		FlowSubsystem->TransitToState(EMatchState::MainMenu);
	}
}