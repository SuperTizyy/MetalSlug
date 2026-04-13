#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
// 【新增】引入在线会话接口的头文件
#include "Interfaces/OnlineSessionInterface.h"
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
class URoomLabelWidget; // 【新增】前向声明我们刚才写的房间条目类

/**
 * 局域网大厅与房间页面
 * 包含大厅列表、创房弹窗、房间内红蓝对抗面板的综合 UI 控制器
 */
UCLASS()
class METALSLUG01_API ULANRoomPage : public UUserWidget
{
	GENERATED_BODY()
	
public:
	// ==========================================
	// UI 跳转配置区域
	// ==========================================
	
	// 暴露给蓝图的变量，用于在编辑器中选择你要返回的主菜单蓝图类（WBP_GameMenu）
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<class UUserWidget> GameMenuClass;
	
	// 暴露给蓝图，用于在编辑器中选择子条目蓝图（WBP_RoomLabel）
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<class URoomLabelWidget> RoomLabelClass;
	
	

protected:
	virtual bool Initialize() override;

	// ==========================================
	// 1. 大厅基础层控件 (Lobby Base)
	// ==========================================

	// 房间列表 (后续往里动态添加房间条目)
	UPROPERTY(meta = (BindWidget))
	UScrollBox* List_Rooms;

	// 聊天列表 (暂时不做逻辑)
	UPROPERTY(meta = (BindWidget))
	UScrollBox* List_Chat;

	// 聊天输入框
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* Input_Chat;

	// 成员列表框 (大厅的所有成员)
	UPROPERTY(meta = (BindWidget))
	UVerticalBox* Box_Members;

	// 加入选中的房间
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_EnterRoom;

	// 打开创建房间的弹窗面板
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_ShowCreateRoom;

	// 返回主菜单
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_BackToMenu;

	// ==========================================
	// 2. 创建房间覆盖面板 (Create Room Overlay)
	// ==========================================

	// 创房覆盖面板 (控制显示/隐藏)
	UPROPERTY(meta = (BindWidget))
	UOverlay* Overlay_CreateRoom;

	// 输入：房间名称
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* Input_RoomName;

	// 输入：房间密码
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* Input_RoomPassword;

	// 确认创建并进入房间
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_ConfirmCreateRoom;

	// 关闭创房面板 (取消)
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_HideCreateRoom;
	
	// 创房提示框：用于给用户显示“名字为空”或“重名”等错误信息
	UPROPERTY(meta = (BindWidget)) 
	UTextBlock* Text_CreateRoomHint;
	
	// ==========================================
	// 3. 游戏模式与地图选择 (Create Room Overlay)
	// ==========================================
	
	// 游戏模式选择下拉框
	UPROPERTY(meta = (BindWidget)) 
	UComboBoxString* ComboBox_GameMode;
	
	// 地图选择下拉框
	UPROPERTY(meta = (BindWidget)) 
	UComboBoxString* ComboBox_MapSelect;
	
	// 地图信息数据表 (用于填充地图下拉框)
	UPROPERTY(EditDefaultsOnly, Category = "Data Config")
	UDataTable* MapInfoDataTable;


private:
	// ==========================================
	// 按钮点击响应函数
	// ==========================================

	// -- 大厅层 --
	UFUNCTION() void OnShowCreateRoomClicked();
	UFUNCTION() void OnEnterRoomClicked();
	UFUNCTION() void OnBackToMenuClicked();

	// -- 创房层 --
	UFUNCTION() void OnConfirmCreateRoomClicked();
	UFUNCTION() void OnHideCreateRoomClicked();

	// -- 房间内层 --
	UFUNCTION() void OnLeaveRoomClicked();
	UFUNCTION() void OnToggleReadyClicked();
	
	// ==========================================
	// 【新增】局域网会话底层逻辑
	// ==========================================
	
	// 当底层创建会话完成时，引擎会自动回调这个函数
	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);

	// 保存委托的句柄，用于在完成后注销清理
	FDelegateHandle CreateSessionCompleteDelegateHandle;

	// ==========================================
	// 【新增与修改】局域网会话底层逻辑
	// ==========================================
	
	// 暂存玩家想创建的房间名和密码（为了在销毁旧房间后能继续创建）
	FString PendingRoomName;
	FString PendingRoomPassword;
	
	// 暂存玩家选择的游戏模式和地图
	FString PendingGameMode;
	FName PendingMapLevelName;

	// 1. 创房相关
	void HostRealSession(); // 真正执行创房的内部代码

	// 2. 销毁相关（解决玩家0创房失败的Bug）
	void OnDestroySessionComplete(FName SessionName, bool bWasSuccessful);
	FDelegateHandle DestroySessionCompleteDelegateHandle;

	// 3. 搜索相关（解决看不见房间的问题）
	void FindLANRooms(); // 触发搜索
	void OnFindSessionsComplete(bool bWasSuccessful);
	
	// 声明一个句柄（相当于退订凭证），用来记住这次订阅！
	FDelegateHandle FindSessionsCompleteDelegateHandle;
	
	// 引擎提供的搜索设置容器（必须用 TSharedPtr 智能指针包起来）
	TSharedPtr<class FOnlineSessionSearch> SessionSearch;
	
	// ==========================================
	// 【新增】搜索与刷新相关
	// ==========================================
	
	// 定时器句柄，用于自动刷新房间列表
	FTimerHandle SearchTimerHandle;
	
	// 搜索状态锁（防止上一次没搜完，下一次又开始了）
	bool bIsSearching = false;

	// 重写销毁函数，用于在关闭界面时清理定时器，防止内存泄漏崩溃
	virtual void NativeDestruct() override;
	
	// 身份标识：记录自己是不是房主
	bool bIsHost = false;
	
	// 用于记录当前屏幕上已经显示出来的房间列表，防止 UI 无意义刷新导致闪烁
	TArray<FString> CurrentDisplayedRooms;
	
	// ==========================================
	// 【新增】：用于防 UI 闪烁的“状态签名”数组
	// (格式为：房间名_当前人数_最大人数，专门用来检测人数是否变化)
	// ==========================================
	TArray<FString> CurrentRoomSignatures;
	
	// ==========================================
	// 内部状态标志
	// ==========================================
	bool bIsReady = false; // 记录当前玩家的准备状态
	
	// ==========================================
	// 【新增】加入房间与队伍分配逻辑
	// ==========================================

	// 记录玩家当前在列表中选中的房间名
	FString CurrentSelectedRoomName;

	// 当某个房间条目被点击时，触发此函数记录名字
	UFUNCTION()
	void HandleRoomSelected(FString RoomName);

	// 底层加入房间完成后的回调函数
	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	
	// 加入房间的委托句柄
	FDelegateHandle JoinSessionCompleteDelegateHandle;
	
	// 【新增】标记是否正在传送到新关卡
	bool bIsTraveling = false;
	
	//  声明一个委托对象（相当于订阅单）
	FOnFindSessionsCompleteDelegate FindSessionsCompleteDelegate;
	
};