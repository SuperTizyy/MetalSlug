// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "UObject/Object.h"
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
USTRUCT()
struct FAccountSessionState
{
	GENERATED_BODY()

	/** 客户端在进房时生成的唯一标识（FGuid::NewGuid().ToString()） */
	UPROPERTY()
	FString SessionId;

	/** 指向该玩家的 PlayerController 指针 */
	UPROPERTY()
	TWeakObjectPtr<ARoomPlayerController> PC;

	/** 上线时间戳 */
	UPROPERTY()
	FDateTime LoginAt;

	/** 最后收到心跳的时间戳(用于检测异常掉线) */
	UPROPERTY()
	FDateTime LastHeartbeatAt;

	/** 默认构造 */
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
 *
 * 职责说明:
 * - 维护 TMap<Username, FAccountSessionState>，记录"谁在哪个 PC 上、用哪个 SessionId"
 * - 接收客户端的 Server_NotifyLogin / Server_NotifyLogout
 * - 判定"账号是否冲突"并触发强踢流程
 *
 * 架构理念:
 * 1. 单例: 跟随 RoomGameMode 生命周期，只在 ListenServer 端实例化
 * 2. 线程安全: 仅在游戏线程访问，不需要加锁
 * 3. 弱引用: PC 用 TWeakObjectPtr 防止崩溃时悬挂
 *
 * 注意:
 * - 加入者（Client）端不会创建本类的实例
 * - 离线（未开房）时也不会创建
 */
UCLASS()
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
