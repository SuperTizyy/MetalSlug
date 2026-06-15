// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"
// 引入用户控件基类
#include "Blueprint/UserWidget.h"
// 【新增】引入在线会话接口的头文件
#include "Interfaces/OnlineSessionInterface.h"
// UE 自动生成的头文件
#include "LANRoomPage.generated.h"

// 前向声明所有用到的 UI 控件，加快编译速度
class UScrollBox;
class UListView;
class UEditableTextBox;
class UVerticalBox;
class UButton;
class UOverlay;
class UTextBlock;
class UComboBoxString;
class UImage;
class UDataTable;
class URoomLabelWidget; // 【新增】前向声明房间条目类


/**
 * @class ULANRoomPage
 * @brief 局域网大厅与房间页面
 *
 * 职责说明:
 * - 大厅层: 房间列表、自动刷新、加入房间
 * - 创房层: 弹窗面板、命名/密码/模式/地图选择、创建会话
 * - 房间内: 已通过 OnEnterRoomClicked 进入战斗地图
 *
 * 架构理念:
 * 1. UE 在线子系统: 完全基于 IOnlineSubsystem 创/搜/加会话
 * 2. 自动刷新: 3 秒一次定时器扫描局域网
 * 3. 状态签名防闪烁: 用 "房间名_人数_最大_状态" 字符串作为签名
 * 4. Session 元数据: 房间名/游戏模式/地图名 都通过 SessionSettings 传输
 */
UCLASS()
class METALSLUG01_API ULANRoomPage : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. UI 跳转配置区域
	// ==========================================

	/**
	 * 主菜单页面类
	 * 用途: 在编辑器中选择 WBP_GameMenu 蓝图类
	 * 点击"返回主菜单"按钮后动态创建并显示此页面
	 */
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<class UUserWidget> GameMenuClass;

	/**
	 * 房间条目 Widget 类
	 * 用途: 用于在房间列表中显示单个房间的 UI
	 */
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<class URoomLabelWidget> RoomLabelClass;

protected:
	// ==========================================
	// 2. 生命周期
	// ==========================================

	/**
	 * 重写 UserWidget 的初始化函数
	 * 用途: 绑定所有按钮事件、初始化搜索定时器、初始化下拉框
	 */
	virtual bool Initialize() override;

	// ==========================================
	// 3. 大厅基础层控件 (Lobby Base)
	// ==========================================

	/**
	 * 房间列表容器（后续往里动态添加房间条目）
	 */
	UPROPERTY(meta = (BindWidget))
	UScrollBox* List_Rooms;

	/**
	 * 聊天列表容器（暂时不做逻辑）
	 */
	UPROPERTY(meta = (BindWidget))
	UScrollBox* List_Chat;

	/**
	 * 聊天输入框
	 */
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* Input_Chat;

	/**
	 * 成员列表框（大厅的所有成员）
	 */
	UPROPERTY(meta = (BindWidget))
	UVerticalBox* Box_Members;

	/**
	 * 加入选中的房间按钮
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_EnterRoom;

	/**
	 * 打开创建房间的弹窗面板按钮
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_ShowCreateRoom;

	/**
	 * 返回主菜单按钮
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_BackToMenu;

	// ==========================================
	// 3.5 账号冲突模态对话框（从 LoginPage 迁移）
	// 触发场景: 房主拒收本客户端 (Client_LoginResult bReject=true)
	// 职责: 在大厅页面弹模态框, 玩家点 [确认] → 保持账号登录态, 仅回大厅
	// 注意: 蓝图里要拖 3 个同名控件进 WBP_LANRoomPage
	// 默认 Overlay 全部 Collapsed
	// ==========================================

	/**
	 * 模态对话框容器
	 * 蓝图侧: 建议放 1 个 Border + 1 个 VerticalBox(包含 Text + 按钮)
	 */
	UPROPERTY(meta = (BindWidget))
	UOverlay* Overlay_LANRoomConflict;

	/**
	 * 对话框内显示的提示文本
	 * 例如: "账号 [甲] 已在房间 [房间名] 中, 不允许重复进房"
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_ConflictMsg;

	/**
	 * 确认按钮 (强踢后玩家点这个 → 留在大厅, 不退账号)
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_ConfirmLANRoomConflict;

	// ==========================================
	// 4. 创建房间覆盖面板 (Create Room Overlay)
	// ==========================================

	/**
	 * 创房覆盖面板（控制显示/隐藏）
	 */
	UPROPERTY(meta = (BindWidget))
	UOverlay* Overlay_CreateRoom;

	/**
	 * 输入: 房间名称
	 */
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* Input_RoomName;

	/**
	 * 输入: 房间密码
	 */
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* Input_RoomPassword;

	/**
	 * 确认创建并进入房间按钮
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_ConfirmCreateRoom;

	/**
	 * 关闭创房面板（取消）按钮
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_HideCreateRoom;

	/**
	 * 创房提示框: 用于给用户显示"名字为空"或"重名"等错误信息
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_CreateRoomHint;

	// ==========================================
	// 5. 游戏模式与地图选择 (Create Room Overlay)
	// ==========================================

	/**
	 * 游戏模式选择下拉框
	 */
	UPROPERTY(meta = (BindWidget))
	UComboBoxString* ComboBox_GameMode;

	/**
	 * 地图选择下拉框
	 */
	UPROPERTY(meta = (BindWidget))
	UComboBoxString* ComboBox_MapSelect;

	/**
	 * 地图信息数据表（用于填充地图下拉框）
	 * 用途: 关联 DT_MapInfo
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Data Config")
	UDataTable* MapInfoDataTable;

private:
	// ==========================================
	// 6. 按钮点击响应函数
	// ==========================================

	/** 大厅层: 打开创房弹窗 */
	UFUNCTION() void OnShowCreateRoomClicked();

	/** 大厅层: 加入选中的房间 */
	UFUNCTION() void OnEnterRoomClicked();

	/** 大厅层: 返回主菜单 */
	UFUNCTION() void OnBackToMenuClicked();

	/** 创房层: 确认创建房间 */
	UFUNCTION() void OnConfirmCreateRoomClicked();

	/** 创房层: 隐藏创房弹窗 */
	UFUNCTION() void OnHideCreateRoomClicked();

	/** 房间内层: 离开房间 */
	UFUNCTION() void OnLeaveRoomClicked();

	/** 房间内层: 切换准备状态 */
	UFUNCTION() void OnToggleReadyClicked();

	// ==========================================
	// 6.5 账号冲突对话框回调（从 LoginPage 迁移）
	// ==========================================

	/**
	 * 外部调用入口: 弹模态对话框
	 * @param Reason 冲突原因(例如: "账号已在房间中")
	 *
	 * 被谁调用:
	 *  - ARoomPlayerController::HandleForcedKickNotification → 反射调到这里
	 *
	 * 内部行为:
	 *  - 显示 Overlay_LANRoomConflict + Text_ConflictMsg
	 *  - 改输入模式为 UIOnly + 显示鼠标
	 */
	UFUNCTION(BlueprintCallable, Category = "LANRoom|Account")
	void ShowLANRoomConflictDialog(const FString& Reason);

	/**
	 * 确认按钮回调: 玩家点"确认" → 切回大厅(不退出账号)
	 * 用户想退出登录 → 自己点 GameMenuPage 的 Btn_BackToLogin
	 */
	UFUNCTION()
	void OnConfirmLANRoomConflictClicked();

	// ==========================================
	// 7. 局域网会话底层逻辑
	// ==========================================

	/**
	 * 底层创建会话完成的引擎回调
	 * @param SessionName 会话名
	 * @param bWasSuccessful 是否成功
	 */
	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);

	/**
	 * 创建会话完成的委托句柄（用于注销清理）
	 */
	FDelegateHandle CreateSessionCompleteDelegateHandle;

	/**
	 * 暂存玩家想创建的房间名和密码
	 * 用途: 销毁旧房间后再继续创建
	 */
	FString PendingRoomName;
	FString PendingRoomPassword;

	/**
	 * 暂存玩家选择的游戏模式和地图
	 */
	FString PendingGameMode;
	FName PendingMapLevelName;

	/** 真正执行创房的内部代码 */
	void HostRealSession();

	/**
	 * 销毁会话完成的引擎回调
	 * 解决玩家 0 创房失败的 Bug
	 */
	void OnDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	/** 销毁会话完成的委托句柄 */
	FDelegateHandle DestroySessionCompleteDelegateHandle;

	/** 触发搜索 */
	void FindLANRooms();

	/** 搜索完成的引擎回调 */
	void OnFindSessionsComplete(bool bWasSuccessful);

	/** 搜索完成的委托句柄（退订凭证） */
	FDelegateHandle FindSessionsCompleteDelegateHandle;

	/**
	 * 引擎提供的搜索设置容器（必须用 TSharedPtr 智能指针包起来）
	 */
	TSharedPtr<class FOnlineSessionSearch> SessionSearch;

	// ==========================================
	// 8.1 创房前的"同号检查"搜索（新增）
	// ==========================================

	/**
	 * 创房前专用: 搜索大厅是否有"我的账号"已建好的房间
	 * 区别于 FindLANRooms: 本函数只查 HOST_ACCOUNT 这一项, 不刷新 CurrentDisplayedRooms
	 *
	 * 用法: 在 OnConfirmCreateRoomClicked 末尾调一次
	 *      → OnAccountCheckFindSessionsComplete 回调里决定放行或弹框
	 */
	void FindSessionsForAccountCheck();

	/**
	 * 创房前搜索完成的引擎回调
	 * 检查所有结果的 HOST_ACCOUNT 是否与当前账号名相同
	 * - 相同: 弹"已有此账户创建的房间"提示, 不创建
	 * - 不同/无: 继续走真正的创房流程
	 */
	void OnAccountCheckFindSessionsComplete(bool bWasSuccessful);

	/** 创房前搜索的委托句柄(独立于常规搜索,避免冲突) */
	FDelegateHandle AccountCheckFindSessionsDelegateHandle;

	/** 创房前搜索的临时结果缓存(回调里用) */
	TSharedPtr<class FOnlineSessionSearch> AccountCheckSessionSearch;

	/**
	 * 同号检查通过后, 真正开始执行创房流程
	 * 从 OnConfirmCreateRoomClicked 末尾搬过来, 由 OnAccountCheckFindSessionsComplete 调用
	 *
	 * 为什么单独抽出来: 创房前要异步等 FindSessions 回调, 不能再同步调用
	 * 把"创房"动作搬到异步回调里
	 */
	void ProceedToCreateRoomAfterCheck();

	// ==========================================
	// 8. 搜索与刷新相关
	// ==========================================

	/**
	 * 定时器句柄，用于自动刷新房间列表（3 秒一次）
	 */
	FTimerHandle SearchTimerHandle;

	/**
	 * 搜索状态锁（防止上一次没搜完，下一次又开始了）
	 */
	bool bIsSearching = false;

	/**
	 * 重写销毁函数
	 * 用途: 关闭界面时清理定时器，防止内存泄漏崩溃
	 */
	virtual void NativeDestruct() override;

	/**
	 * 身份标识: 记录自己是不是房主
	 */
	bool bIsHost = false;

	/**
	 * 用于记录当前屏幕上已经显示出来的房间列表
	 * 用途: 防止 UI 无意义刷新导致闪烁
	 */
	TArray<FString> CurrentDisplayedRooms;

	/**
	 * 用于防 UI 闪烁的"状态签名"数组
	 * 格式: 房间名_当前人数_最大人数_是否在战斗
	 * 专门用来检测人数是否变化
	 */
	TArray<FString> CurrentRoomSignatures;

	/**
	 * 记录当前玩家的准备状态
	 */
	bool bIsReady = false;

	// ==========================================
	// 9. 加入房间与队伍分配逻辑
	// ==========================================

	/**
	 * 记录玩家当前在列表中选中的房间名
	 */
	FString CurrentSelectedRoomName;

	/**
	 * 房间条目被点击时的回调（用于高亮选中）
	 */
	UFUNCTION()
	void HandleRoomSelected(FString RoomName);

	/**
	 * 底层加入房间完成后的回调
	 */
	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);

	/**
	 * 加入房间的委托句柄
	 */
	FDelegateHandle JoinSessionCompleteDelegateHandle;

	/**
	 * 标记是否正在传送到新关卡
	 * 用途: NativeDestruct 时区分"房主离开"和"正常跳转"
	 */
	bool bIsTraveling = false;

	/**
	 * 声明一个委托对象（相当于订阅单）
	 */
	FOnFindSessionsCompleteDelegate FindSessionsCompleteDelegate;
};
