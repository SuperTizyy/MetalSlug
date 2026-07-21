// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ComboBoxString.h"
#include "Data/Tables/CharacterTableRow.h"
#include "Data/Enums/CombatEnums.h"  // 【v56 重构】EWeaponMeshType (武器类型过滤)
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
    // 【架构升级】View 标准接口
    // ==========================================

    /**
     * IView 接口: View 绑定后由 UIViewService 调用
     * 内部职责: 启动 0.5s 一次 UI 刷新定时器 + 立即刷新一次
     */
    UFUNCTION(BlueprintCallable, Category = "RoomInsidePage")
    void OnViewShown();

    /**
     * IView 接口: View 解绑时由 UIViewService 调用
     */
    UFUNCTION(BlueprintCallable, Category = "RoomInsidePage")
    void OnViewHidden();

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
	// 【P0 架构升级】URoomService 事件总线回调（替代 0.5s 定时器轮询）
	// 必须是 UFUNCTION, 因为是 Dynamic 委托的 BindDynamic 目标
	// ==========================================

	/** 房主身份变化: 刷新按钮可见性 */
	UFUNCTION()
	void OnRoomServiceHostChanged(bool bIsHostNow);

	/** 玩家加入: 立即刷新房间标签列表 */
	UFUNCTION()
	void OnRoomServicePlayerJoined(const FString& PlayerName);

	/** 玩家离开: 立即刷新房间标签列表 */
	UFUNCTION()
	void OnRoomServicePlayerLeft(const FString& PlayerName);

	// ==========================================
	// 3. 攻守方玩家列表相关控件
	// ==========================================

	/** 攻方列表容器: 用于显示所有加入攻方的玩家条目 */
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UVerticalBox> Box_AttackTeam;

	/** 守方列表容器: 用于显示所有加入守方的玩家条目 */
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UVerticalBox> Box_DefenseTeam;

	/** 加入攻方按钮 */
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> Btn_JoinAttackTeam;

	/** 加入守方按钮 */
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> Btn_JoinDefenseTeam;

	/** 退出房间按钮 */
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> Btn_LeaveRoom;

	/** 准备/取消准备按钮 */
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> Btn_ToggleReady;

	/** 准备/取消准备文字显示 */
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Text_ReadyStatus;

	/** 开始游戏按钮（仅房主可见） */
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> Btn_StartGame;

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
	 * 【v52 P0】点击此按钮, 呼出**主武器**更换弹窗
	 *
	 * 大厂原则 (职责对等):
	 *   - Btn_ChangePrimaryWeapon: 改主武器 (EWeaponMeshType::Primary)
	 *   - Btn_ChangeSecondaryWeapon: 改副武器 (EWeaponMeshType::Secondary)
	 *   - Btn_ChangeMeleeWeapon: 改近战武器 (EWeaponMeshType::Melee)
	 *
	 * 蓝图里 BP_WBP_RoomInsidePage 必须有同名 Button 控件
	 *
	 * 注意: 旧版 "Btn_ChangeWeapon" 字段已重命名为 "Btn_ChangeMeleeWeapon" (语义: 专门换近战武器)
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_ChangePrimaryWeapon;

	/**
	 * 【v52 P0】点击此按钮, 呼出**副武器**更换弹窗
	 * 蓝图里 BP_WBP_RoomInsidePage 必须有同名 Button 控件
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_ChangeSecondaryWeapon;

	/**
	 * 【v52 P0】点击此按钮, 呼出**近战武器**更换弹窗
	 * 蓝图里 BP_WBP_RoomInsidePage 必须有同名 Button 控件
	 *
	 * 命名说明:
	 *   - 旧 v51 字段名: Btn_ChangeWeapon (单一武器槽)
	 *   - 新 v52 字段名: Btn_ChangeMeleeWeapon (专门用于换近战武器)
	 *   - 旧字段已删除 (语义不再适用)
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_ChangeMeleeWeapon;

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
	// 7. 背包切换按钮 (保留 — 每个背包包含主+副+近战 3 把武器)
	// ==========================================

	/** 背包 1 切换按钮 — 大厂原则: 每个背包存 3 把武器 (主/副/近战), 切换背包整套 Loadout 替换 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_Inventory1;

	/** 背包 2 切换按钮 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_Inventory2;

	/**
	 * 【v52 P0 改造】大厅常驻武器展示 — 拆为 3 个 Image (主/副/近战)
	 *
	 * 旧 v51: 单个 Image_WeaponDisplay 显示"当前激活背包槽"的单把武器
	 * 新 v52: 3 个独立 Image, 每个 Image 显示对应武器类型图标
	 *
	 * 大厂原则 (职责对等):
	 *   - Image_PrimaryWeaponIcon: 主武器图标 (EWeaponMeshType::Primary)
	 *   - Image_SecondaryWeaponIcon: 副武器图标 (EWeaponMeshType::Secondary)
	 *   - Image_MeleeWeaponIcon: 近战武器图标 (EWeaponMeshType::Melee)
	 *
	 * 蓝图里 BP_WBP_RoomInsidePage 必须有同名 Image 控件 (3 个)
	 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_PrimaryWeaponIcon;

	UPROPERTY(meta = (BindWidget))
	UImage* Image_SecondaryWeaponIcon;

	UPROPERTY(meta = (BindWidget))
	UImage* Image_MeleeWeaponIcon;

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

	/** 确认更换武器按钮点击事件 — 【v52 P0】根据 ActiveWeaponType 写入对应字段 */
	UFUNCTION()
	void OnConfirmWeaponChangeClicked();

	/**
	 * 【v52 P0 拆 3 个回调】3 个独立的"换枪按钮"回调
	 *
	 * 大厂原则 (职责对等):
	 *   - OnChangePrimaryWeaponClicked: 主武器按钮 → 弹窗只列 Primary 武器
	 *   - OnChangeSecondaryWeaponClicked: 副武器按钮 → 弹窗只列 Secondary 武器
	 *   - OnChangeMeleeWeaponClicked: 近战武器按钮 → 弹窗只列 Melee 武器
	 *
	 * 旧 v51: 单个 OnChangeWeaponClicked 服务 2 个背包槽, 不区分武器类型
	 */
	UFUNCTION()
	void OnChangePrimaryWeaponClicked();

	UFUNCTION()
	void OnChangeSecondaryWeaponClicked();

	UFUNCTION()
	void OnChangeMeleeWeaponClicked();

	/**
	 * 【v52 P0】打开武器选择弹窗的私有助手 — 3 个换枪按钮共享入口
	 *
	 * 大厂原则 (DRY):
	 *   - OnChangePrimaryWeaponClicked / Secondary / Melee 都调这个助手
	 *   - 助手内部根据 WeaponType 设置 ActiveWeaponType + 加载当前选择 + 设置预览 + 弹窗
	 *   - 避免 3 个回调里复制 5 份几乎相同的代码
	 *
	 * @param WeaponType 当前要更换的武器类型 (Primary / Secondary / Melee)
	 *
	 * 流程:
	 *   1. 设 ActiveWeaponType = WeaponType
	 *   2. 读 LoadedRow: Primary 走存档, Secondary/Melee 走运行时 TMap
	 *   3. 默认回退: 找 DT_WeaponInfo 里第一个匹配 WeaponType 的 Row
	 *   4. 设 Image_WeaponPreview
	 *   5. Overlay_WeaponSelect->SetVisibility(Visible) + PopulateWeaponGrid(WeaponType)
	 */
	void OpenWeaponSelectDialog(EWeaponMeshType WeaponType);

	/**
	 * 状态机变量: 当前正在操作哪个背包 (1 或 2)
	 * 每个背包包含主+副+近战 3 把武器
	 */
	int32 ActiveBackpackSlot = 1;

	/**
	 * 【v52 P0】状态机变量: 当前弹窗正在为哪种武器类型服务
	 *
	 * 大厂原则 (零兜底):
	 *   - None = 弹窗未激活
	 *   - Primary / Secondary / Melee = 弹窗对应服务该类型
	 *   - 玩家点 "Btn_ChangePrimaryWeapon" → ActiveWeaponType = Primary
	 *   - 点确认 → 根据 ActiveWeaponType 写对应 PS.SelectedWeaponID{1,2,3}
	 */
	EWeaponMeshType ActiveWeaponType = EWeaponMeshType::Melee;

	/**
	 * 【v52 P0】状态机变量: 玩家在弹窗里点选的临时武器 (按类型分桶)
	 *
	 * 大厂原则 (数据结构对等):
	 *   - 旧 v51: FName TempSelectedWeaponRow (单变量, 不区分类型)
	 *   - 新 v52: TMap<EWeaponMeshType, FName> TempSelectedWeaponsByType (3 个类型各自的临时选择)
	 *
	 * 业务场景:
	 *   - 玩家点 Btn_ChangePrimaryWeapon → 弹 Primary 弹窗 → 点 Primary 武器 "WQ001"
	 *     → TempSelectedWeaponsByType[Primary] = "WQ001"
	 *   - 不点确认直接关弹窗 → TempSelectedWeaponsByType 暂存, 下次再开弹窗预填
	 *   - 点确认 → 写入 PS 对应字段 + 缓存到 GetLastSelectedWeapon
	 */
	TMap<EWeaponMeshType, FName> TempSelectedWeaponsByType;

	/**
	 * 【v52 P0】专门用来生成整个棋盘格的函数 — 按武器类型过滤
	 *
	 * @param FilterType EWeaponMeshType::Primary / Secondary / Melee
	 *                   None = 不过滤 (兼容旧路径)
	 */
	void PopulateWeaponGrid(EWeaponMeshType FilterType = EWeaponMeshType::None);

	/** 背包 1 按钮点击回调 */
	UFUNCTION() void OnInventory1Clicked();

	/** 背包 2 按钮点击回调 */
	UFUNCTION() void OnInventory2Clicked();

	// ==========================================
	// 12. 刷新主界面武器图标的小助手
	// ==========================================

	/**
	 * 【v52 P0】更新主界面武器显示图片 — 按武器类型更新对应 Image
	 *
	 * 旧 v51: UpdateWeaponDisplayImage(int32 BackpackSlot) — 单个 Image, 按槽位刷新
	 * 新 v52: UpdateWeaponDisplayImage(EWeaponMeshType WeaponType) — 3 个 Image, 按类型刷新
	 *
	 * 内部走 GetLastSelectedWeapon(ActiveBackpackSlot) → DT_WeaponInfo[WeaponRowName].WeaponIcon
	 */
	void UpdateWeaponDisplayImage(EWeaponMeshType WeaponType);

	/**
	 * 【v52 P0】刷新所有 3 个 Image 控件 (主+副+近战)
	 * 用途: 切换背包 / 初始化时一次刷完 3 个图标
	 */
	void RefreshAllWeaponDisplayImages();

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
	 * 【2026.07.11 v29 大厂架构重构】内部辅助: 在指定 Box 里创建一个 UPlayerLabelWidget
	 *
	 * 设计动机:
	 *   旧 (v28) 错误做法: RefreshRoomUI 内联 70 行 widget 创建/属性设置逻辑, 真人 + AI 各一份
	 *     → 重复代码, 改一处忘另一处, 易出 bug
	 *   新 (v29): 抽出 CreatePlayerLabelInBox, 真人 + AI 共用, 单点真理
	 *
	 * 大厂原则:
	 *   - 单一入口: widget 创建 + 属性设置逻辑只在此一处
	 *   - 显式参数: bIsAI 显式传 (不再依赖 PName.StartsWith("[AI]") 字符串约定)
	 *   - 零兜底: TargetBox 为空或 PlayerLabel 创建失败 → Log Error + 不渲染 (显式问题)
	 *
	 * @param TargetBox 目标阵营容器 (Box_AttackTeam 或 Box_DefenseTeam)
	 * @param PName 玩家/AI 名字
	 * @param bIsAI 是否 AI 占位
	 * @param CurrentHostName 服务器权威房主名
	 * @param bAmILocalHost 本机是否房主 (从 NetMode 权威判断得来)
	 * @param LocalAccountName 本机账号名 (兜底本地房主判定)
	 * @param bLabelReady 该 label 准备状态 (AI 永远 false) — 与类成员 bIsReady 区分
	 */
	void CreatePlayerLabelInBox(
		UVerticalBox* TargetBox,
		const FString& PName,
		bool bIsAI,
		const FString& CurrentHostName,
		bool bAmILocalHost,
		const FString& LocalAccountName,
		bool bLabelReady);

	/**
	 * 【2026-06-29 P0 修复】统一刷新房主/玩家专属按钮的可见性
	 * 职责: 根据当前玩家房主身份, 设置 Btn_OpenAIPanel / Btn_StartGame / Btn_ToggleReady 可见性
	 * 触发点:
	 *   - NativeConstruct (widget 首次创建时)
	 *   - OnViewShown (View 挂载时, 修复玩家B加入看不到按钮的 bug)
	 *   - OnRoomServiceHostChanged (房主身份变化时)
	 *   - RefreshRoomUI 末尾 (房主身份可能在订阅期间变化)
	 */
	void UpdateHostVisibility();

	/**
	 * 【Bug1 P0 修复】兜底函数: 遍历本机玩家 VBox, 把对应本地账号的 PlayerLabel 强制标为房主
	 * 场景: 服务器 ON_REP 时延导致 RefreshRoomUI 创建 widget 时漏判房主身份,
	 *       → 监听 OnRoomServiceHostChanged(true) 后调用本函数补救
	 */
	void ForceApplyHostIdentityToLocalWidget();

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
