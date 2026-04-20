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
 * @brief 房间内部 UI 页面（在黑屏的 Map_Lobby 中显示）
 * @author 开发团队
 * @date 2026-04-20
 * 
 * 负责显示攻守方、准备、开始游戏，并响应服务器的 RPC 刷新指令
 * 该页面是玩家在进入战斗前的准备区域，提供队伍选择、角色配置、武器选择等功能
 */
UCLASS()
class METALSLUG01_API URoomInsidePage : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * @brief 初始化函数，设置所有UI控件的事件绑定
	 * @return 初始化是否成功
	 * @note 在此函数中完成所有按钮点击事件、文本输入事件的绑定
	 */
	virtual bool Initialize() override;

	// ==========================================
	// 供对讲机调用的接口：往聊天框里塞入一条新消息
	// ==========================================
	/**
	 * @brief 向聊天框添加消息
	 * @param SenderName 发送者名称
	 * @param bIsHost 是否为房主
	 * @param Message 消息内容
	 * @param bIsSystemMsg 是否为系统消息
	 * @note 该函数用于在房间内显示聊天信息，区分普通玩家消息和系统消息
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomUI")
	void AddChatMessage(const FString& SenderName, bool bIsHost, const FString& Message, bool bIsSystemMsg);
	
	/**
	 * @brief 在小格子中被选中时触发的回调
	 * @param WeaponRowName 被选中的武器行名
	 * @note 当用户在武器网格中选择某个武器时调用此函数
	 */
	void OnWeaponItemSelectedInGrid(FName WeaponRowName);
	
	/**
	 * @brief 点击开始游戏按钮时的处理函数
	 * @note 触发服务器端的游戏开始请求
	 */
	UFUNCTION() void OnStartGameClicked();
	
	/**
	 * @brief 向聊天框发送系统提示消息
	 * @param Message 要显示的系统消息内容
	 * @note 用于显示系统级别的提示信息，如操作失败、状态变更等
	 */
	void AddSystemMessageToChat(const FString& Message);
	
protected:
	
	/**
	 * @brief 房间名称展示控件 (蓝图里务必命名为 Text_RoomName)
	 * @note 显示当前房间的名称和游戏模式信息
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_RoomName;
	
	/**
	 * @brief 重写 UI 构造函数，这里才是读表最安全的地方！
	 * @note 在UI构建完成后执行，用于初始化数据表和UI元素
	 */
	virtual void NativeConstruct() override;
	
	/**
	 * @brief UI销毁时的清理工作
	 * @note 清理事件监听器和定时器，防止内存泄漏
	 */
	void NativeDestruct() override;
	
	/**
	 * @brief 监听状态改变的回调函数
	 * @param NewState 新的游戏流程状态
	 * @note 当游戏状态发生变化时（如从准备状态变为战斗状态），执行相应的UI更新逻辑
	 */
	UFUNCTION()
	void OnGameFlowStateChanged(EMatchState NewState);


	// ==========================================
	// 从原先 LANRoomPage 搬过来的 UI 控件
	// ==========================================
	/**
	 * @brief 攻方列表容器
	 * @note 用于显示所有加入攻方的玩家条目
	 */
	UPROPERTY(meta = (BindWidget)) UVerticalBox* Box_AttackTeam;
	
	/**
	 * @brief 守方列表容器
	 * @note 用于显示所有加入守方的玩家条目
	 */
	UPROPERTY(meta = (BindWidget)) UVerticalBox* Box_DefenseTeam;
	
	/**
	 * @brief 加入攻方按钮
	 * @note 点击后将当前玩家分配到攻方队伍
	 */
	UPROPERTY(meta = (BindWidget)) UButton* Btn_JoinAttackTeam;
	
	/**
	 * @brief 加入守方按钮
	 * @note 点击后将当前玩家分配到守方队伍
	 */
	UPROPERTY(meta = (BindWidget)) UButton* Btn_JoinDefenseTeam;
	
	/**
	 * @brief 退出房间按钮
	 * @note 点击后离开当前房间并返回大厅
	 */
	UPROPERTY(meta = (BindWidget)) UButton* Btn_LeaveRoom;
	
	/**
	 * @brief 准备/取消准备按钮
	 * @note 切换玩家的准备状态
	 */
	UPROPERTY(meta = (BindWidget)) UButton* Btn_ToggleReady;
	
	/**
	 * @brief 准备/取消准备文字显示
	 * @note 显示当前玩家的准备状态文本
	 */
	UPROPERTY(meta = (BindWidget)) UTextBlock* Text_ReadyStatus;
	
	/**
	 * @brief 开始游戏按钮
	 * @note 仅房主可见，点击后启动游戏匹配
	 */
	UPROPERTY(meta = (BindWidget)) UButton* Btn_StartGame;

	/**
	 * @brief 动态生成玩家条目所需的蓝图类配置
	 * @note 指定用于创建玩家标签的Widget类，用于在队伍列表中显示玩家信息
	 */
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<class UPlayerLabelWidget> PlayerLabelClass;
	
	// ==========================================
	// 【新增】：房间最大允许的总人数 (真人 + AI)
	// (对应你 LANRoomPage 创房时的 SessionSettings.NumPublicConnections)
	// ==========================================
	/**
	 * @brief 房间最大允许的总人数 (真人 + AI)
	 * @note 限制房间内玩家和AI的总数，对应创建房间时的会话设置
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Room Config")
	int32 MaxNumPublicConnections = 10;

	// ==========================================
	// 聊天系统控件
	// ==========================================
	
	/**
	 * @brief 聊天消息列表 (注意：请在蓝图里使用 ScrollBox 控件)
	 * @note 显示所有聊天消息的滚动容器
	 */
	UPROPERTY(meta = (BindWidget))
	UScrollBox* ScrollBox_ChatList;

	/**
	 * @brief 聊天输入框
	 * @note 允许玩家输入并发送聊天消息
	 */
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* Input_Chat;
	
	// ==========================================
	// 【新增】：数据表配置插槽
	// 供你在蓝图里选择刚才建好的 DT_CharacterList
	// ==========================================
	/**
	 * @brief 角色数据表引用
	 * @note 存储所有可用角色的信息，包括名称、头像等数据
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Data Config")
	UDataTable* CharacterDataTable;
	
	/**
	 * @brief 玩家角色选择下拉框
	 * @note 允许玩家从可用角色列表中选择一个角色
	 */
	UPROPERTY(meta = (BindWidget))
	UComboBoxString* ComboBox_CharacterSelect;
	
	/**
	 * @brief 绑定显示角色头像的图片控件
	 * @note 显示当前选中角色的头像图片
	 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_CharacterDisplay;
	
	/**
	 * @brief 点击此按钮，呼出更换武器的弹窗
	 * @note 确保蓝图里这个按钮的名字严格叫 Btn_ChangeWeapon
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_ChangeWeapon;
	
	// ==========================================
	// 武器选择弹窗 (Overlay) 及其子控件
	// ==========================================

	/**
	 * @brief 更换武器覆盖面板 (整个弹窗的根节点，用于控制显示/隐藏)
	 * @note 包含武器选择界面的所有UI元素
	 */
	UPROPERTY(meta = (BindWidget))
	UOverlay* Overlay_WeaponSelect;

	/**
	 * @brief 物品棋盘格 (用于动态生成存放武器图标的格子)
	 * @note 以网格形式展示所有可选武器
	 */
	UPROPERTY(meta = (BindWidget))
	UUniformGridPanel* Grid_WeaponItems;

	/**
	 * @brief 物品预览图 (展示当前选中的武器大图)
	 * @note 显示玩家当前选中的武器图标
	 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_WeaponPreview;

	/**
	 * @brief 隐藏覆盖面板按钮 (取消/关闭)
	 * @note 点击后关闭武器选择弹窗
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_HideWeaponOverlay;

	/**
	 * @brief 确认更换武器按钮
	 * @note 点击后确认选择的武器并应用到当前背包槽位
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_ConfirmWeaponChange;
	
	// ==========================================
	// 军火库数据与细胞蓝图
	// ==========================================
	/**
	 * @brief 武器数据表引用
	 * @note 存储所有可用武器的信息，包括名称、图标等数据
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Data Config")
	UDataTable* WeaponDataTable;

	/**
	 * @brief 武器图标Widget类
	 * @note 用于在武器选择网格中显示单个武器图标的Widget类
	 */
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<UWeaponIconWidget> WeaponItemClass;

	// ==========================================
	// 背包切换按钮
	// ==========================================
	/**
	 * @brief 背包1切换按钮
	 * @note 点击后切换到第一个背包槽位
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_Inventory1;

	/**
	 * @brief 背包2切换按钮
	 * @note 点击后切换到第二个背包槽位
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_Inventory2;
	
	/**
	 * @brief 大厅常驻武器展示控件
	 * @note 显示当前选中背包槽位的武器图标
	 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_WeaponDisplay;
	
	// ==========================================
	// 背包高亮指示器
	// ==========================================
	
	/**
	 * @brief 背包1高亮指示器
	 * @note 显示当前是否选中了背包1
	 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_HighlightBP1;

	/**
	 * @brief 背包2高亮指示器
	 * @note 显示当前是否选中了背包2
	 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_HighlightBP2;
	
	// ==========================================
	// 5. AI 控制与设置面板 
	// ==========================================
	
	/**
	 * @brief 添加AI玩家按钮
	 * @note 点击后打开AI配置面板
	 */
	UPROPERTY(meta = (BindWidget)) 
	UButton* Btn_OpenAIPanel;
	
	/**
	 * @brief AI 设置覆盖面板 (默认隐藏)
	 * @note 包含AI配置的所有UI元素
	 */
	UPROPERTY(meta = (BindWidget)) 
	UOverlay* Overlay_AddAI;

	/**
	 * @brief AI 角色选择下拉框
	 * @note 允许选择AI使用的角色
	 */
	UPROPERTY(meta = (BindWidget)) 
	UComboBoxString* ComboBox_AICharacter;

	/**
	 * @brief AI 武器选择下拉框
	 * @note 允许选择AI使用的武器
	 */
	UPROPERTY(meta = (BindWidget)) 
	UComboBoxString* ComboBox_AIWeapon;

	/**
	 * @brief AI 队伍选择 (攻方/守方)
	 * @note 允许选择AI加入的队伍
	 */
	UPROPERTY(meta = (BindWidget)) 
	UComboBoxString* ComboBox_AITeam;

	/**
	 * @brief 添加 AI 的人数输入框
	 * @note 允许输入要添加的AI数量
	 */
	UPROPERTY(meta = (BindWidget)) 
	UEditableTextBox* Input_AICount;

	/**
	 * @brief 确认添加 AI 按钮
	 * @note 点击后根据配置添加AI玩家
	 */
	UPROPERTY(meta = (BindWidget)) 
	UButton* Btn_ConfirmAddAI;

	/**
	 * @brief 关闭 AI 面板按钮
	 * @note 点击后关闭AI配置面板
	 */
	UPROPERTY(meta = (BindWidget)) 
	UButton* Btn_HideAddAI;
	
	/**
	 * @brief 添加 AI 的提示信息框
	 * @note 显示添加AI时的提示或错误信息
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_AddAIHint;

private:
	/**
	 * @brief 退出房间按钮点击事件
	 * @note 处理玩家离开房间的请求
	 */
	UFUNCTION() void OnLeaveRoomClicked();
	
	/**
	 * @brief 加入攻方按钮点击事件
	 * @note 处理玩家加入攻方的请求
	 */
	UFUNCTION() void OnJoinAttackTeamClicked();
	
	/**
	 * @brief 加入守方按钮点击事件
	 * @note 处理玩家加入守方的请求
	 */
	UFUNCTION() void OnJoinDefenseTeamClicked();
	
	/**
	 * @brief 切换准备状态按钮点击事件
	 * @note 处理玩家准备/取消准备的请求
	 */
	UFUNCTION() void OnToggleReadyClicked();

	/**
	 * @brief 当前玩家是否已准备
	 * @note 用于跟踪玩家的准备状态
	 */
	bool bIsReady = false;
	
	/**
	 * @brief 监听玩家在输入框按下回车键发送消息
	 * @param Text 输入的文本内容
	 * @param CommitMethod 提交方式（如按回车键）
	 * @note 当玩家在聊天输入框中按下回车时触发
	 */
	UFUNCTION()
	void OnChatTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	
	/**
	 * @brief 监听下拉框切换事件的函数
	 * @param SelectedItem 选中的角色名称
	 * @param SelectionType 选择类型
	 * @note 当玩家在下拉框中选择不同角色时触发
	 */
	UFUNCTION()
	void OnCharacterSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	
	/**
	 * @brief 【新增】：专门用来刷新头像的底层小助手
	 * @param SelectedCharacterName 选中的角色名称
	 * @note 根据选中的角色名称更新显示的头像图片
	 */
	void UpdateCharacterDisplayImage(const FString& SelectedCharacterName);
	
	// ==========================================
	// 弹窗按钮的点击响应函数
	// ==========================================
	/**
	 * @brief 隐藏武器选择弹窗按钮点击事件
	 * @note 关闭武器选择界面
	 */
	UFUNCTION()
	void OnHideWeaponOverlayClicked();

	/**
	 * @brief 确认更换武器按钮点击事件
	 * @note 确认选择的武器并应用到当前背包槽位
	 */
	UFUNCTION()
	void OnConfirmWeaponChangeClicked();
	
	/**
	 * @brief 监听呼出弹窗的点击事件
	 * @note 打开武器选择界面
	 */
	UFUNCTION()
	void OnChangeWeaponClicked();
	
	/**
	 * @brief 状态机变量 - 当前正在操作哪个背包 (1 或 2)
	 * @note 跟踪当前活跃的背包槽位
	 */
	int32 ActiveBackpackSlot = 1;
	
	/**
	 * @brief 状态机变量 - 玩家在弹窗里点选的临时武器 (还没点确认)
	 * @note 存储玩家临时选择的武器，直到确认更改
	 */
	FName TempSelectedWeaponRow;

	/**
	 * @brief 专门用来生成整个棋盘格的函数
	 * @note 根据武器数据表动态生成武器选择网格
	 */
	void PopulateWeaponGrid();

	/**
	 * @brief 背包1按钮点击回调
	 * @note 切换到第一个背包槽位
	 */
	UFUNCTION() void OnInventory1Clicked();
	
	/**
	 * @brief 背包2按钮点击回调
	 * @note 切换到第二个背包槽位
	 */
	UFUNCTION() void OnInventory2Clicked();
	
	// ==========================================
	// 刷新主界面武器图标的小助手
	// ==========================================
	/**
	 * @brief 更新主界面武器显示图片
	 * @param BackpackSlot 要更新的背包槽位 (1 或 2)
	 * @note 根据指定背包槽位的武器配置更新显示的武器图标
	 */
	void UpdateWeaponDisplayImage(int32 BackpackSlot);
	
	/**
	 * @brief 专门控制高亮框位置的小助手
	 * @param BackpackSlot 要高亮的背包槽位 (1 或 2)
	 * @note 更新背包选择的高亮指示器位置
	 */
	void UpdateInventoryHighlightUI(int32 BackpackSlot);
	
	// ==========================================
	// AI 配置面板专属函数
	// ==========================================

	/**
	 * @brief 初始化填充 AI 面板的下拉框（角色、武器、队伍）
	 * @note 从数据表中读取数据并填充AI配置界面的下拉选项
	 */
	void PopulateAIPanelData();

	/**
	 * @brief 呼出 AI 配置面板 (你需要在大厅里绑定一个按钮来触发它)
	 * @note 打开AI配置界面，允许房主添加AI玩家
	 */
	UFUNCTION()
	void OnOpenAIPanelClicked();

	/**
	 * @brief 隐藏 AI 配置面板
	 * @note 关闭AI配置界面
	 */
	UFUNCTION()
	void OnHideAddAIClicked();

	/**
	 * @brief 确认添加 AI
	 * @note 根据配置向房间中添加指定数量的AI玩家
	 */
	UFUNCTION()
	void OnConfirmAddAIClicked();
	
	// ==========================================
	// 记忆上次确认添加的 AI 角色和武器名称
	// ==========================================
	/**
	 * @brief 记忆上次确认添加的 AI 角色名称
	 * @note 用于在下次打开AI配置时恢复上次的选择
	 */
	FString LastConfirmedAICharacter;
	
	/**
	 * @brief 记忆上次确认添加的 AI 武器名称
	 * @note 用于在下次打开AI配置时恢复上次的选择
	 */
	FString LastConfirmedAIWeapon;
	
	/**
	 * @brief 记忆上次确认添加的队伍
	 * @note 用于在下次打开AI配置时恢复上次的选择
	 */
	FString LastConfirmedAITeam;
	
	// ==========================================
	// 【全新架构】：UI 自动订阅与刷新引擎
	// ==========================================
	
	/**
	 * @brief 定时器句柄：用于周期性检查是否有新人加入/离开
	 * @note 每0.5秒检查一次房间内的玩家变化
	 */
	FTimerHandle PlayerCheckTimerHandle;

	/**
	 * @brief 记忆数组：UI 当前已经认识并订阅了的玩家状态，用于对比差异
	 * @note 跟踪已知的玩家状态，以便检测新加入或离开的玩家
	 */
	UPROPERTY()
	TArray<class ARoomPlayerState*> KnownPlayerStates;

	/**
	 * @brief 探头函数：每 0.5 秒运行一次，扫描 GameState 找新人
	 * @note 定期检查GameState中的玩家列表，检测玩家变化
	 */
	UFUNCTION()
	void CheckForNewPlayers();

	/**
	 * @brief 核心刷新函数：只有当人员变动，或者某个玩家触发 OnStateChanged 时，才执行重绘！
	 * @note 重新绘制房间UI，更新所有玩家的显示信息
	 */
	UFUNCTION()
	void RefreshRoomUI();
	
	/**
	 * @brief 工业级规范：将重复的网络同步逻辑封装成私有助手函数，保持代码整洁
	 * @note 同步玩家的装备配置到服务器
	 */
	void SyncLoadoutToServer();
	
	/**
	 * @brief 建立显示索引与真实 ID 的映射关系
	 * @note 缓存角色ID列表，用于快速查找和索引
	 */
	TArray<FName> CachedCharacterIDs;
	
};