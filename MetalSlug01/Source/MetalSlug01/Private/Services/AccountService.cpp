// 版权声明：在项目设置的描述页面填写您的版权信息。

#include "Services/AccountService.h"
#include "Systems/Account/AccountSubsystem.h"
#include "Systems/GameFlowSubsystem.h"
#include "Engine/GameInstance.h"

// ==========================================
// 静态访问器
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
// 业务接口
// ==========================================

EAccountLoginResult UAccountService::Login(const FString& Username, const FString& Password)
{
	// 业务前置校验（V 层无感）
	if (Username.IsEmpty() || Password.IsEmpty())
	{
		return EAccountLoginResult::EmptyInput;
	}

	UGameInstance* GI = GetGameInstance();
	if (!GI) return EAccountLoginResult::InternalError;

	UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>();
	if (!AccountSub) return EAccountLoginResult::InternalError;

	// 业务校验：账号是否在线
	if (AccountSub->IsAccountOnline(Username))
	{
		return EAccountLoginResult::AccountOnline;
	}

	// 业务校验：账号密码是否正确
	if (!AccountSub->TryLogin(Username, Password))
	{
		return EAccountLoginResult::InvalidCredentials;
	}

	// 登录成功：触发状态切换（V 层完全无感知）
	OnLoginSuccess();
	return EAccountLoginResult::Success;
}

bool UAccountService::Register(const FString& Username, const FString& Password)
{
	if (Username.IsEmpty() || Password.IsEmpty()) return false;

	UGameInstance* GI = GetGameInstance();
	if (!GI) return false;

	UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>();
	if (!AccountSub) return false;

	return AccountSub->TryRegister(Username, Password);
}

void UAccountService::Logout()
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) return;

	if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
	{
		AccountSub->Logout();
	}

	// 触发状态切换到 Login
	if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
	{
		FlowSubsystem->TransitToState(EMatchState::Login);
	}
}

FString UAccountService::GetCurrentUser() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			return AccountSub->GetCurrentLoggedInUser();
		}
	}
	return TEXT("");
}

// ==========================================
// 偏好查询（V 层无感地访问持久化偏好）
// ==========================================

FString UAccountService::GetLastSelectedCharacter() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			return AccountSub->GetLastSelectedCharacter();
		}
	}
	return TEXT("Default");
}

void UAccountService::SaveLastSelectedCharacter(const FString& CharacterName)
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			AccountSub->SaveLastSelectedCharacter(CharacterName);
		}
	}
}

FString UAccountService::GetLastSelectedWeapon(int32 BackpackSlot) const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			return AccountSub->GetLastSelectedWeapon(BackpackSlot);
		}
	}
	return TEXT("Default");
}

void UAccountService::SaveLastSelectedWeapon(int32 BackpackSlot, const FString& WeaponRowName)
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			AccountSub->SaveLastSelectedWeapon(BackpackSlot, WeaponRowName);
		}
	}
}

// ==========================================
// 私有
// ==========================================

void UAccountService::OnLoginSuccess()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
		{
			// 登录成功 → 跳转到主菜单状态
			// UIViewService 监听状态变化 → 销毁 LoginPage + 创建 GameMenuPage
			FlowSubsystem->TransitToState(EMatchState::MainMenu);
		}
	}
}