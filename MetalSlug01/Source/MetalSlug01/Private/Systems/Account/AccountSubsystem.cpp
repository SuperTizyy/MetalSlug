// ========================================================================
// AccountSubsystem.cpp — 本地账号会话层实现文件
// ========================================================================
//
// 文件功能总览:
//   - 实现 Initialize: 进程启动时生成 SessionToken (GUID)
//   - 实现 SetCurrentUser / ClearSession: 纯内存操作,不写硬盘
//   - 实现 MockLoginForTesting: 测试模式临时身份 (随机名 + Repository 自愈式建档)
//   - 实现 GetAccountRecord: 占位返回 nullptr, 兼容旧蓝图调用点
//
// 大厂原则:
//   - Session 零持久化:任何修改都不调用 SaveDataToDisk
//   - 测试账号隔离:MockName 是内存临时账号,关闭游戏后自动销毁,不污染真实存档
//   - 自愈式建档:MockLogin 时同步在 Repository 创建空 Record,避免下游 GetLastSelected 找不到
// ========================================================================

// ==========================================
// 头文件包含区
// ==========================================
// 包含当前子系统的头文件
#include "Systems/Account/AccountSubsystem.h"
// 仓储层 (用于测试身份自愈式创建 Record)
#include "Systems/Account/LocalAccountRepository.h"
// GUID 生成
#include "Misc/Guid.h"
// 屏幕调试
#include "Engine/Engine.h"
// GameInstance (用于获取 Subsystem)
#include "Engine/GameInstance.h"


// ==========================================
// 1. 生命周期
// ==========================================

void UAccountSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 进程启动时生成一次会话 Token, 整个客户端生命周期不变
	SessionToken = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);

	UE_LOG(LogTemp, Log, TEXT("[AccountSession] Initialize 完成, SessionToken=%s"),
		*SessionToken.Right(8));
}


// ==========================================
// 2. 会话操作 (纯内存, 不写硬盘)
// ==========================================

void UAccountSubsystem::SetCurrentUser(const FString& Username)
{
	CurrentLoggedInUser = Username;
	UE_LOG(LogTemp, Log, TEXT("[AccountSession] SetCurrentUser = %s"), *Username);
}

/**
 * @brief 清空当前登录会话(纯内存, 不写硬盘)
 *
 * 仅把 CurrentLoggedInUser 置空, 用于登出/账号切换场景
 * 不触发任何磁盘 IO (账号持久化由 Repository 层负责, 不在 Session 层)
 */
void UAccountSubsystem::ClearSession()
{
	const FString Old = CurrentLoggedInUser;
	CurrentLoggedInUser.Empty();
	if (!Old.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("[AccountSession] ClearSession, 旧用户 = %s"), *Old);
	}
}


// ==========================================
// 3. 调试接口 (跳过登录)
// ==========================================

void UAccountSubsystem::MockLoginForTesting()
{
	// 1. 生成一个四位随机十六进制后缀的名字，例如 "TestUser_A3F1"
	// 这样多开客户端时，每个窗口都会获得不同的身份，联机逻辑(如踢人)就不会因重名而崩溃
	const FString MockName = FString::Printf(TEXT("TestUser_%04X"), FMath::RandRange(0, 0xFFFF));

	// 2. 把当前窗口的登录人设置为这个临时账号
	CurrentLoggedInUser = MockName;

	// 3. 【架构升级】同步在 Repository 里创建一条空 Record, 让偏好查询/写入链路自洽
	//
	// 设计动机 (2026-07-03 bug fix):
	//   - 之前 MockLogin 只写 Session.CurrentLoggedInUser, 不写 Repository.AccountRecords
	//   - 导致 AccountService::GetLastSelectedWeapon(...) 永远返回 "" (Record 找不到)
	//   - 房间页面 InitDefaultWeapons 拿不到默认武器 → PC->Server_SelectLoadout 传空串
	//   - GM 兜底 "Knife" → DT_WeaponInfo 找不到 Knife 行 → 武器不显示
	//
	// 现在做法:
	//   - 利用 Repository 自愈式 Save (Record 不存在则自动创建)
	//   - 模拟"用户首次登录, 系统自动建档"的真实业务流
	//   - 因为 MockName 是内存临时账号, 关闭游戏后随进程销毁, 不污染真实存档
	if (UGameInstance* GI = GetGameInstance())
	{
		if (ULocalAccountRepository* Repo = GI->GetSubsystem<ULocalAccountRepository>())
		{
			// 触发自愈式 Save, 自动建档 (空字符串 = "尚未选择")
			Repo->SaveLastSelectedCharacter(MockName, TEXT(""));
			Repo->SaveLastSelectedWeapon(MockName, 1, TEXT(""));
			Repo->SaveLastSelectedWeapon(MockName, 2, TEXT(""));

			UE_LOG(LogTemp, Log,
				TEXT("[AccountSession] MockLoginForTesting: 已在 Repository 创建临时 Record (User='%s')"),
				*MockName);
		}
	}

	// 4. 【核心细节】: 我们故意【不调用】任何 SaveDataToDisk()
	// 临时测试账号只存在于本次运行的内存中, 关闭游戏后自动销毁, 绝不污染真实存档
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Green,
			FString::Printf(TEXT("[测试模式] 分配临时身份: %s"), *MockName));
	}
}


// ==========================================
// 4. 兼容占位
// ==========================================

const FAccountRecord* UAccountSubsystem::GetAccountRecord(const FString& AccountName) const
{
	// 旧接口已废弃: 真实查询请走 ULocalAccountRepository::FindRecord
	// 这里返回 nullptr 是为了让旧蓝图"if (Record != nullptr)" 逻辑不会崩
	UE_LOG(LogTemp, Verbose,
		TEXT("[AccountSession] GetAccountRecord 已废弃, 请改用 ULocalAccountRepository::FindRecord"));
	return nullptr;
}