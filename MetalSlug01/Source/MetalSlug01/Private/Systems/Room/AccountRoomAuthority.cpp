// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 本类自己的声明（会自动包含 .generated.h）
#include "Systems/Room/AccountRoomAuthority.h"
// 引入 ARoomPlayerController 的完整定义（调用 Client_LoginResult RPC）
// 注意：AccountRoomAuthority.h 只有前向声明，此处需要完整类定义
#include "Systems/RoomPlayerController.h"
// 引入引擎的 TimerManager 以做延迟强踢
#include "Engine/World.h"
#include "TimerManager.h"
// 引入 OnlineSessionSettings 完整定义（FNamedOnlineSession 等）
#include "OnlineSessionSettings.h"


// ==========================================
// 1. 构造
// ==========================================

/**
 * UAccountRoomAuthority 构造函数
 * 唯一职责: 初始化空字典（保持 GC 友好）
 */
UAccountRoomAuthority::UAccountRoomAuthority()
{
}


// ==========================================
// 2. 核心接口实现
// ==========================================

/**
 * HandleLoginRequest
 *
 * 房主端处理客户端"上线通知"的统一入口
 * 1. 同 (Username, SessionId) → 视为重连, 刷新 PC 指针, 发送 ok
 * 2. 同 Username 不同 SessionId → 拒绝新 PC 进房(不踢旧 PC), 发送 reject
 * 3. 新账号 → 直接注册, 发送 ok
 *
 * 【新语义】"不允许进房": 旧 PC 不动, 只把新 PC 弹回登录页
 */
bool UAccountRoomAuthority::HandleLoginRequest(
	const FString& Username,
	const FString& SessionId,
	ARoomPlayerController* RequesterPC)
{
	// 输入校验: 账号或 SessionId 为空直接拒绝
	if (Username.IsEmpty() || SessionId.IsEmpty() || RequesterPC == nullptr)
	{
		RequesterPC->Client_LoginResult(false, TEXT("参数错误"), false);
		return false;
	}

	// 情况 1: 完全重连 - 同账号同 SessionId 已存在
	if (FAccountSessionState* Existing = OnlineAccounts.Find(Username))
	{
		if (Existing->SessionId == SessionId)
		{
			// 视为重连, 刷新 PC 指针(可能 PC 被重建过)
			Existing->PC = RequesterPC;
			Existing->LoginAt = FDateTime::Now();
			Existing->LastHeartbeatAt = FDateTime::Now();  // 刷新心跳

			// 给客户端发 ok
			RequesterPC->Client_LoginResult(true, TEXT(""), false);
			return true;
		}

		// 情况 2: 同账号但 SessionId 不同 - 拒绝新 PC 进房
		// 旧 PC 不动(最先登录的账户不受任何处理)
		// 仅告诉新 PC: 此账号已在房间中, 拒绝进房
		const FString RejectReason = FString::Printf(
			TEXT("此账号 [%s] 已在房间中, 不允许重复进房"), *Username);
		RequesterPC->Client_LoginResult(false, RejectReason, true);

		// 注意: 不写入 TMap, 不更新旧 PC 的任何状态
		// TMap 里依然是"最先登录的账户"的会话, 完全不动

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow,
				FString::Printf(TEXT("[Authority] 拒绝新会话: %s (Session=%s)"),
					*Username, *SessionId));
		}
		return false;
	}

	// 情况 3: 新账号,直接注册
	FAccountSessionState NewState;
	NewState.SessionId = SessionId;
	NewState.PC = RequesterPC;
	NewState.LoginAt = FDateTime::Now();
	NewState.LastHeartbeatAt = FDateTime::Now();  // 初始化心跳时间戳
	OnlineAccounts.Add(Username, NewState);

	// 给客户端发 ok
	RequesterPC->Client_LoginResult(true, TEXT(""), false);
	return true;
}


/**
 * HandleLogoutRequest
 *
 * 房主端处理客户端"下线通知"
 * - 仅清理 (Username, SessionId) 完全匹配的条目
 * - 不匹配时不做事(避免误删"重连后产生的新条目")
 */
void UAccountRoomAuthority::HandleLogoutRequest(
	const FString& Username,
	const FString& SessionId)
{
	// 在 TMap 中查找
	if (FAccountSessionState* Existing = OnlineAccounts.Find(Username))
	{
		// 仅在 SessionId 匹配时才清理
		if (Existing->SessionId == SessionId)
		{
			OnlineAccounts.Remove(Username);

			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan,
					FString::Printf(TEXT("[Authority] 账号 %s 已下线"), *Username));
			}
		}
	}
}


/**
 * HandleControllerDestroyed
 *
 * 玩家 PC 销毁时(异常掉线 / 强退)由外部调入
 * 遍历 TMap,清理所有 PC 指针已失效的条目
 */
void UAccountRoomAuthority::HandleControllerDestroyed(ARoomPlayerController* PC)
{
	if (PC == nullptr)
	{
		return;
	}

	// 收集要删除的 Key(不能在遍历中直接删)
	TArray<FString> KeysToRemove;
	for (auto& Pair : OnlineAccounts)
	{
		// 失效的弱引用 或 指向相同 PC 但本函数就是从该 PC 触发的
		if (!Pair.Value.PC.IsValid() || Pair.Value.PC.Get() == PC)
		{
			KeysToRemove.Add(Pair.Key);
		}
	}

	// 二次清理
	for (const FString& Key : KeysToRemove)
	{
		OnlineAccounts.Remove(Key);
	}

	if (GEngine && KeysToRemove.Num() > 0)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow,
			FString::Printf(TEXT("[Authority] 清理掉 %d 个失效账号记录"), KeysToRemove.Num()));
	}
}


/**
 * HandleHeartbeat
 *
 * 客户端心跳到达时刷新 LastHeartbeatAt
 * - 仅刷新 (Username, SessionId) 完全匹配的条目
 * - 不匹配的条目不刷新(避免撞车)
 */
void UAccountRoomAuthority::HandleHeartbeat(const FString& Username, const FString& SessionId)
{
	if (FAccountSessionState* Existing = OnlineAccounts.Find(Username))
	{
		if (Existing->SessionId == SessionId)
		{
			Existing->LastHeartbeatAt = FDateTime::Now();
		}
	}
}


/**
 * SweepDeadSessions
 *
 * 房主端定期调: 清理超时未心跳的会话
 * 触发场景: 客户端进程被强退(X 关闭 / 断电), EndPlay 没机会执行
 *
 * @param TimeoutSeconds 超时阈值(秒)
 * @return 被清理的条目数
 *
 * 注意:
 *  - 超时阈值的选取: 心跳间隔(5s) * 3 = 15s, 给弱网留余量
 *  - 清理时不主动发强踢 RPC(因为 PC 已经没了, 发也白发)
 *  - 清理后该 Username 就可以被新会话注册了
 */
int32 UAccountRoomAuthority::SweepDeadSessions(float TimeoutSeconds)
{
	// 用比当前时间早 N 秒作为截止点
	const FDateTime Cutoff = FDateTime::Now() - FTimespan::FromSeconds(TimeoutSeconds);

	// 收集超时条目
	TArray<FString> KeysToRemove;
	for (auto& Pair : OnlineAccounts)
	{
		// 弱引用已失效 OR 心跳超时
		bool bIsDeadPC = !Pair.Value.PC.IsValid();
		bool bIsTimeout = (Pair.Value.LastHeartbeatAt < Cutoff);

		if (bIsDeadPC || bIsTimeout)
		{
			KeysToRemove.Add(Pair.Key);
		}
	}

	// 清理
	for (const FString& Key : KeysToRemove)
	{
		OnlineAccounts.Remove(Key);
	}

	if (GEngine && KeysToRemove.Num() > 0)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow,
			FString::Printf(TEXT("[Authority] 心跳扫描清理 %d 个超时会话"), KeysToRemove.Num()));
	}

	return KeysToRemove.Num();
}


/**
 * StartSweepTimer
 *
 * 启动房主端心跳扫描定时器
 * - 每 5 秒调一次 SweepDeadSessions(15s 阈值)
 * - 重复调本方法不会重复启定时器(先 ClearTimer 防御)
 *
 * @param World 用来拿 TimerManager
 */
void UAccountRoomAuthority::StartSweepTimer(UWorld* World)
{
	if (!World)
	{
		return;
	}

	// 防御: 防止重复启动
	World->GetTimerManager().ClearTimer(SweepTimerHandle);

	FTimerDelegate SweepDelegate = FTimerDelegate::CreateLambda([WeakAuthority = TWeakObjectPtr<UAccountRoomAuthority>(this)]()
	{
		if (!WeakAuthority.IsValid())
		{
			return;
		}
		WeakAuthority->SweepDeadSessions(15.0f);
	});

	World->GetTimerManager().SetTimer(
		SweepTimerHandle,
		SweepDelegate,
		5.0f,    // 每 5 秒扫一次
		true);   // 循环

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
			TEXT("[Authority] 心跳扫描定时器已启动 (5s 间隔, 15s 超时)"));
	}
}


// ==========================================
// 3. 查询接口
// ==========================================

/**
 * IsAccountOnline
 * 查询某账号当前是否在线(仅查 TMap,不做强踢)
 */
bool UAccountRoomAuthority::IsAccountOnline(const FString& Username) const
{
	return OnlineAccounts.Contains(Username);
}


/**
 * FindControllerByUsername
 * 根据账号名找 PC(用于强踢或调试)
 */
ARoomPlayerController* UAccountRoomAuthority::FindControllerByUsername(const FString& Username) const
{
	if (const FAccountSessionState* Existing = OnlineAccounts.Find(Username))
	{
		ARoomPlayerController* PC = Existing->PC.Get();
		if (IsValid(PC))
		{
			return PC;
		}
	}
	return nullptr;
}


// ==========================================
// 4. 在线账号列表查询与 Session 同步
// ==========================================

/**
 * GetAllOnlineAccountNames
 *
 * 获取当前所有在线账号名列表（不含 HOST_ACCOUNT）
 * @return OnlineAccounts 中所有 Key 的副本
 */
TArray<FString> UAccountRoomAuthority::GetAllOnlineAccountNames() const
{
	TArray<FString> Result;
	OnlineAccounts.GetKeys(Result);
	return Result;
}


/**
 * SyncAccountsToSessionSettings
 *
 * 将当前所有在线账号列表同步写入 SessionSettings 的 ROOM_ACCOUNTS 字段
 * 并调用 UpdateSession 使其广播到大厅，供其他客户端查询
 *
 * 写入格式:
 * - ROOM_ACCOUNTS: 包含 HOST_ACCOUNT 在内的所有在线账号（字符串数组）
 *                  HOST_ACCOUNT 排在第一位（方便客户端扫描）
 *
 * @param SessionInterface 在线会话接口（由调用方从 GameInstance 传入）
 * @param MyAccountName 房主的 HOST_ACCOUNT（不加入 OnlineAccounts，但一并写入 ROOM_ACCOUNTS）
 */
void UAccountRoomAuthority::SyncAccountsToSessionSettings(IOnlineSessionPtr SessionInterface, const FString& MyAccountName)
{
	if (!SessionInterface.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Authority] SyncAccountsToSessionSettings: SessionInterface 无效"));
		return;
	}

	// 从 SessionInterface 拿到当前 NamedSession 的可变引用
	FNamedOnlineSession* NamedSession = SessionInterface->GetNamedSession(NAME_GameSession);
	if (!NamedSession)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Authority] SyncAccountsToSessionSettings: NamedSession 不存在"));
		return;
	}

	// 将账号列表序列化为单条 FString（OnlineSubsystemNull 只导出了 FString 版本的 Set/Get）
	FString AllAccountsStr;
	for (const TPair<FString, FAccountSessionState>& Pair : OnlineAccounts)
	{
		if (!Pair.Key.IsEmpty())
		{
			AllAccountsStr += Pair.Key + TEXT("|");
		}
	}

	// 追加房主账号（放在最前）
	if (!MyAccountName.IsEmpty())
	{
		AllAccountsStr = MyAccountName + TEXT("|") + AllAccountsStr;
	}

	// 写入 ROOM_ACCOUNTS 并广播更新
	NamedSession->SessionSettings.Set(
		FName("ROOM_ACCOUNTS"),
		AllAccountsStr,
		EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// ==========================================
	// 【2026-06-30 P0 Bug2 修复 - 撤销版】此处不再写 TOTAL_PLAYERS_WITH_AI
	//
	// 旧版 (2026-06-29) 我在这里顺手推送了 (AccountCount + 1),
	// 但算法有 bug: AccountCount 起始 1 + 数 '|' 数量 → "host|client|" 数 2 个 '|'
	//   → AccountCount = 1 + 2 = 3 → TotalRealPlayers = 3 + 1 = 4 ❌
	// 即真实人数只有 2 (房主 + 1 普通玩家), 客户端却显示 4
	//
	// 新版: TOTAL_PLAYERS_WITH_AI 由 URoomInsidePage 单一权威推送
	//   - 房主端: RefreshRoomUI 末尾拿 (AttackCount + DefenseCount) 推
	//   - 这个值已经包含 AI (因为 RefreshRoomUI 会创建 AI 标签进 VBox)
	//   - 客户端: URoomService.bIsHost = false, BroadcastRoomPlayerCount 内部护栏静默忽略
	// 这样无论在哪一侧, 都只有一条 UpdateSession 路径, 不会双重计数
	// ==========================================

	// 调用 UpdateSession 将修改推送给局域网内的所有客户端
	SessionInterface->UpdateSession(NAME_GameSession, NamedSession->SessionSettings, true);

	// 简单统计账号数：数管道符数量（末尾空串不计入）
	int32 AccountCount = AllAccountsStr.IsEmpty() ? 0 : 1;
	for (const TCHAR& Ch : AllAccountsStr)
	{
		if (Ch == TEXT('|')) { AccountCount++; }
	}
	UE_LOG(LogTemp, Log, TEXT("[Authority] SyncAccountsToSessionSettings: 已同步 %d 个账号到 ROOM_ACCOUNTS [%s] (TOTAL_PLAYERS_WITH_AI 由 URoomInsidePage 单一推送)"), AccountCount, *AllAccountsStr);
}


// ==========================================
// 5. (旧版强踢流程已删除)
// ==========================================
// 旧版 NotifyForcedKick / DelayedForceLeave 已删除
// 新版语义: 房主端不踢旧 PC, 只拒绝新 PC 进房
// 详见 HandleLoginRequest 的"情况 2"
