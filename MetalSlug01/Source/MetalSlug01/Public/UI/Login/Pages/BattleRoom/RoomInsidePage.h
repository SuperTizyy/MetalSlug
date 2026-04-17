#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ComboBoxString.h"
#include "RoomInsidePage.generated.h"

class UVerticalBox;
class UButton;
class UTextBlock;
class UPlayerLabelWidget; // 之前写的玩家条目
class UScrollBox;
class UEditableTextBox;
class UImage;
class UUniformGridPanel;
class UOverlay;
class UWeaponIconWidget;
class UDataTable;

/**
 * 房间内部 UI 页面（在黑屏的 Map_Lobby 中显示）
 * 负责显示红蓝队、准备、开始游戏，并响应服务器的 RPC 刷新指令
 */
UCLASS()
class METALSLUG01_API URoomInsidePage : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;

	// ==========================================
	// 供对讲机调用的接口：往聊天框里塞入一条新消息
	// ==========================================
	UFUNCTION(BlueprintCallable, Category = "RoomUI")
	void AddChatMessage(const FString& SenderName, bool bIsHost, const FString& Message, bool bIsSystemMsg);
	
	// 供小格子调用的公开接口
	void OnWeaponItemSelectedInGrid(FName WeaponRowName);
	
	// 点击开始游戏按钮
	UFUNCTION() void OnStartGameClicked();
	
	// 【新增】：往聊天框发送系统提示
	void AddSystemMessageToChat(const FString& Message);
	
protected:
	
	// 房间名称展示控件 (蓝图里务必命名为 Text_RoomName)
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_RoomName;
	
	// 重写 UI 构造函数，这里才是读表最安全的地方！
	virtual void NativeConstruct() override;
	void NativeDestruct() override;
	
	// 监听状态改变的回调函数
	UFUNCTION()
	void OnGameFlowStateChanged(EMatchState NewState);


	// ==========================================
	// 从原先 LANRoomPage 搬过来的 UI 控件
	// ==========================================
	//红队列表容器
	UPROPERTY(meta = (BindWidget)) UVerticalBox* Box_RedTeam;
	//蓝队列表容器
	UPROPERTY(meta = (BindWidget)) UVerticalBox* Box_BlueTeam;
	//加入红队
	UPROPERTY(meta = (BindWidget)) UButton* Btn_JoinRedTeam;
	//加入蓝队
	UPROPERTY(meta = (BindWidget)) UButton* Btn_JoinBlueTeam;
	//退出房间
	UPROPERTY(meta = (BindWidget)) UButton* Btn_LeaveRoom;
	//准备/取消准备
	UPROPERTY(meta = (BindWidget)) UButton* Btn_ToggleReady;
	//准备/取消准备文字
	UPROPERTY(meta = (BindWidget)) UTextBlock* Text_ReadyStatus;
	//开始游戏
	UPROPERTY(meta = (BindWidget)) UButton* Btn_StartGame;

	// 动态生成玩家条目所需的蓝图类配置
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<class UPlayerLabelWidget> PlayerLabelClass;
	
	// ==========================================
	// 【新增】：房间最大允许的总人数 (真人 + AI)
	// (对应你 LANRoomPage 创房时的 SessionSettings.NumPublicConnections)
	// ==========================================
	UPROPERTY(EditDefaultsOnly, Category = "Room Config")
	int32 MaxNumPublicConnections = 10;

	// ==========================================
	// 聊天系统控件
	// ==========================================
	
	// 聊天消息列表 (注意：请在蓝图里使用 ScrollBox 控件)
	UPROPERTY(meta = (BindWidget))
	UScrollBox* ScrollBox_ChatList;

	// 聊天输入框
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* Input_Chat;
	
	// ==========================================
	// 【新增】：数据表配置插槽
	// 供你在蓝图里选择刚才建好的 DT_CharacterList
	// ==========================================
	UPROPERTY(EditDefaultsOnly, Category = "Data Config")
	UDataTable* CharacterDataTable;
	
	// 玩家角色选择下拉框
	UPROPERTY(meta = (BindWidget))
	UComboBoxString* ComboBox_CharacterSelect;
	
	// 绑定显示角色头像的图片控件
	UPROPERTY(meta = (BindWidget))
	UImage* Image_CharacterDisplay;
	
	// 点击此按钮，呼出更换武器的弹窗
	// (确保蓝图里这个按钮的名字严格叫 Btn_ChangeWeapon)
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_ChangeWeapon;
	
	// ==========================================
	// 武器选择弹窗 (Overlay) 及其子控件
	// ==========================================

	// 更换武器覆盖面板 (整个弹窗的根节点，用于控制显示/隐藏)
	UPROPERTY(meta = (BindWidget))
	UOverlay* Overlay_WeaponSelect;

	// 物品棋盘格 (用于动态生成存放武器图标的格子)
	UPROPERTY(meta = (BindWidget))
	UUniformGridPanel* Grid_WeaponItems;

	// 物品预览图 (展示当前选中的武器大图)
	UPROPERTY(meta = (BindWidget))
	UImage* Image_WeaponPreview;

	// 隐藏覆盖面板按钮 (取消/关闭)
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_HideWeaponOverlay;

	// 确认更换武器按钮
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_ConfirmWeaponChange;
	
	// ==========================================
	// 军火库数据与细胞蓝图
	// ==========================================
	UPROPERTY(EditDefaultsOnly, Category = "Data Config")
	UDataTable* WeaponDataTable;

	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<UWeaponIconWidget> WeaponItemClass;

	// ==========================================
	// 背包切换按钮
	// ==========================================
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_Inventory1;

	UPROPERTY(meta = (BindWidget))
	UButton* Btn_Inventory2;
	
	// 大厅常驻武器展示控件
	UPROPERTY(meta = (BindWidget))
	UImage* Image_WeaponDisplay;
	
	// ==========================================
	// 背包高亮指示器
	// ==========================================
	
	UPROPERTY(meta = (BindWidget))
	UImage* Image_HighlightBP1;

	UPROPERTY(meta = (BindWidget))
	UImage* Image_HighlightBP2;
	
	// ==========================================
	// 5. AI 控制与设置面板 
	// ==========================================
	
	// 添加AI玩家按钮 
	UPROPERTY(meta = (BindWidget)) 
	UButton* Btn_OpenAIPanel;
	
	// AI 设置覆盖面板 (默认隐藏)
	UPROPERTY(meta = (BindWidget)) 
	UOverlay* Overlay_AddAI;

	// AI 角色选择
	UPROPERTY(meta = (BindWidget)) 
	UComboBoxString* ComboBox_AICharacter;

	// AI 武器选择
	UPROPERTY(meta = (BindWidget)) 
	UComboBoxString* ComboBox_AIWeapon;

	// AI 队伍选择 (红队/蓝队)
	UPROPERTY(meta = (BindWidget)) 
	UComboBoxString* ComboBox_AITeam;

	// 添加 AI 的人数
	UPROPERTY(meta = (BindWidget)) 
	UEditableTextBox* Input_AICount;

	// 确认添加 AI 按钮 
	UPROPERTY(meta = (BindWidget)) 
	UButton* Btn_ConfirmAddAI;

	// 关闭 AI 面板按钮 
	UPROPERTY(meta = (BindWidget)) 
	UButton* Btn_HideAddAI;
	
	// 添加 AI 的提示信息框
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_AddAIHint;

private:
	// 按钮点击事件
	UFUNCTION() void OnLeaveRoomClicked();
	
	UFUNCTION() void OnJoinRedTeamClicked();
	UFUNCTION() void OnJoinBlueTeamClicked();
	UFUNCTION() void OnToggleReadyClicked();

	bool bIsReady = false;
	
	// 监听玩家在输入框按下回车键发送消息
	UFUNCTION()
	void OnChatTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	
	// 监听下拉框切换事件的函数
	UFUNCTION()
	void OnCharacterSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	
	// 【新增】：专门用来刷新头像的底层小助手
	void UpdateCharacterDisplayImage(const FString& SelectedCharacterName);
	
	// ==========================================
	// 弹窗按钮的点击响应函数
	// ==========================================
	UFUNCTION()
	void OnHideWeaponOverlayClicked();

	UFUNCTION()
	void OnConfirmWeaponChangeClicked();
	
	// 监听呼出弹窗的点击事件
	UFUNCTION()
	void OnChangeWeaponClicked();
	
	// 状态机变量
	int32 ActiveBackpackSlot = 1; // 当前正在操作哪个背包 (1 或 2)
	FName TempSelectedWeaponRow;  // 玩家在弹窗里点选的临时武器 (还没点确认)

	// 专门用来生成整个棋盘格的函数
	void PopulateWeaponGrid();

	// 按钮点击回调
	UFUNCTION() void OnInventory1Clicked();
	UFUNCTION() void OnInventory2Clicked();
	
	// ==========================================
	// 刷新主界面武器图标的小助手
	// ==========================================
	void UpdateWeaponDisplayImage(int32 BackpackSlot);
	
	// 专门控制高亮框位置的小助手
	void UpdateInventoryHighlightUI(int32 BackpackSlot);
	
	// ==========================================
	// AI 配置面板专属函数
	// ==========================================

	// 初始化填充 AI 面板的下拉框（角色、武器、队伍）
	void PopulateAIPanelData();

	// 呼出 AI 配置面板 (你需要在大厅里绑定一个按钮来触发它)
	UFUNCTION()
	void OnOpenAIPanelClicked();

	// 隐藏 AI 配置面板
	UFUNCTION()
	void OnHideAddAIClicked();

	// 确认添加 AI
	UFUNCTION()
	void OnConfirmAddAIClicked();
	
	// ==========================================
	// 记忆上次确认添加的 AI 角色和武器名称
	// ==========================================
	FString LastConfirmedAICharacter;
	FString LastConfirmedAIWeapon;
	// 记忆上次确认添加的队伍
	FString LastConfirmedAITeam;
	
	// ==========================================
	// 【全新架构】：UI 自动订阅与刷新引擎
	// ==========================================
	
	// 定时器句柄：用于周期性检查是否有新人加入/离开
	FTimerHandle PlayerCheckTimerHandle;

	// 记忆数组：UI 当前已经认识并订阅了的玩家状态，用于对比差异
	UPROPERTY()
	TArray<class ARoomPlayerState*> KnownPlayerStates;

	// 探头函数：每 0.5 秒运行一次，扫描 GameState 找新人
	UFUNCTION()
	void CheckForNewPlayers();

	// 核心刷新函数：只有当人员变动，或者某个玩家触发 OnStateChanged 时，才执行重绘！
	UFUNCTION()
	void RefreshRoomUI();
	
};