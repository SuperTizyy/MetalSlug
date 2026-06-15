// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ComboBoxString.h"
#include "Data/Tables/CharacterTableRow.h"
#include "RoomInsidePage.generated.h"

// 前向声明所有用到的 UI 控件
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
 * @class URoomInsidePage
 * @brief 房间内部 UI 页面（在黑屏的 Map_Lobby 中显示）
 * @author 开发团队
 * @date 2026-04-20
 *
 * 职责说明:
 * - 显示攻守方、准备、开始游戏
 * - 响应服务器的 RPC 刷新指令
 * - 该页面是玩家在进入战斗前的准备区域，提供队伍选择、角色配置、武器选择等功能
 *
 * 架构理念:
 * 1. 房主/玩家区分: 根据 HasAuthority() 控制按钮可见性
 * 2. 自动订阅: 0.5 秒一次 CheckForNewPlayers 扫 GameState
 * 3. 角色/武器/AI 配置: 全部从 DataTable 加载
 * 4. 跨关卡: 监听 GameFlowSubsystem 状态变化，战斗开始自我销毁
 */
UCLASS()
class METALSLUG01_API URoomInsidePage : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * 初始化函数
	 * 时机: UI 创建时最先调用的地方
	 * 用途: 设置所有 UI 控件的事件绑定
	 */
	virtual bool Initialize() override;

	// ==========================================
	// 1. 公共接口（对外）
	// ==========================================

	/**
	 * 向聊天框添加消息
	 * @param SenderName 发送者名称
	 * @param bIsHost 是否为房主
	 * @param Message 消息内容
	 * @param bIsSystemMsg 是否为系统消息
	 * 用途: 在房间内显示聊天信息，区分普通玩家消息和系统消息
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomUI")
	void AddChatMessage(const FString& SenderName, bool bIsHost, const FString& Message, bool bIsSystemMsg);

	/**
	 * 在小格子中被选中时触发的回调
	 * @param WeaponRowName 被选中的武器行名
	 */
	void OnWeaponItemSelectedInGrid(FName WeaponRowName);

	/**
	 * 点击开始游戏按钮时的处理函数
	 * 用途: 触发服务器端的游戏开始请求
	 */
	UFUNCTION() void OnStartGameClicked();

	/**
	 * 向聊天框发送系统提示消息
	 * @param Message 要显示的系统消息内容
	 */
	void AddSystemMessageToChat(const FString& Message);

	/**
	 * 激活聊天输入框
	 * 用途: 供 PlayerController 快捷键调用
	 * 时机: 在房间等待状态下，按下聊天快捷键时呼出输入框
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomUI")
	void ActivateChatInput();

protected:
	// ==========================================
	// 2. 基础属性
	// ==========================================

	/**
	 * 房间名称展示控件
	 * 注意: 蓝图里务必命名为 Text_RoomName
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_RoomName;

	/**
	 * 重写 UI 构造函数
	 * 时机: UI 构建完成后执行
	 * 用途: 这里是读表最安全的地方! 初始化数据表和 UI 元素
	 */
	virtual void NativeConstruct() override;

	/**
	 * UI 销毁时的清理工作
	 * 用途: 清理事件监听器和定时器，防止内存泄漏
	 */
	void NativeDestruct() override;

	/**
	 * 监听状态改变的回调函数
	 * @param NewState 新的游戏流程状态
	 */
	UFUNCTION()
	void OnGameFlowStateChanged(EMatchState NewState);

	// ==========================================
	// 3. 攻守方玩家列表相关控件
	// ==========================================

	/** 攻方列表容器: 用于显示所有加入攻方的玩家条目 */
	UPROPERTY(meta = (BindWidget)) UVerticalBox* Box_AttackTeam;

	/** 守方列表容器: 用于显示所有加入守方的玩家条目 */
	UPROPERTY(meta = (BindWidget)) UVerticalBox* Box_DefenseTeam;

	/** 加入攻方按钮 */
	UPROPERTY(meta = (BindWidget)) UButton* Btn_JoinAttackTeam;

	/** 加入守方按钮 */
	UPROPERTY(meta = (BindWidget)) UButton* Btn_JoinDefenseTeam;

	/** 退出房间按钮 */
	UPROPERTY(meta = (BindWidget)) UButton* Btn_LeaveRoom;

	/** 准备/取消准备按钮 */
	UPROPERTY(meta = (BindWidget)) UButton* Btn_ToggleReady;

	/** 准备/取消准备文字显示 */
	UPROPERTY(meta = (BindWidget)) UTextBlock* Text_ReadyStatus;

	/** 开始游戏按钮（仅房主可见） */
	UPROPERTY(meta = (BindWidget)) UButton* Btn_StartGame;

	/**
	 * 动态生成玩家条目所需的蓝图类配置
	 * 用途: 指定用于创建玩家标签的 Widget 类
	 */
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<class UPlayerLabelWidget> PlayerLabelClass;

	/**
	 * 房间最大允许的总人数（真人 + AI）
	 * 默认 10，对应 LANRoomPage 创房时的 NumPublicConnections
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Room Config")
	int32 MaxNumPublicConnections = 10;

	// ==========================================
	// 4. 聊天系统控件
	// ==========================================

	/** 聊天消息列表 (注意: 请在蓝图里使用 ScrollBox 控件) */
	UPROPERTY(meta = (BindWidget))
	UScrollBox* ScrollBox_ChatList;

	/** 聊天输入框 */
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* Input_Chat;

	// ==========================================
	// 5. 角色选择相关控件
	// ==========================================

	/**
	 * 角色数据表引用
	 * 用途: 关联 DT_CharacterList，存储所有可用角色的信息
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Data Config")
	UDataTable* CharacterDataTable;

	/** 玩家角色选择下拉框 */
	UPROPERTY(meta = (BindWidget))
	UComboBoxString* ComboBox_CharacterSelect;

	/** 绑定显示角色头像的图片控件 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_CharacterDisplay;

	// ==========================================
	// 6. 武器选择相关控件
	// ==========================================

	/**
	 * 点击此按钮，呼出更换武器的弹窗
	 * 注意: 蓝图里这个按钮的名字严格叫 Btn_ChangeWeapon
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_ChangeWeapon;

	/** 更换武器覆盖面板 (整个弹窗的根节点，用于控制显示/隐藏) */
	UPROPERTY(meta = (BindWidget))
	UOverlay* Overlay_WeaponSelect;

	/** 物品棋盘格 (用于动态生成存放武器图标的格子) */
	UPROPERTY(meta = (BindWidget))
	UUniformGridPanel* Grid_WeaponItems;

	/** 物品预览图 (展示当前选中的武器大图) */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_WeaponPreview;

	/** 隐藏覆盖面板按钮 (取消/关闭) */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_HideWeaponOverlay;

	/** 确认更换武器按钮 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_ConfirmWeaponChange;

	/**
	 * 武器数据表引用
	 * 用途: 关联 DT_WeaponList
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Data Config")
	UDataTable* WeaponDataTable;

	/**
	 * 武器图标 Widget 类
	 * 用途: 用于在武器选择网格中显示单个武器图标的 Widget 类
	 */
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<UWeaponIconWidget> WeaponItemClass;

	// ==========================================
	// 7. 背包切换按钮
	// ==========================================

	/** 背包 1 切换按钮 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_Inventory1;

	/** 背包 2 切换按钮 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_Inventory2;

	/** 大厅常驻武器展示控件 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_WeaponDisplay;

	// ==========================================
	// 8. 背包高亮指示器
	// ==========================================

	/** 背包 1 高亮指示器 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_HighlightBP1;

	/** 背包 2 高亮指示器 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_HighlightBP2;

	// ==========================================
	// 9. AI 控制与设置面板
	// ==========================================

	/** 添加 AI 玩家按钮 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_OpenAIPanel;

	/** AI 设置覆盖面板 (默认隐藏) */
	UPROPERTY(meta = (BindWidget))
	UOverlay* Overlay_AddAI;

	/** AI 角色选择下拉框 */
	UPROPERTY(meta = (BindWidget))
	UComboBoxString* ComboBox_AICharacter;

	/** AI 武器选择下拉框 */
	UPROPERTY(meta = (BindWidget))
	UComboBoxString* ComboBox_AIWeapon;

	/** AI 队伍选择 (攻方/守方) */
	UPROPERTY(meta = (BindWidget))
	UComboBoxString* ComboBox_AITeam;

	/** 添加 AI 的人数输入框 */
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* Input_AICount;

	/** 确认添加 AI 按钮 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_ConfirmAddAI;

	/** 关闭 AI 面板按钮 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_HideAddAI;

	/** 添加 AI 的提示信息框 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_AddAIHint;

private:
	// ==========================================
	// 10. 按钮点击响应函数
	// ==========================================

	/** 退出房间按钮点击事件 */
	UFUNCTION() void OnLeaveRoomClicked();

	/** 加入攻方按钮点击事件 */
	UFUNCTION() void OnJoinAttackTeamClicked();

	/** 加入守方按钮点击事件 */
	UFUNCTION() void OnJoinDefenseTeamClicked();

	/** 切换准备状态按钮点击事件 */
	UFUNCTION() void OnToggleReadyClicked();

	/**
	 * 当前玩家是否已准备
	 * 用途: 跟踪玩家的准备状态
	 */
	bool bIsReady = false;

	/**
	 * 监听玩家在输入框按下回车键发送消息
	 * @param Text 输入的文本内容
	 * @param CommitMethod 提交方式（如按回车键）
	 */
	UFUNCTION()
	void OnChatTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	/**
	 * 监听下拉框切换事件的函数
	 * @param SelectedItem 选中的角色名称
	 * @param SelectionType 选择类型
	 */
	UFUNCTION()
	void OnCharacterSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	/**
	 * 专门用来刷新头像的底层小助手
	 * @param SelectedCharacterName 选中的角色名称
	 */
	void UpdateCharacterDisplayImage(const FString& SelectedCharacterName);

	// ==========================================
	// 11. 武器选择弹窗的响应函数
	// ==========================================

	/** 隐藏武器选择弹窗按钮点击事件 */
	UFUNCTION()
	void OnHideWeaponOverlayClicked();

	/** 确认更换武器按钮点击事件 */
	UFUNCTION()
	void OnConfirmWeaponChangeClicked();

	/** 监听呼出弹窗的点击事件 */
	UFUNCTION()
	void OnChangeWeaponClicked();

	/**
	 * 状态机变量: 当前正在操作哪个背包 (1 或 2)
	 */
	int32 ActiveBackpackSlot = 1;

	/**
	 * 状态机变量: 玩家在弹窗里点选的临时武器 (还没点确认)
	 */
	FName TempSelectedWeaponRow;

	/**
	 * 专门用来生成整个棋盘格的函数
	 * 用途: 根据武器数据表动态生成武器选择网格
	 */
	void PopulateWeaponGrid();

	/** 背包 1 按钮点击回调 */
	UFUNCTION() void OnInventory1Clicked();

	/** 背包 2 按钮点击回调 */
	UFUNCTION() void OnInventory2Clicked();

	// ==========================================
	// 12. 刷新主界面武器图标的小助手
	// ==========================================

	/**
	 * 更新主界面武器显示图片
	 * @param BackpackSlot 要更新的背包槽位 (1 或 2)
	 */
	void UpdateWeaponDisplayImage(int32 BackpackSlot);

	/**
	 * 专门控制高亮框位置的小助手
	 * @param BackpackSlot 要高亮的背包槽位 (1 或 2)
	 */
	void UpdateInventoryHighlightUI(int32 BackpackSlot);

	// ==========================================
	// 13. AI 配置面板专属函数
	// ==========================================

	/**
	 * 初始化填充 AI 面板的下拉框（角色、武器、队伍）
	 * 用途: 从数据表中读取数据并填充 AI 配置界面的下拉选项
	 */
	void PopulateAIPanelData();

	/**
	 * 呼出 AI 配置面板
	 * 用途: 打开 AI 配置界面，允许房主添加 AI 玩家
	 */
	UFUNCTION()
	void OnOpenAIPanelClicked();

	/**
	 * 隐藏 AI 配置面板
	 */
	UFUNCTION()
	void OnHideAddAIClicked();

	/**
	 * 确认添加 AI
	 * 用途: 根据配置向房间中添加指定数量的 AI 玩家
	 */
	UFUNCTION()
	void OnConfirmAddAIClicked();

	// ==========================================
	// 14. 记忆上次确认添加的 AI 配置
	// ==========================================

	/** 记忆上次确认添加的 AI 角色名称 */
	FString LastConfirmedAICharacter;

	/** 记忆上次确认添加的 AI 武器名称 */
	FString LastConfirmedAIWeapon;

	/** 记忆上次确认添加的队伍 */
	FString LastConfirmedAITeam;

	// ==========================================
	// 15. 【全新架构】: UI 自动订阅与刷新引擎
	// ==========================================

	/**
	 * 定时器句柄: 用于周期性检查是否有新人加入/离开
	 * 频率: 每 0.5 秒一次
	 */
	FTimerHandle PlayerCheckTimerHandle;

	/**
	 * 记忆数组: UI 当前已经认识并订阅了的玩家状态
	 * 用途: 对比差异，检测新加入或离开的玩家
	 */
	UPROPERTY()
	TArray<class ARoomPlayerState*> KnownPlayerStates;

	/**
	 * 探头函数: 每 0.5 秒运行一次，扫描 GameState 找新人
	 */
	UFUNCTION()
	void CheckForNewPlayers();

	/**
	 * 核心刷新函数
	 * 时机: 人员变动 或 玩家触发 OnStateChanged
	 * 用途: 重新绘制房间 UI，更新所有玩家的显示信息
	 */
	UFUNCTION()
	void RefreshRoomUI();

	/**
	 * 工业级规范: 将重复的网络同步逻辑封装成私有助手函数
	 * 用途: 同步玩家的装备配置到服务器
	 */
	void SyncLoadoutToServer();

	/**
	 * 建立显示索引与真实 ID 的映射关系
	 * 用途: 缓存角色 ID 列表，用于快速查找和索引
	 */
	TArray<FName> CachedCharacterIDs;
};
