// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Login/Pages/MultiplayerMode/LANRoomPage.h"
// 房间条目 UI
#include "UI/Login/Pages/MultiplayerMode/RoomLabelWidget.h"
// 【修复 C2065】末尾追加的 OnViewShown/OnViewHidden 使用了 ULANRoomPresenter, 必须显式 include
#include "Systems/Session/LANRoomPresenter.h"
// 【P0】SessionManager 替代 OnlineSubsystem 直调（但老路径仍需完整类型定义）
#include "Systems/Session/SessionManagerSubsystem.h"
#include "Systems/Session/SessionResult.h"
#include "Systems/RoomPlayerController.h"
#include "Systems/LoginPlayerController.h"
// 【保留】旧代码路径（OnShowCreateRoomClicked / FindSessionsForAccountCheck / OnFindSessionsComplete）仍用这些类型
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"
#include "Components/ScrollBox.h"
#include "Components/ListView.h"
#include "Components/EditableTextBox.h"
#include "Components/VerticalBox.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/Overlay.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Kismet/GameplayStatics.h"
#include "Services/AccountService.h"
#include "Systems/GameFlowSubsystem.h"
#include "UI/Login/Pages/BattleRoom/PlayerLabelWidget.h"
#include "Data/Enums/RoomEnums.h"
#include "Data/Tables/MapTableRow.h"


// ==========================================
// 1. 初始化
// ==========================================

/**
 * ULANRoomPage::Initialize
 *
 * 1. 订阅 SessionManager 的 OnRoomsFound 事件（替代直绑 OnlineSubsystem 委托）
 * 2. 绑定大厅层 / 创房层 按钮
 * 3. 初始化 UI 面板状态（隐藏创房弹窗）
 * 4. 开启 3 秒一次自动刷新定时器
 * 5. 初始化游戏模式下拉框（刀战/生化）
 * 6. 初始化地图下拉框（从 MapInfoDataTable 读）
 *
 * 【P0 架构升级】LANRoomPage 不再直读 IOnlineSubsystem, 全部委托走 SessionManager
 */
bool ULANRoomPage::Initialize()
{
	if (!Super::Initialize()) return false;

	// 【P0】订阅 SessionManager 的 OnRoomsFound 事件
	//   替代原来 Initialize/Destruct 中 AddOnFindSessionsCompleteDelegate_Handle 的手动管理
	if (UGameInstance* GI = GetGameInstance())
	{
		if (USessionManagerSubsystem* SessionManager = GI->GetSubsystem<USessionManagerSubsystem>())
		{
			SessionManager->OnRoomsFound.AddDynamic(this, &ULANRoomPage::OnRoomsFoundFromManager);
		}
	}

	// ==========================================
	// 1. 绑定大厅层按钮
	// ==========================================
	if (Btn_ShowCreateRoom) Btn_ShowCreateRoom->OnClicked.AddDynamic(this, &ULANRoomPage::OnShowCreateRoomClicked);
	if (Btn_EnterRoom)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LANRoomPage] Btn_EnterRoom 绑定成功"));
		Btn_EnterRoom->OnClicked.AddDynamic(this, &ULANRoomPage::OnEnterRoomClicked);

		// 【Bug1 修复】初始为禁用态: 未选中任何房间时按钮不可点击
		// 设计理念: View 层显式控制自身可达性, 避免出现"点了没反应"的死局
		Btn_EnterRoom->SetIsEnabled(false);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[LANRoomPage] Btn_EnterRoom 为空! 请检查蓝图中的变量名是否匹配"));
	}
	if (Btn_BackToMenu) Btn_BackToMenu->OnClicked.AddDynamic(this, &ULANRoomPage::OnBackToMenuClicked);

	// ==========================================
	// 1.5 账号冲突模态对话框初始化（从 LoginPage 迁移）
	// ==========================================
	// 默认隐藏冲突弹框
	if (Overlay_LANRoomConflict)
	{
		Overlay_LANRoomConflict->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 绑定确认按钮(防呆: 先清理旧绑定, 防止多重绑定)
	if (Btn_ConfirmLANRoomConflict)
	{
		Btn_ConfirmLANRoomConflict->OnClicked.RemoveDynamic(this, &ULANRoomPage::OnConfirmLANRoomConflictClicked);
		Btn_ConfirmLANRoomConflict->OnClicked.AddDynamic(this, &ULANRoomPage::OnConfirmLANRoomConflictClicked);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[LANRoomPage] Btn_ConfirmLANRoomConflict 为空! 请检查 WBP_LANRoomPage 里是否拖了同名按钮"));
	}

	// ==========================================
	// 2. 绑定创房层按钮
	// ==========================================
	// 【大厂标准】创房/取消按钮必须打诊断日志, 与 Btn_EnterRoom 保持一致
	// 原因: 早期排查发现: Btn_ConfirmCreateRoom 静默失败 → 无法定位是按钮为空还是点击未触发
	if (Btn_ConfirmCreateRoom)
	{
		Btn_ConfirmCreateRoom->OnClicked.RemoveDynamic(this, &ULANRoomPage::OnConfirmCreateRoomClicked);
		Btn_ConfirmCreateRoom->OnClicked.AddDynamic(this, &ULANRoomPage::OnConfirmCreateRoomClicked);
		UE_LOG(LogTemp, Warning, TEXT("[LANRoomPage] Btn_ConfirmCreateRoom 绑定成功"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[LANRoomPage] Btn_ConfirmCreateRoom 为空! 请检查 WBP_LANRoomPage 里是否拖了同名按钮 (类型必须为 UButton)"));
	}

	if (Btn_HideCreateRoom)
	{
		Btn_HideCreateRoom->OnClicked.RemoveDynamic(this, &ULANRoomPage::OnHideCreateRoomClicked);
		Btn_HideCreateRoom->OnClicked.AddDynamic(this, &ULANRoomPage::OnHideCreateRoomClicked);
		UE_LOG(LogTemp, Warning, TEXT("[LANRoomPage] Btn_HideCreateRoom 绑定成功"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[LANRoomPage] Btn_HideCreateRoom 为空! 请检查 WBP_LANRoomPage 里是否拖了同名按钮 (类型必须为 UButton)"));
	}

	// ==========================================
	// 初始化 UI 面板状态
	// ==========================================
	// 刚进大厅时，隐藏创房面板和房间内面板
	if (Overlay_CreateRoom) Overlay_CreateRoom->SetVisibility(ESlateVisibility::Hidden);
	// 【新增】一开始隐藏创房提示框
	if (Text_CreateRoomHint) Text_CreateRoomHint->SetVisibility(ESlateVisibility::Hidden);

	// 初始为未准备状态
	bIsReady = false;

	// 【核心修复1】: 在开启定时器前，先强杀可能残留的幽灵定时器
	GetWorld()->GetTimerManager().ClearTimer(SearchTimerHandle);

	// 【修改】开启定时器，每 3 秒自动执行一次 FindLANRooms，并且一进来就立刻执行一次
	GetWorld()->GetTimerManager().SetTimer(SearchTimerHandle, this, &ULANRoomPage::FindLANRooms, 3.0f, true, 0.0f);

	// ==========================================
	// 3. 初始化游戏模式与地图选择下拉框
	// ==========================================
	if (ComboBox_GameMode)
	{
		// 添加游戏模式选项
		ComboBox_GameMode->AddOption(TEXT("刀战模式"));
		ComboBox_GameMode->AddOption(TEXT("生化模式"));
		// 默认选中第一个
		ComboBox_GameMode->SetSelectedIndex(0);
	}

	if (ComboBox_MapSelect && MapInfoDataTable)
	{
		// 清空旧选项
		ComboBox_MapSelect->ClearOptions();

		static const FString ContextString(TEXT("MapInfo Context"));
		TArray<FMapInfoRow*> AllMaps;
		MapInfoDataTable->GetAllRows<FMapInfoRow>(ContextString, AllMaps);

		// 遍历地图表，提取 DisplayName 填充下拉框
		for (FMapInfoRow* MapInfo : AllMaps)
		{
			if (MapInfo)
			{
				ComboBox_MapSelect->AddOption(MapInfo->DisplayName.ToString());
			}
		}

		// 如果表里有数据，默认选中第一个
		if (ComboBox_MapSelect->GetOptionCount() > 0)
		{
			ComboBox_MapSelect->SetSelectedIndex(0);
		}
	}

	return true;
}


// ==========================================
// 2. 界面销毁时，必须关掉定时器
// ==========================================

/**
 * ULANRoomPage::NativeDestruct
 *
 * 1. 关闭自动刷新定时器
 * 2. 退订 FindSessions 委托（持有句柄凭证）
 * 3. 核心修复: 房主 + 非传送销毁时，强制摧毁 Session
 */
void ULANRoomPage::NativeDestruct()
{
	// ==========================================
	// 【DEBUG-SET-3-C】LANRoomPage 被销毁的入口 (切图/主动 RemoveFromParent 都会触发)
	// ==========================================
	UE_LOG(LogTemp, Error,
		TEXT("[DEBUG-S3-C][LANRoomPage::NativeDestruct] PID=%u WorldName=%s bIsHost=%d bIsTraveling=%d"),
		FPlatformProcess::GetCurrentProcessId(),
		*GetWorld()->GetName(),
		bIsHost ? 1 : 0,
		bIsTraveling ? 1 : 0);

	// 关闭自动刷新定时器
	GetWorld()->GetTimerManager().ClearTimer(SearchTimerHandle);

	// 【P0】解绑 SessionManager 事件订阅
	if (UGameInstance* GI = GetGameInstance())
	{
		if (USessionManagerSubsystem* SessionManager = GI->GetSubsystem<USessionManagerSubsystem>())
		{
			SessionManager->OnRoomsFound.RemoveDynamic(this, &ULANRoomPage::OnRoomsFoundFromManager);
		}
	}

	// ==========================================
	// 【P0】房主离开: 走 SessionManager->DestroyRoom, 不再直调 DestroySession
	// ==========================================
	if (bIsHost && !bIsTraveling)
	{
		if (UGameInstance* GI2 = GetGameInstance())
		{
			if (USessionManagerSubsystem* SessionManager = GI2->GetSubsystem<USessionManagerSubsystem>())
			{
				SessionManager->DestroyRoom(FOnDestroyRoomComplete());
				bIsHost = false;
				if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow, TEXT("检测到房主离开，后台房间已强制解散!"));
			}
		}
	}

	Super::NativeDestruct();
}


// ==========================================
// 3. 大厅层逻辑
// ==========================================

/**
 * OnShowCreateRoomClicked
 *
 * 打开创房弹窗
 * 体验优化: 每次打开时清空输入框和错误提示
 */
void ULANRoomPage::OnShowCreateRoomClicked()
{
	// 弹出创房面板
	if (Overlay_CreateRoom) Overlay_CreateRoom->SetVisibility(ESlateVisibility::Visible);

	// 【体验优化】: 每次打开创房面板时，把上一次的错误提示隐藏，并清空输入框
	if (Text_CreateRoomHint) Text_CreateRoomHint->SetVisibility(ESlateVisibility::Hidden);
	if (Input_RoomName) Input_RoomName->SetText(FText::GetEmpty());
	if (Input_RoomPassword) Input_RoomPassword->SetText(FText::GetEmpty());
}


// ==========================================
// 4. 处理房间选中与加入逻辑
// ==========================================

/**
 * HandleRoomSelected
 *
 * 房间条目被点击的回调
 * 1. 通过 widget 引用读取房间名 (单一可信数据源)
 * 2. 联动 Btn_EnterRoom 可用性
 * 3. 遍历所有条目，更新高亮状态（多选一）
 *
 * 【架构升级】: 参数从 FString 改为 URoomLabelWidget*
 * 原因: 旧设计依赖 CachedRoomName 字符串传递, 若房主未写入 ROOM_NAME SessionSetting
 *       则 widget 收到空字符串, 外层按钮永远不可用
 *       新设计: 外层通过 widget 引用直接调用 GetRoomName(), 拿到准确的 CachedRoomName
 *               即使房主没写 ROOM_NAME, 玩家至少能拿到 "" 而不是被静默丢弃
 */
void ULANRoomPage::HandleRoomSelected(URoomLabelWidget* SelectedRoomWidget)
{
	// ==========================================
	// 【P0 防御】: 防止 widget 被 ClearChildren 销毁后, 残留的回调进入
	// IsValid 检查能识别"已标记 GC 但内存还在"的情况, 比 nullptr 严格
	// ==========================================
	if (!IsValid(SelectedRoomWidget))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[LANRoomPage] HandleRoomSelected: SelectedRoomWidget 为空或已销毁, 忽略本次回调"));
		return;
	}

	// ==========================================
	// 【P0 修复】: 从 widget 引用读取房间名 (单一可信数据源)
	// ==========================================
	const FString RoomName = SelectedRoomWidget->GetRoomName();

	UE_LOG(LogTemp, Warning,
		TEXT("[LANRoomPage] HandleRoomSelected 被调用, SelectedWidget=%s RoomName=[%s]"),
		*SelectedRoomWidget->GetName(), *RoomName);

	// 记录当前选中的房间名
	CurrentSelectedRoomName = RoomName;

	// 【架构升级】缓存选中的 widget 引用, 用于后续重绘时回填高亮
	CurrentSelectedRoomWidget = SelectedRoomWidget;

	// ==========================================
	// 【Bug1/Bug2 修复】: 选中房间后, Btn_EnterRoom 立即变为可用态
	// 设计理由: 选中是用户的"明确意图", 此时禁用按钮违反交互预期
	// ==========================================
	if (Btn_EnterRoom)
	{
		bool bShouldEnable = !CurrentSelectedRoomName.IsEmpty();
		Btn_EnterRoom->SetIsEnabled(bShouldEnable);

		UE_LOG(LogTemp, Warning,
			TEXT("[LANRoomPage] HandleRoomSelected Btn_EnterRoom SetIsEnabled=%d (CurrentSelectedRoomName=[%s])"),
			bShouldEnable ? 1 : 0, *CurrentSelectedRoomName);

		// 【Bug2 大厂诊断】: 同步日志告知按钮实际状态
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Yellow,
				FString::Printf(TEXT("已选中房间 [%s]，进入按钮已启用"), *CurrentSelectedRoomName));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[LANRoomPage] HandleRoomSelected: Btn_EnterRoom 为空!"));
	}

	// ==========================================
	// 【核心魔法】: 遍历列表，刷新所有条目的高亮状态
	// 使用 widget 引用比对, 不依赖字符串 (与新架构匹配)
	// ==========================================
	if (List_Rooms)
	{
		TArray<UWidget*> AllRoomLabels = List_Rooms->GetAllChildren();
		for (UWidget* ChildWidget : AllRoomLabels)
		{
			if (URoomLabelWidget* RoomLabel = Cast<URoomLabelWidget>(ChildWidget))
			{
				// 引用相等: 点击的 widget 与当前 widget 是同一个 → 高亮
				bool bShouldHighlight = (RoomLabel == SelectedRoomWidget);
				RoomLabel->SetHighlight(bShouldHighlight);
			}
		}
	}
}


/**
 * OnEnterRoomClicked
 *
 * 加入房间按钮
 * 1. 拦截: 是否有选中房间
 * 2. 从 SessionSearch 中找到对应的 FOnlineSessionSearchResult
 * 3. 调用 JoinSession 加入
 *
 * 【Bug3 修复要点】:
 *   - 使用 FRoomSessionResult(const FOnlineSessionSearchResult&) 构造函数,
 *     让业务字段 (RoomName/HostAccount/...) 自动填充 (旧代码手动构造, 业务字段全空)
 *   - 加入失败时给玩家明确的 Toast, 不要再静默 return
 */
void ULANRoomPage::OnEnterRoomClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("[LANRoomPage] OnEnterRoomClicked 被调用，CurrentSelectedRoomName=[%s]"), *CurrentSelectedRoomName);

	// 1. 拦截: 有没有选中房间？
	if (CurrentSelectedRoomName.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[LANRoomPage] 房间名为空，无法加入"));
		return;
	}

	// ==========================================
	// 【P0 修复】从 SessionSearch 中找选中房间的搜索结果
	// 旧设计: 按 ROOM_NAME 字符串匹配 → 若房间名为空, 会匹配错
	// 新设计: 按 CurrentSelectedWidget 在 List_Rooms 中的索引匹配 SessionSearch->SearchResults 同索引
	//         索引是稳定的, 不依赖字段值
	// ==========================================
	FOnlineSessionSearchResult* TargetRoomResult = nullptr;
	int32 SelectedIndex = INDEX_NONE;
	if (SessionSearch.IsValid() && List_Rooms)
	{
		// 找到当前选中 widget 在 List_Rooms 中的索引
		TArray<UWidget*> AllChildren = List_Rooms->GetAllChildren();
		for (int32 i = 0; i < AllChildren.Num(); ++i)
		{
			if (AllChildren[i] == CurrentSelectedRoomWidget.Get())
			{
				SelectedIndex = i;
				break;
			}
		}

		// 用索引拿搜索结果 (ClearChildren+CreateWidget 保持 widget 顺序与 SessionSearch 顺序一致)
		if (SessionSearch->SearchResults.IsValidIndex(SelectedIndex))
		{
			TargetRoomResult = &SessionSearch->SearchResults[SelectedIndex];
		}
	}

	// 【Bug3 防御】: 找不到对应搜索结果 (可能 SessionSearch 已被 OnRoomsFoundFromManager 重建, 选中态过期)
	if (!TargetRoomResult)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[LANRoomPage] 找不到选中房间 [%s] 的搜索结果 (SelectedIndex=%d, SessionSearch 已过期), 请重新点击房间刷新选中态"),
			*CurrentSelectedRoomName, SelectedIndex);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Orange,
				TEXT("房间列表已刷新，请重新选择房间"));
		}

		// 清掉选中态, 按钮回到禁用, 让用户重新选
		CurrentSelectedRoomName = TEXT("");
		CurrentSelectedRoomWidget.Reset();
		if (Btn_EnterRoom) Btn_EnterRoom->SetIsEnabled(false);
		if (List_Rooms)
		{
			for (UWidget* Child : List_Rooms->GetAllChildren())
			{
				if (URoomLabelWidget* RoomLabel = Cast<URoomLabelWidget>(Child))
				{
					RoomLabel->SetHighlight(false);
				}
			}
		}
		return;
	}

	// 3. 获取当前登录账号
	FString MyAccountName = TEXT("");
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountService* AccountSub = UAccountService::Get(this))
		{
			MyAccountName = AccountSub->GetCurrentUser();
		}
	}

	// 4. 查目标房间的 HOST_ACCOUNT + ROOM_ACCOUNTS，逐一与当前账号比对
	//    ROOM_ACCOUNTS 由房主端在每次玩家登录/登出时通过 UpdateSession 广播
	//    包含: [HOST_ACCOUNT, 玩家1, 玩家2, ...]
	if (TargetRoomResult && !MyAccountName.IsEmpty())
	{
		// 4a. 先查 HOST_ACCOUNT（房主账号）
		FString TargetHostAccount = TEXT("");
		TargetRoomResult->Session.SessionSettings.Get(FName("HOST_ACCOUNT"), TargetHostAccount);

		if (!TargetHostAccount.IsEmpty() &&
			TargetHostAccount.Equals(MyAccountName, ESearchCase::IgnoreCase))
		{
			UE_LOG(LogTemp, Warning, TEXT("[LANRoomPage] 同账号 [%s] 创建的房间，禁止加入"), *CurrentSelectedRoomName);
			ShowLANRoomConflictDialog();
			return;
		}

		// 4b. 再查 ROOM_ACCOUNTS（所有已在房间内的玩家账号，已序列化为管道符分隔字符串）
		FString RoomAccountsStr;
		TargetRoomResult->Session.SessionSettings.Get(FName("ROOM_ACCOUNTS"), RoomAccountsStr);

		TArray<FString> RoomAccounts;
		RoomAccountsStr.ParseIntoArray(RoomAccounts, TEXT("|"), true);

		for (const FString& RoomAccount : RoomAccounts)
		{
			if (RoomAccount.Equals(MyAccountName, ESearchCase::IgnoreCase))
			{
				UE_LOG(LogTemp, Warning, TEXT("[LANRoomPage] 当前账号 [%s] 已在房间中，禁止重复加入"), *MyAccountName);
				ShowLANRoomConflictDialog();
				return;
			}
		}
	}

	// 5. 所有检查通过 → 走 SessionManager->JoinRoom (由 SessionManager 内部管理委托)
	if (TargetRoomResult)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (USessionManagerSubsystem* SessionManager = GI->GetSubsystem<USessionManagerSubsystem>())
			{
				// 【Bug3 修复】使用 FRoomSessionResult(const FOnlineSessionSearchResult&) 构造函数
				//
				// 设计理由:
				//   旧代码手动 MakeShared<FOnlineSessionSearchResult>(*TargetRoomResult)
				//   然后 RoomResult.RoomName/HostAccount 等业务字段保持空字符串!
				//   这会导致 SessionManager 内部 JoinSession 时, OSS 用业务字段匹配失败
				//   → 返回 SessionDoesNotExist → ConnectString 为空 → ClientTravel 失败
				//
				// 新代码: 走标准构造函数, 自动填充 RoomName/HostAccount/GameMode/MapName/...
				//         同时 RawSearchResult 仍然共享源数据 (MakeShared 是引用计数安全的)
				FRoomSessionResult RoomResult(*TargetRoomResult);

				// 安全日志: 诊断用
				UE_LOG(LogTemp, Warning,
					TEXT("[LANRoomPage] 准备加入房间: RoomName=[%s] HostAccount=[%s] Map=[%s] Mode=[%s]"),
					*RoomResult.RoomName, *RoomResult.HostAccount,
					*RoomResult.MapName, *RoomResult.GameMode);

				// 【P0】用 UFUNCTION 装回调 (FOnJoinRoomComplete 是 Dynamic 委托, 必须 UFUNCTION)
				FOnJoinRoomComplete JoinDelegate;
				JoinDelegate.BindDynamic(this, &ULANRoomPage::OnJoinRoomFromManager);
				SessionManager->JoinRoom(RoomResult, JoinDelegate);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[LANRoomPage] SessionManager 不可用, 无法加入房间"));
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
						TEXT("会话管理器不可用，无法加入房间"));
				}
			}
		}
	}
}


/**
 * OnJoinRoomFromManager
 *
 * 【P0 架构升级】SessionManager JoinRoom 单播回调 (Dynamic 委托)
 * 收到后转交原有 OnJoinSessionComplete 走 ClientTravel 流程
 *
 * 【Bug3 修复】: 失败路径不再静默, 给玩家明确的 Toast 提示
 */
void ULANRoomPage::OnJoinRoomFromManager(bool bWasSuccessful, const FString& ConnectString)
{
	if (bWasSuccessful)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[LANRoomPage] JoinRoom 成功, ConnectString=%s"), *ConnectString);

		// 【大厂 P0 修复】传入 SessionManager 解析的真实 ConnectString
		// 真局域网: 192.168.x.x:7777
		// 单机器本地: 127.0.0.1:7777（引擎 GetResolvedConnectString 自动返回）
		// 不再硬编码地址，跨机器部署时仍能正确连接房主。
		OnJoinSessionComplete(NAME_GameSession, EOnJoinSessionCompleteResult::Success, ConnectString);
	}
	else
	{
		// 【Bug3 修复】失败路径给玩家明确提示, 不要再静默 return
		// 失败原因:
		//   - 房间已满 (SessionIsFull)
		//   - 房间已消失 (SessionDoesNotExist) — 通常是搜索结果过期, 房主已下线
		//   - 地址解析失败 (CouldNotRetrieveAddress)
		//   - 网络错误
		UE_LOG(LogTemp, Error,
			TEXT("[LANRoomPage] JoinRoom 失败 (ConnectString=%s)"),
			*ConnectString);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::Red,
				TEXT("加入房间失败：房间可能已关闭、已满或网络异常，请稍后重试"));
		}

		// 重置选中态 + 按钮回到禁用
		CurrentSelectedRoomName = TEXT("");
		if (Btn_EnterRoom) Btn_EnterRoom->SetIsEnabled(false);
		if (List_Rooms)
		{
			for (UWidget* Child : List_Rooms->GetAllChildren())
			{
				if (URoomLabelWidget* RoomLabel = Cast<URoomLabelWidget>(Child))
				{
					RoomLabel->SetHighlight(false);
				}
			}
		}

		// 失败路径：ConnectString 无意义, 走 Error 结果码
		OnJoinSessionComplete(NAME_GameSession, EOnJoinSessionCompleteResult::SessionDoesNotExist, FString());
	}
}


// ==========================================
// 5. 成功加入房间后的"队伍自动平衡分配"算法
// ==========================================

/**
 * OnJoinSessionComplete
 *
 * 加入会话完成回调
 * 1. 退订委托
 * 2. 成功后获取 ConnectString（房主 IP）
 * 3. 调用 ClientTravel 传送到房主世界
 * 注意: 本地测试写死 127.0.0.1:7777
 */
void ULANRoomPage::OnJoinSessionComplete(
	FName SessionName,
	EOnJoinSessionCompleteResult::Type Result,
	const FString& ConnectString)
{
	// 【P0】原 OnlineSubsystem 委托清理已废弃 (改由 SessionManager 内部管理)

	if (Result == EOnJoinSessionCompleteResult::Success)
	{
		bIsHost = false; // 我是加入者，不是房主

		// 【大厂 P0 修复】使用 SessionManager 通过 OnJoinRoomFromManager 传入的真实 ConnectString
		// 防御 1: 理论上 Success 时 ConnectString 必然非空 (SessionManager 在 JoinSessionComplete 成功时
		//         已经调 GetResolvedConnectString 拿到地址)。但兜底检查一下, 避免空字符串传给 ClientTravel。
		// 防御 2: 只有房主在同一台机器本地测试时, ConnectString 是 127.0.0.1:7777
		//         跨机器时, ConnectString 是房主的局域网 IP: 192.168.x.x:7777
		if (ConnectString.IsEmpty())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[LANRoomPage] OnJoinSessionComplete 成功但 ConnectString 为空, 拒绝 ClientTravel 防止跳错地址"));
			return;
		}

		UE_LOG(LogTemp, Log,
			TEXT("[LANRoomPage] 准备 ClientTravel → ConnectString=%s"), *ConnectString);

		bIsTraveling = true; // 标记正在跳转, NativeDestruct 时不再误判为房主离开

		// 让当前玩家的控制器带着真实 IP，瞬间飞进房主的世界
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			PC->ClientTravel(ConnectString, TRAVEL_Absolute);
		}
	}
}


/**
 * OnBackToMenuClicked
 *
 * 返回主菜单按钮
 * 销毁当前大厅，生成主菜单
 */
void ULANRoomPage::OnBackToMenuClicked()
{
	// ==========================================
	// 【核心跳转逻辑】: 动态生成主菜单 UI 并销毁当前局域网大厅
	// ==========================================

	// 【架构升级】返回主菜单: 走 GameFlowSubsystem, 由 UIViewService 自动接管
    if (UGameInstance* GI = GetGameInstance())
    {
        if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
        {
            FlowSubsystem->TransitToState(EMatchState::MainMenu);
        }
    }
    // 注意: 不再手动 RemoveFromParent, UIViewService 会处理
}


// ==========================================
// 6. 搜索房间并显示到列表的逻辑
// ==========================================

/**
 * FindLANRooms
 *
 * 触发局域网房间搜索
 * 1. 状态锁: 引擎自带的 EOnlineAsyncTaskState::InProgress 检查
 * 2. 重新插拔委托: 保证回调畅通
 * 3. 配置 SessionSearch（LAN、20 结果、2 秒超时）
 * 4. 调用 FindSessions
 */
void ULANRoomPage::FindLANRooms()
{
	// 【P0】走 SessionManager->FindRooms, 由 SessionManager 内部管理状态锁/委托生命周期
	//   替代原 OnlineSubsystem 直调 + SessionSearch 直配
	if (UGameInstance* GI = GetGameInstance())
	{
		if (USessionManagerSubsystem* SessionManager = GI->GetSubsystem<USessionManagerSubsystem>())
		{
			SessionManager->FindRooms(FOnFindRoomsComplete());
		}
	}
}


/**
 * OnRoomsFoundFromManager
 *
 * 【P0 架构升级】SessionManager.OnRoomsFound 多播委托回调
 * 职责: 把 SessionManager 返回的 TArray<FRoomSessionResult> 还原为 SessionSearch
 *       然后转交给 OnFindSessionsComplete 走原有 UI 渲染逻辑
 *
 * 设计取舍: 不重写整个 UI 渲染（OnFindSessionsComplete 内部强依赖 FOnlineSessionSearch）,
 *          通过复用 SessionSearch 兼容老代码, 后续可平滑迁移
 */
void ULANRoomPage::OnRoomsFoundFromManager(const TArray<FRoomSessionResult>& Rooms)
{
	// ==========================================
	// 【DEBUG-SET-1-A】入口: 跨进程 OSS 回调是否真的触发了？
	// ==========================================
	UE_LOG(LogTemp, Error,
		TEXT("[DEBUG-S1-A][OnRoomsFoundFromManager] PID=%u WorldName=%s Rooms.Num=%d NetMode=%d IsInViewport=%d ActivePanel/WidgetValid=%d/%d"),
		FPlatformProcess::GetCurrentProcessId(),
		*GetWorld()->GetName(),
		Rooms.Num(),
		(int32)GetWorld()->GetNetMode(),
		IsInViewport() ? 1 : 0,
		0, 0);  // ActivePanel/Widget 暂时用 0 占位, 后面看 UIViewService

	// 1. 构造 SessionSearch 并填充 SearchResults
	SessionSearch = MakeShareable(new FOnlineSessionSearch());
	SessionSearch->bIsLanQuery = true;
	SessionSearch->MaxSearchResults = 20;
	SessionSearch->TimeoutInSeconds = 2.0f;

	for (const FRoomSessionResult& Room : Rooms)
	{
		if (Room.IsValid() && Room.RawSearchResult.IsValid())
		{
			SessionSearch->SearchResults.Add(*Room.RawSearchResult);
		}
	}

	// 2. 走原有 UI 渲染逻辑（OnFindSessionsComplete 内部已封装, 不重复实现）
	OnFindSessionsComplete(true);
}


/**
 * OnFindSessionsComplete
 *
 * 搜索完成回调
 * 1. 退订委托
 * 2. 解析所有搜索结果，生成 NewlyFoundRoomSignatures 签名
 * 3. 与 CurrentRoomSignatures 对比，决定是否需要重绘 UI
 * 4. 真正需要重绘时，ClearChildren + 重新 AddChild 房间条目
 * 5. 设置高亮（针对已选中的房间名）
 */
void ULANRoomPage::OnFindSessionsComplete(bool bWasSuccessful)
{
	// ==========================================
	// 【DEBUG-SET-2-A】入口: 搜索完成回调触发
	// ==========================================
	UE_LOG(LogTemp, Error,
		TEXT("[DEBUG-S2-A][OnFindSessionsComplete] PID=%u WorldName=%s bWasSuccessful=%d IsInViewport=%d"),
		FPlatformProcess::GetCurrentProcessId(),
		*GetWorld()->GetName(),
		bWasSuccessful ? 1 : 0,
		IsInViewport() ? 1 : 0);

	// ==========================================
	// 1. 声明这两个关键的数组（修复 C2065 报错）
	// ==========================================
	TArray<FString> NewlyFoundRooms;
	TArray<FString> NewlyFoundRoomSignatures;

	// 【P0】原 OnlineSubsystem 委托清理已废弃 (改由 SessionManager.OnRoomsFound 推送)
	//   保留这段是为了兼容 OnRoomsFoundFromManager 转发调用, 但解绑已经不需要

	// ==========================================
	// 3. 打印我们期待已久的终极回调状态
	// ==========================================
	int32 FoundNum = SessionSearch.IsValid() ? SessionSearch->SearchResults.Num() : 0;
	if (GEngine)
	{
		FString Msg = FString::Printf(TEXT("回调触发了! 成功状态: %d, 搜到 %d 个"), bWasSuccessful, FoundNum);
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, bWasSuccessful ? FColor::Cyan : FColor::Red, Msg);
	}

	// ==========================================
	// 4. 解析结果并生成 UI
	// ==========================================
	if (bWasSuccessful && SessionSearch.IsValid())
	{
		// 第一遍遍历: 提取名字和人数，制作"签名"
		for (const FOnlineSessionSearchResult& Result : SessionSearch->SearchResults)
		{
			if (Result.IsValid())
			{
				FString FoundRoomName = TEXT("未命名房间");
				Result.Session.SessionSettings.Get(FName("ROOM_NAME"), FoundRoomName);
				NewlyFoundRooms.Add(FoundRoomName);

				int32 MaxPlayers = Result.Session.SessionSettings.NumPublicConnections;
				if (MaxPlayers <= 0) MaxPlayers = 10;

				int32 CurrentRealPlayers = MaxPlayers - Result.Session.NumOpenPublicConnections;
				if (CurrentRealPlayers <= 0) CurrentRealPlayers = 1;

				int32 TotalPlayersWithAI = CurrentRealPlayers;
				Result.Session.SessionSettings.Get(FName("TOTAL_PLAYERS_WITH_AI"), TotalPlayersWithAI);

				if (TotalPlayersWithAI <= 0) TotalPlayersWithAI = 1;

				bool bIsPlaying = false;
				Result.Session.SessionSettings.Get(FName("ROOM_STATE"), bIsPlaying);

				FString Signature = FString::Printf(TEXT("%s_%d_%d_%d"), *FoundRoomName, TotalPlayersWithAI, MaxPlayers, bIsPlaying ? 1 : 0);
				NewlyFoundRoomSignatures.Add(Signature);
			}
		}

		bool bIsDifferent = false;

		if (NewlyFoundRoomSignatures.Num() != CurrentRoomSignatures.Num())
		{
			bIsDifferent = true;
		}
		else
		{
			for (int32 i = 0; i < NewlyFoundRoomSignatures.Num(); ++i)
			{
				if (NewlyFoundRoomSignatures[i] != CurrentRoomSignatures[i])
				{
					bIsDifferent = true;
					break;
				}
			}
		}

		if (bIsDifferent)
		{
			CurrentDisplayedRooms = NewlyFoundRooms;
			CurrentRoomSignatures = NewlyFoundRoomSignatures;

			// ==========================================
			// 【DEBUG-SET-2-B】即将重绘房间列表 (ClearChildren + AddChild)
			// ==========================================
			UE_LOG(LogTemp, Error,
				TEXT("[DEBUG-S2-B][RedrawList] PID=%u WorldName=%s OldSigNum=%d NewSigNum=%d IsInViewport=%d"),
				FPlatformProcess::GetCurrentProcessId(),
				*GetWorld()->GetName(),
				CurrentRoomSignatures.Num(),
				NewlyFoundRoomSignatures.Num(),
				IsInViewport() ? 1 : 0);

			if (List_Rooms)
			{
				List_Rooms->ClearChildren();

				if (RoomLabelClass)
				{
					for (const FOnlineSessionSearchResult& Result : SessionSearch->SearchResults)
					{
						if (Result.IsValid())
						{
							URoomLabelWidget* RoomLabel = CreateWidget<URoomLabelWidget>(GetWorld(), RoomLabelClass);
							if (RoomLabel)
							{
								// 【Bug1 修复】强制触发蓝图属性同步
								// UMG 的 BindWidget 绑定在 NativeConstruct 阶段尚未解析 (Slate 树未构建完)
								// 若不主动调用 SynchronizeProperties，Text_RoomName 在 CreateWidget 后仍为 nullptr
								// 导致 SetRoomName 写入 CachedRoomName 成功但 Text_RoomName->SetText 静默跳过
								RoomLabel->SynchronizeProperties();

								// ==========================================
								// 【P0 修复】解析房间名: 多源兜底策略
								// 优先级: ROOM_NAME > HOST_ACCOUNT > 未命名房间#索引
								// 原因: 即使房主端某个字段没广播 (DontAdvertise), 也不会让玩家看到空字符串
								// ==========================================
								FString RoomName;
								Result.Session.SessionSettings.Get(FName("ROOM_NAME"), RoomName);

								FString HostAccount;
								Result.Session.SessionSettings.Get(FName("HOST_ACCOUNT"), HostAccount);

								if (RoomName.IsEmpty())
								{
									if (!HostAccount.IsEmpty())
									{
										// 兜底 1: 用房主账号作为房间名
										RoomName = FString::Printf(TEXT("%s 的房间"), *HostAccount);
									}
									else
									{
										// 兜底 2: 硬编码 "未命名房间" 保证按钮可用
										RoomName = TEXT("未命名房间");
									}
								}

								// 【大厂 P0 诊断】: 把每个房间的真实 RoomName 打印出来
								// 解决"按钮永远不可用"问题: 即使房主未广播 ROOM_NAME,
								// 客户端也会使用 HOST_ACCOUNT 兜底, 保证房间名非空
								UE_LOG(LogTemp, Warning,
									TEXT("[LANRoomPage][RedrawList] RoomName=[%s] HostAccount=[%s] IsValid=%d"),
									*RoomName, *HostAccount, Result.IsValid() ? 1 : 0);

								RoomLabel->SetRoomName(RoomName);

								// 【2026-06-30 P0 Bug2 终极修复】客户端: 严禁用 MaxPlayers - NumOpenPublicConnections 兜底
								// 旧逻辑: CurrentRealPlayers = MaxPlayers - NumOpenPublicConnections, 默认填入 TotalPlayersWithAI
								// 根因: NumOpenPublicConnections 在 listen server 跨图/玩家加入退出时的状态不一致
								//       (player counts via FUniqueNetId replication 滞后于 PlayerController replication),
								//       → 客户端看到的 TotalPlayersWithAI 经常是错的 (例如 4 而非 2)。
								// 新策略: TotalPlayersWithAI 必须由房主主动推送, 客户端拿到就用, 拿不到就 1 (兜底)
								int32 MaxPlayers = Result.Session.SessionSettings.NumPublicConnections;
								if (MaxPlayers <= 0) MaxPlayers = 10;

								int32 TotalPlayersWithAI = 0;
								Result.Session.SessionSettings.Get(FName("TOTAL_PLAYERS_WITH_AI"), TotalPlayersWithAI);

								// 兜底: 房主还没推送过 (例如刚 create session 时的瞬时窗口)
								if (TotalPlayersWithAI <= 0) TotalPlayersWithAI = 1;

								// 【大厂 P0 诊断日志 - 2026-06-30】追踪 TOTAL_PLAYERS_WITH_AI 读取是否正确
								// 解决"显示4人"问题: 看清每个房间项的 NumOpenConnections vs Get 实际读到的值
								UE_LOG(LogTemp, Log,
									TEXT("[LANRoomPage][PlayerCount-DIAG] RoomName=[%s] MaxPlayers=%d NumOpenPublicConn=%d GetReturns=%d FinalTotal=%d"),
									*RoomName, MaxPlayers, Result.Session.NumOpenPublicConnections,
									TotalPlayersWithAI, FMath::Max(TotalPlayersWithAI, 1));

								RoomLabel->SetPlayerCount(TotalPlayersWithAI, MaxPlayers);

								bool bIsPlaying = false;
								Result.Session.SessionSettings.Get(FName("ROOM_STATE"), bIsPlaying);
								RoomLabel->SetRoomState(bIsPlaying);

								// 【架构升级】高亮匹配逻辑:
								//  1. 优先: 引用相等 (用户刚刚点的 widget 与当前 widget 是同一个)
								//  2. 兜底: 字符串相等 (新 widget 通过 RoomName 找回高亮态)
								//  这样即使 ClearChildren 后 widget 实例重建, 也能恢复高亮
								bool bIsHighlight = false;
								if (CurrentSelectedRoomWidget.IsValid())
								{
									bIsHighlight = (RoomLabel == CurrentSelectedRoomWidget.Get());
								}
								else
								{
									bIsHighlight = (RoomName == CurrentSelectedRoomName);
								}
								RoomLabel->SetHighlight(bIsHighlight);

								RoomLabel->OnRoomSelected.AddDynamic(this, &ULANRoomPage::HandleRoomSelected);

								List_Rooms->AddChild(RoomLabel);
							}
						}
					}
				}
			}
		}
	}

	// 最后别忘了重置搜索状态，防止死循环
	bIsSearching = false;
}


// ==========================================
// 7. 创房层逻辑
// ==========================================

/**
 * OnConfirmCreateRoomClicked
 *
 * 确认创建房间按钮
 * 1. 提取 Trim 后的房间名/密码
 * 2. 读取游戏模式/地图名（从 MapInfoDataTable 反查 LevelName）
 * 3. 校验: 房间名非空
 * 4. 校验: 不与已存在房间重名
 * 5. 检查是否有残留 Session，有则先销毁再创建
 * 6. 调用 HostRealSession
 */
void ULANRoomPage::OnConfirmCreateRoomClicked()
{
	// 1. 获取用户输入的房间名，并使用 TrimStartAndEnd() 去掉前后的无用空格，防止用户只敲了几个空格
	PendingRoomName = Input_RoomName->GetText().ToString().TrimStartAndEnd();
	PendingRoomPassword = Input_RoomPassword->GetText().ToString();

	// ==========================================
	// 2. 获取选择的游戏模式和地图
	// ==========================================
	if (ComboBox_GameMode)
	{
		PendingGameMode = ComboBox_GameMode->GetSelectedOption();
	}

	if (ComboBox_MapSelect)
	{
		// 获取选中的地图显示名称
		FString SelectedMapDisplayName = ComboBox_MapSelect->GetSelectedOption();

		// 从 MapInfoDataTable 中查找对应的 LevelName
		if (MapInfoDataTable)
		{
			static const FString ContextString(TEXT("MapInfo Context"));
			TArray<FMapInfoRow*> AllMaps;
			MapInfoDataTable->GetAllRows<FMapInfoRow>(ContextString, AllMaps);

			for (FMapInfoRow* MapInfo : AllMaps)
			{
				if (MapInfo && MapInfo->DisplayName.ToString() == SelectedMapDisplayName)
				{
					PendingMapLevelName = MapInfo->LevelName;
					break;
				}
			}
		}
	}

	// ==========================================
	// 【核心校验 1】: 房间名是否为空？
	// ==========================================
	if (PendingRoomName.IsEmpty())
	{
		if (Text_CreateRoomHint)
		{
			Text_CreateRoomHint->SetText(FText::FromString(TEXT("房间名称不能为空!")));
			Text_CreateRoomHint->SetVisibility(ESlateVisibility::Visible);
		}
		return; // 拦截操作，不往下执行
	}

	// ==========================================
	// 2. 准备调用引擎底层接口创建真实的局域网会话 (Session)
	// 【Bug3 修复】: 文字改为用户期望的内容，且按钮立即禁用，防止重复点击
	if (Text_CreateRoomHint)
	{
		Text_CreateRoomHint->SetText(FText::FromString(TEXT("正在检测是否有相同账号创建了房间...")));
		Text_CreateRoomHint->SetVisibility(ESlateVisibility::Visible);
	}

	// 【Bug3 修复】: 立即禁用创建按钮，防止异步过程中用户重复点击
	DisableCreateRoomButton();

	// ==========================================
	// 账号本地存储，创房前先 FindSessions 查大厅
	// - 同账号已有建房 → 弹"已有此账户创建的房间"提示，阻止创房
	// - 无/非同号 → 继续走真正的创房流程
	// ==========================================
	FindSessionsForAccountCheck();
}


/**
 * OnDestroySessionComplete
 *
 * 【P0 废弃】原 OnlineSubsystem 直调 DestroySession 的回调
 * 现已替换为 OnDestroyRoomBeforeCreateFromManager (走 SessionManager 链)
 * 保留函数仅为避免 Blueprint 反射丢失, 内部不再有实际逻辑
 */
void ULANRoomPage::OnDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	// 【P0】委托清理由 SessionManager 内部完成, 此函数已无业务职责
}


/**
 * HostRealSession
 *
 * 真正执行创建房间的代码
 * 1. 绑定 CreateSession 回调
 * 2. 配置 FOnlineSessionSettings（LAN、10 人、广告、Presence、JIP）
 * 3. 通过 SessionSettings.Set 写入 ROOM_NAME/GAME_MODE/MAP_NAME/ROOM_STATE/TOTAL_PLAYERS_WITH_AI
 * 4. 调用 CreateSession
 */
void ULANRoomPage::HostRealSession()
{
	// 【P0】走 SessionManager->CreateRoom, 由 SessionManager 内部封装 SessionSettings + 委托管理
	if (UGameInstance* GI = GetGameInstance())
	{
		if (USessionManagerSubsystem* SessionManager = GI->GetSubsystem<USessionManagerSubsystem>())
		{
			// ---- 收集房主账号 ----
			FString HostAccountName = TEXT("");
			if (UAccountService* AccountSub = UAccountService::Get(this))
			{
				HostAccountName = AccountSub->GetCurrentUser();
			}

			// ---- 构造 FRoomCreationParams ----
			FRoomCreationParams Params;
			Params.RoomName = PendingRoomName;
			Params.Password = PendingRoomPassword;
			Params.GameMode = PendingGameMode;
			Params.MapName = PendingMapLevelName.ToString();
			Params.LevelName = PendingMapLevelName;
			Params.MaxPlayers = 10;
			Params.HostAccount = HostAccountName;

			// ---- 订阅 SessionManager 完成回调 (Dynamic 委托必须 UFUNCTION) ----
			FOnCreateRoomComplete CreateDelegate;
			CreateDelegate.BindDynamic(this, &ULANRoomPage::OnCreateRoomFromManager);
			SessionManager->CreateRoom(Params, CreateDelegate);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[LANRoomPage] HostRealSession: SessionManager 不可用"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[LANRoomPage] HostRealSession: GameInstance 不可用"));
	}
}


/**
 * OnCreateRoomFromManager
 *
 * 【P0 架构升级】SessionManager CreateRoom 单播回调 (Dynamic 委托)
 * 收到后转交原有 OnCreateSessionComplete 走 OpenLevel ?listen 流程
 */
void ULANRoomPage::OnCreateRoomFromManager(bool bWasSuccessful, const FString& ErrorMessage)
{
	OnCreateSessionComplete(NAME_GameSession, bWasSuccessful);
}


// ==========================================
// 8. 创房结果回调
// ==========================================

/**
 * OnCreateSessionComplete
 *
 * 创建会话完成回调
 * 1. 内存管理规范: 无论结果如何，第一时间退订委托
 * 2. 成功 -> 标记 bIsHost=true、bIsTraveling=true（防误杀）
 * 3. 成功 -> 计算 TargetMapName（有就玩家选，否则回退 L_Room）
 * 4. 成功 -> URLOptions = "?listen" 打开监听服务器
 * 5. 成功 -> UGameplayStatics::OpenLevel 以 ListenServer 模式开图
 * 6. 失败 -> 显示错误提示
 */
void ULANRoomPage::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	// ==========================================
	// 【DEBUG-SET-3-A】入口: CreateSession 完成回调 (Host 端)
	// ==========================================
	UE_LOG(LogTemp, Error,
		TEXT("[DEBUG-S3-A][OnCreateSessionComplete] PID=%u WorldName=%s bWasSuccessful=%d NetMode=%d IsInViewport=%d"),
		FPlatformProcess::GetCurrentProcessId(),
		*GetWorld()->GetName(),
		bWasSuccessful ? 1 : 0,
		(int32)GetWorld()->GetNetMode(),
		IsInViewport() ? 1 : 0);

	// 【P0】原 OnlineSubsystem 委托清理已废弃 (改由 SessionManager 内部管理)

	if (bWasSuccessful)
	{
		bIsHost = true; // 确立房主权威身份

		// 发放免死金牌! 告诉 NativeDestruct 我们是进行关卡跳转，禁止误杀 Session
		bIsTraveling = true;

		// ==========================================
		// 【核心修复】: 摒弃硬编码，使用玩家在 UI 选择的数据驱动关卡名
		// 增加防呆设计: 如果解析出的名字为空，则回退到保底大厅 "L_Room"
		// ==========================================
		FName TargetMapName = PendingMapLevelName.IsNone() ? FName("L_Room") : PendingMapLevelName;

		// ==========================================
		// 构建网络传输的 URL Options
		// ?listen : 告诉引擎以 Listen Server（监听服务器）模式打开此关卡
		// ==========================================
		FString URLOptions = TEXT("?listen");

		/* * 【高级架构扩展建议】:
		 * 如果你需要根据 PendingGameMode 强行替换当前地图的 GameMode，可以拼接到 URL 中
		 * 例如: URLOptions += FString::Printf(TEXT("?game=%s"), *TargetGameModeClassPath);
		 * 目前若你的地图已经在编辑器 World Settings 里配置了正确的 GameMode，则无需这行
		 */

		// 日志追踪: 帮助快速定位多端跳转问题
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green, FString::Printf(TEXT("创房成功! 正在作为服务器跳转至关卡: %s"), *TargetMapName.ToString()));
		}

		// 执行绝对跳转 (TRAVEL_Absolute)
		// ==========================================
		// 【DEBUG-SET-3-B】即将 OpenLevel (Host 端触发 ServerTravel)
		// ==========================================
		UE_LOG(LogTemp, Error,
			TEXT("[DEBUG-SET-3-B][OpenLevel-Host] PID=%u WorldName=%s TargetMap=%s URL=%s IsInViewport=%d"),
			FPlatformProcess::GetCurrentProcessId(),
			*GetWorld()->GetName(),
			*TargetMapName.ToString(),
			*URLOptions,
			IsInViewport() ? 1 : 0);

		UGameplayStatics::OpenLevel(GetWorld(), TargetMapName, true, URLOptions);
	}
	else
	{
		// 失败提示处理保持不变
		if (Text_CreateRoomHint)
		{
			Text_CreateRoomHint->SetText(FText::FromString(TEXT("创建房间失败，请检查网络或重启游戏重试!")));
			Text_CreateRoomHint->SetVisibility(ESlateVisibility::Visible);
		}

		// 【Bug3 修复】: 创房失败时重新启用按钮，让玩家可以修改后重试
		ReEnableCreateRoomButton();
	}
}


/**
 * OnHideCreateRoomClicked
 *
 * 取消按钮: 隐藏创房弹窗
 */
void ULANRoomPage::OnHideCreateRoomClicked()
{
	// 取消创房，隐藏面板
	if (Overlay_CreateRoom) Overlay_CreateRoom->SetVisibility(ESlateVisibility::Hidden);

	// 【Bug3 修复】: 取消操作也需要重新启用按钮，否则下次打开弹窗时按钮仍是禁用态
	ReEnableCreateRoomButton();
}


/**
 * OnToggleReadyClicked
 *
 * 切换准备状态按钮
 */
void ULANRoomPage::OnToggleReadyClicked()
{
	// 翻转状态
	bIsReady = !bIsReady;
}


// ==========================================
// 10. 创房前"同号检查"搜索（新增）
// ==========================================

/**
 * FindSessionsForAccountCheck
 *
 * 创房前专用搜索: 查大厅是否有"我的账号"已建好的房间
 *
 * 设计:
 *  - 走独立委托句柄 (AccountCheckFindSessionsDelegateHandle)
 *    不与常规 FindLANRooms 冲突
 *  - 不刷新 CurrentDisplayedRooms (本函数不显示列表, 只查)
 *  - 不弹 UI 提示, 回调里集中处理
 */
void ULANRoomPage::FindSessionsForAccountCheck()
{
	// 【P0】走 SessionManager->FindRooms, 由 SessionManager 内部管理状态锁/委托生命周期
	if (UGameInstance* GI = GetGameInstance())
	{
		if (USessionManagerSubsystem* SessionManager = GI->GetSubsystem<USessionManagerSubsystem>())
		{
			FOnFindRoomsComplete FindDelegate;
			FindDelegate.BindDynamic(this, &ULANRoomPage::OnAccountCheckFindRoomsFromManager);
			SessionManager->FindRooms(FindDelegate);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[LANRoomPage] SessionManager 不可用, 放行"));
			// SessionManager 不可用, 直接放行让 HostRealSession 自己报错
			ProceedToCreateRoomAfterCheck();
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[LANRoomPage] GameInstance 不可用, 放行"));
		ProceedToCreateRoomAfterCheck();
	}
}


/**
 * OnAccountCheckFindRoomsFromManager
 *
 * 【P0 架构升级】SessionManager FindRooms 单播回调 (Dynamic 委托)
 * 用于创房前同号检查: 从 FRoomSessionResult 缓存读 HOST_ACCOUNT, 不再直读 SessionSettings
 */
void ULANRoomPage::OnAccountCheckFindRoomsFromManager(bool bWasSuccessful, const TArray<FRoomSessionResult>& Rooms)
{
	// 缓存结果, 给 OnAccountCheckFindSessionsComplete 用
	AccountCheckRoomsCache = Rooms;

	// 委托原 OnAccountCheckFindSessionsComplete 处理 (它会从 AccountCheckRoomsCache 读取)
	OnAccountCheckFindSessionsComplete(bWasSuccessful);
}


/**
 * OnAccountCheckFindSessionsComplete
 *
 * 同号检查的搜索回调
 * 1. 遍历所有结果, 比对 HOST_ACCOUNT 字段
 * 2. 若有同账号建房 → 弹"已有此账户创建的房间"提示, 不创房
 * 3. 若无 → 继续走真正的创房流程
 */
void ULANRoomPage::OnAccountCheckFindSessionsComplete(bool bWasSuccessful)
{
	// 【P0】原 OnlineSubsystem 委托清理已废弃 (改由 SessionManager 内部管理)

	// 搜索失败: 不当误创房, 让 HostRealSession 自己处理失败
	if (!bWasSuccessful)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow,
				TEXT("[CreateRoom] 同号检查搜索失败, 放行创房"));
		}
		AccountCheckRoomsCache.Reset();
		ProceedToCreateRoomAfterCheck();
		return;
	}

	// 拿到当前账号名
	FString MyAccountName = TEXT("");
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountService* AccountSub = UAccountService::Get(this))
		{
			MyAccountName = AccountSub->GetCurrentUser();
		}
	}

	if (MyAccountName.IsEmpty())
	{
		// 拿不到账号名, 放行让 HostRealSession 处理
		AccountCheckRoomsCache.Reset();
		ProceedToCreateRoomAfterCheck();
		return;
	}

	// 【P0】遍历 SessionManager 提供的 FRoomSessionResult 缓存, 查 HOST_ACCOUNT
	for (const FRoomSessionResult& Room : AccountCheckRoomsCache)
	{
		// ==========================================
		// 【核心校验】: 同账号已有建房 → 拦截!
		// ==========================================
		if (Room.HostAccount.Equals(MyAccountName, ESearchCase::IgnoreCase))
		{
			// 弹提示, 阻止创房
			if (Text_CreateRoomHint)
			{
				Text_CreateRoomHint->SetText(FText::FromString(
					FString::Printf(TEXT("此账号在别的客户端已经登陆并创房，您无法创建房间，请更换账号重新登陆！"))));
				Text_CreateRoomHint->SetVisibility(ESlateVisibility::Visible);
			}

			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red,
					FString::Printf(TEXT("[CreateRoom] 拦截: 账号 [%s] 已存在建房"), *MyAccountName));
			}

			// 清缓存, 不创房
			AccountCheckRoomsCache.Reset();

			// 【Bug3 修复】: 同号检查拦截, 重新启用按钮让玩家可以重试
			ReEnableCreateRoomButton();
			return;
		}
	}

	// 清缓存
	AccountCheckRoomsCache.Reset();

	// 没有同账号建房, 放行创房
	ProceedToCreateRoomAfterCheck();
}


/**
 * ProceedToCreateRoomAfterCheck
 *
 * 同号检查通过后, 真正开始执行创房流程
 * (从原 OnConfirmCreateRoomClicked 末尾搬过来)
 */
void ULANRoomPage::ProceedToCreateRoomAfterCheck()
{
	// 更新提示
	if (Text_CreateRoomHint)
	{
		Text_CreateRoomHint->SetText(FText::FromString(TEXT("正在创建房间，请稍候...")));
		Text_CreateRoomHint->SetVisibility(ESlateVisibility::Visible);
	}

	// 【P0】走 SessionManager->DestroyRoom / CreateRoom, 由 SessionManager 内部管理委托 + 状态锁
	if (UGameInstance* GI = GetGameInstance())
	{
		if (USessionManagerSubsystem* SessionManager = GI->GetSubsystem<USessionManagerSubsystem>())
		{
			// 【修复Bug的核心】: 检查 SessionManager 是不是已经 Hosting (内存里卡着旧房间)
			if (SessionManager->IsHosting())
			{
				// 如果有旧的，先订阅销毁回调, 销毁后再触发创房
				FOnDestroyRoomComplete DestroyDelegate;
				DestroyDelegate.BindDynamic(this, &ULANRoomPage::OnDestroyRoomBeforeCreateFromManager);
				SessionManager->DestroyRoom(DestroyDelegate);
			}
			else
			{
				// 如果没有旧的，直接开始创建
				HostRealSession();
			}
		}
	}
}


/**
 * OnDestroyRoomBeforeCreateFromManager
 *
 * 【P0 架构升级】SessionManager DestroyRoom 单播回调 (Dynamic 委托)
 * 用于创房前清理旧房间: 销毁完后转 HostRealSession 创新房
 */
void ULANRoomPage::OnDestroyRoomBeforeCreateFromManager(bool bWasSuccessful, const FString& ErrorMessage)
{
	// 不管销毁成功/失败, 都尝试创建新房间 (SessionManager 内部幂等)
	HostRealSession();
}


// ==========================================
// 11. 账号冲突模态对话框（从 LoginPage 迁移）
// ==========================================

/**
 * ShowLANRoomConflictDialog
 *
 * 外部调用入口: 弹模态对话框
 * 被 ARoomPlayerController::HandleForcedKickNotification 反射调用
 *
 * 行为:
 *  1. 显示 Overlay_LANRoomConflict + Text_ConflictMsg
 *  2. 改输入模式为 UIOnly(确保鼠标能点按钮)
 */
void ULANRoomPage::ShowLANRoomConflictDialog()
{
	// 1. 显示对话框容器
	if (Overlay_LANRoomConflict)
	{
		Overlay_LANRoomConflict->SetVisibility(ESlateVisibility::Visible);
	}

	// 2. 显示提示文本
	if (Text_ConflictMsg)
	{
		Text_ConflictMsg->SetText(FText::FromString(TEXT("此账号在别的客户端已经登陆并创房，您无法创建房间，请更换账号重新登陆！")));
	}

	// 3. 输入模式切到 UIOnly + 显示鼠标
	APlayerController* PC = GetOwningPlayer();
	if (!PC && GetWorld())
	{
		PC = GetWorld()->GetFirstPlayerController();
	}

	if (PC)
	{
		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
		PC->SetShowMouseCursor(true);
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow,
			TEXT("[LANRoomPage] 弹冲突对话框"));
	}
}


/**
 * OnConfirmLANRoomConflictClicked
 *
 * 玩家点 [确认] → 切回大厅(不退出账号!)
 *
 * 注意: 用户想退出登录, 应该自己到 GameMenuPage 点 Btn_BackToLogin
 *       这里只负责把玩家带回大厅, 账号保持登录态
 */
void ULANRoomPage::OnConfirmLANRoomConflictClicked()
{
	// 1. 关闭对话框
	if (Overlay_LANRoomConflict)
	{
		Overlay_LANRoomConflict->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 2. 切回大厅(不退出账号)
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGameFlowSubsystem* FlowSub = GI->GetSubsystem<UGameFlowSubsystem>())
		{
			FlowSub->TransitToState(EMatchState::MainLobby);
		}

		// 故意不调 AccountSubsystem->Logout()
		// 用户的账号保持登录态, 切回大厅后可继续操作或自己点 Btn_BackToLogin 换号
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green,
			TEXT("[LANRoomPage] 玩家确认, 已返回大厅(账号保持登录)"));
	}
}
// ==========================================
// 【架构升级】View 接口实现
// ==========================================

/**
 * ULANRoomPage::OnViewShown
 *
 * View 绑定后由 UIViewService 调用
 */
void ULANRoomPage::OnViewShown()
{
    if (UGameInstance* GI = GetGameInstance())
    {
        if (ULANRoomPresenter* Presenter = GI->GetSubsystem<ULANRoomPresenter>())
        {
            // 订阅 Presenter 的状态变化（用 Lambda 包装, 避免额外声明函数）
            Presenter->OnStateChanged.AddDynamic(this, &ULANRoomPage::HandlePresenterStateChangedForView);
            Presenter->OnRoomListRefreshed.AddDynamic(this, &ULANRoomPage::HandlePresenterRoomListRefreshedForView);
            Presenter->OnErrorOccurred.AddDynamic(this, &ULANRoomPage::HandlePresenterErrorForView);
            // 通知 Presenter View 已显示, 触发首次刷新
            Presenter->OnWidgetShow();
        }
    }
}

/**
 * ULANRoomPage::OnViewHidden
 */
void ULANRoomPage::OnViewHidden()
{
    if (UGameInstance* GI = GetGameInstance())
    {
        if (ULANRoomPresenter* Presenter = GI->GetSubsystem<ULANRoomPresenter>())
        {
            Presenter->OnStateChanged.RemoveDynamic(this, &ULANRoomPage::HandlePresenterStateChangedForView);
            Presenter->OnRoomListRefreshed.RemoveDynamic(this, &ULANRoomPage::HandlePresenterRoomListRefreshedForView);
            Presenter->OnErrorOccurred.RemoveDynamic(this, &ULANRoomPage::HandlePresenterErrorForView);
            Presenter->OnWidgetHide();
        }
    }
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(SearchTimerHandle);
    }
}

// 桩函数: 简单委托给 View 现有的刷新逻辑
void ULANRoomPage::HandlePresenterStateChangedForView()
{
    // TODO: 根据 Presenter CurrentState 切换 UI 状态
}

void ULANRoomPage::HandlePresenterRoomListRefreshedForView()
{
    // 委托 Presenter 重新拉取房间列表
    if (UGameInstance* GI = GetGameInstance())
    {
        if (ULANRoomPresenter* Presenter = GI->GetSubsystem<ULANRoomPresenter>())
        {
            Presenter->RequestRefreshRoomList();
        }
    }
}

void ULANRoomPage::HandlePresenterErrorForView(const FString& ErrorMessage)
{
    if (Text_CreateRoomHint)
    {
        Text_CreateRoomHint->SetText(FText::FromString(ErrorMessage));
        Text_CreateRoomHint->SetVisibility(ESlateVisibility::Visible);
    }
}


// ==========================================
// 12. 创房按钮状态管理 helper 实现
// ==========================================
// 设计原则 (DRY):
//   - 所有创房流程的"异步开始 → 禁用按钮 / 失败终止 → 启用按钮"
//     必须经由此处两个 helper, 避免散落在多处导致状态不一致
//   - .h 中只前向声明, 函数体在 .cpp 内实现
//     (因 .h 里只有 class UButton 前向声明, 编译器看不到 SetIsEnabled 接口)

/**
 * 禁用创房按钮: 异步操作开始时调用
 * 设计理由: 防止异步过程中玩家重复点击导致多次创房请求
 */
void ULANRoomPage::DisableCreateRoomButton()
{
    if (Btn_ConfirmCreateRoom)
    {
        Btn_ConfirmCreateRoom->SetIsEnabled(false);
        UE_LOG(LogTemp, Warning, TEXT("[LANRoomPage] Btn_ConfirmCreateRoom 已禁用 (异步操作中)"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LANRoomPage] DisableCreateRoomButton: Btn_ConfirmCreateRoom 为空!"));
    }
}

/**
 * 启用创房按钮: 创房失败/取消时调用
 * 设计理由: 异步终止后必须恢复 UI 可用性, 否则玩家卡死
 */
void ULANRoomPage::ReEnableCreateRoomButton()
{
    if (Btn_ConfirmCreateRoom)
    {
        Btn_ConfirmCreateRoom->SetIsEnabled(true);
        UE_LOG(LogTemp, Warning, TEXT("[LANRoomPage] Btn_ConfirmCreateRoom 已恢复可用"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LANRoomPage] ReEnableCreateRoomButton: Btn_ConfirmCreateRoom 为空!"));
    }
}