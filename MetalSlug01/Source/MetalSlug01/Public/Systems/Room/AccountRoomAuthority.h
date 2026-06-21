// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "AccountRoomAuthority.generated.h"

class APlayerController;
class ARoomPlayerController;

/**
 * @struct FAccountSessionState
 * @brief 单个玩家在 ListenServer 上的"账号在线状态"记录
 *
 * 字段说明:
 * - SessionId: 客户端在进房时本地生成的 FGuid，用于"重连"判定
 *              同一个 (Username, SessionId) 多次到达只算 1 次
 * - PC: 指向该玩家当前的 PlayerController（用于反向寻址做"强踢"）
 * - LoginAt: 上线时间戳（调试用）
 */
USTRUCT(BlueprintType)
struct METALSLUG01_API FAccountSessionState
{
	GENERATED_BODY()

	UPROPERTY()
	FString SessionId;

	UPROPERTY()
	TWeakObjectPtr<ARoomPlayerController> PC;

	UPROPERTY()
	FDateTime LoginAt;

	UPROPERTY()
	FDateTime LastHeartbeatAt;

	FAccountSessionState()
		: SessionId(TEXT(""))
		, PC(nullptr)
		, LoginAt(FDateTime::MinValue())
		, LastHeartbeatAt(FDateTime::MinValue())
	{
	}
};

/**
 * @class UAccountRoomAuthority
 * @brief 房主端（ListenServer）上的"账号在线状态权威表"
 */
UCLASS(BlueprintType)
class METALSLUG01_API UAccountRoomAuthority : public UObject
{
	GENERATED_BODY()

public:
	UAccountRoomAuthority();

	// ==========================================
	// 对外接口（仅房主端调用）
	// ==========================================

	/**
	 * 处理客户端发来的"上线通知"
	 * @param Username 玩家账号
	 * @param SessionId 客户端本地生成的 FGuid
	 * @param RequesterPC 发起请求的 PlayerController
	 * @return true=正常注册, false=需要强踢（但内部已发好强踢通知）
	 *
	 * 流程:
	 * 1. 查 TMap 是否有 (Username, SessionId) 相同的条目
	 *    - 相同: 视为重连,刷新 PC 指针,返回 true
	 * 2. 查 TMap 是否有同 Username 但不同 SessionId 的条目
	 *    - 有: 给旧 PC 发 Client_LoginResult(forced_kick=true), 等待 0.5s 旧 PC 退房
	 *          然后用新 SessionId 覆盖注册,返回 false（调用方不要重复发 ok）
	 * 3. 没有: 新增条目,返回 true
	 */
	bool HandleLoginRequest(const FString& Username, const FString& SessionId, ARoomPlayerController* RequesterPC);

	/**
	 * 处理客户端发来的"下线通知"
	 * @param Username 玩家账号
	 * @param SessionId 客户端本地生成的 FGuid（仅清理匹配项,避免误删）
	 */
	void HandleLogoutRequest(const FString& Username, const FString& SessionId);

	/**
	 * 房主端主动清理: 玩家 PC 销毁时同步调用
	 * 用法: 监听 PC 的 EndPlay 委托,触发本函数防止 TMap 残留
	 */
	void HandleControllerDestroyed(ARoomPlayerController* PC);

	/**
	 * 客户端心跳: 刷新 LastHeartbeatAt
	 * 房主定期调 SweepDeadSessions() 检测超时未心跳的 PC 并清理
	 * @param Username 玩家账号
	 * @param SessionId 客户端 FGuid(只刷新匹配项,避免撞车)
	 */
	void HandleHeartbeat(const FString& Username, const FString& SessionId);

	/**
	 * 房主端定期调用: 清理超时未心跳的会话
	 * @param TimeoutSeconds 超时阈值(秒),超过则视为掉线
	 * @return 被清理的账号数
	 */
	int32 SweepDeadSessions(float TimeoutSeconds = 15.0f);

	/**
	 * 查询某个账号是否在线（用于内部 RPC Server_RequestIsOnline）
	 */
	bool IsAccountOnline(const FString& Username) const;

	/**
	 * 根据账号名找到对应的 PC（用于强踢）
	 */
	ARoomPlayerController* FindControllerByUsername(const FString& Username) const;

	/**
	 * 获取当前所有在线账号名列表（供 UI/外部同步 SessionSettings 使用）
	 * @return 账号名数组（不含 HOST_ACCOUNT，它由创房逻辑单独维护）
	 */
	TArray<FString> GetAllOnlineAccountNames() const;

	/**
	 * 将当前在线账号列表同步写入 SessionSettings 并调用 UpdateSession
	 * 由 RoomPlayerController 在每次 HandleLoginRequest / HandleLogoutRequest 后调用
	 * @param SessionInterface 在线会话接口（由调用方从 GameInstance 传入）
	 * @param MyAccountName 房主的 HOST_ACCOUNT（不加入 OnlineAccounts，但一并写入 ROOM_ACCOUNTS）
	 */
	void SyncAccountsToSessionSettings(IOnlineSessionPtr SessionInterface, const FString& MyAccountName);

	/**
	 * 调试用: 获取在线账号数量
	 */
	int32 GetOnlineCount() const { return OnlineAccounts.Num(); }

	/**
	 * 启动房主端心跳扫描定时器
	 * 用法: 房主在拿到权威表后调一次, 每 5s 扫描一次超时会话
	 * @param World 用来拿 TimerManager
	 *
	 * 设计: 放在 public 是因为外部 (RoomPlayerController) 需要调
	 *       内部有 ClearTimer 防御, 多次调用安全
	 */
	void StartSweepTimer(UWorld* World);

private:
	/**
	 * 核心在线状态表
	 * Key = 账号名, Value = 该账号当前的会话状态
	 * 仅在 ListenServer 进程上存在
	 */
	UPROPERTY()
	TMap<FString, FAccountSessionState> OnlineAccounts;

	/**
	 * 房主端心跳扫描定时器的句柄
	 * 用途: 定期调 SweepDeadSessions 清理超时会话
	 */
	FTimerHandle SweepTimerHandle;
};
