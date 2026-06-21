// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Login/Pages/BattleRoom/PlayerLabelWidget.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Systems/RoomPlayerController.h"


// ==========================================
// 1. 初始化
// ==========================================

/**
 * UPlayerLabelWidget::Initialize
 *
 * 1. 绑定移除按钮的点击事件
 * 2. 【2026-06-30 P0】Text_IsReady 默认 Collapsed (房主/AI/未初始化 状态都不会显示"未准备")
 *    普通玩家 widget 后续被 RefreshRoomUI 调 SetReadyState() → 显式 SetVisibility(Visible) + 文字/颜色
 *    房主/AI widget 后续被 SetAsHost()/SetAsAI() → 保持 Collapsed (拦截 SetReadyState 永远 return)
 */
bool UPlayerLabelWidget::Initialize()
{
	if (!Super::Initialize()) return false;

	// 绑定移除按钮的点击事件
	if (Btn_RemovePlayer)
	{
		Btn_RemovePlayer->OnClicked.AddDynamic(this, &UPlayerLabelWidget::OnRemoveButtonClicked);
	}

	// 【P0 防御】控件一生成, Text_IsReady 设为 Collapsed (不可见不占空间)
	// 【2026-06-30 加强】Initialize 时 Text_IsReady 一定不可见
	// 原因: Initialize 在 CreateWidget 后立刻执行 (早于 SetAsHost/SetAsAI), 此时如果显示 Visible "未准备",
	//       而后续 SetAsHost 没被调到 (例如 bIsThisLabelTheHost 在 RefreshRoomUI 那一刻误判为 false),
	//       → Text_IsReady 会停在 Visible "未准备" 这就是用户报告的"房主客户端 Text_IsReady 显示未准备"
	// 解决: Text_IsReady 默认 Collapsed, 由 SetReadyState 显式设为 Visible + 文字/颜色
	//       这样即使 SetAsHost 漏调, Text_IsReady 也只是不可见不占空间, 而不是错误地显示"未准备"
	if (Text_IsReady)
	{
		Text_IsReady->SetVisibility(ESlateVisibility::Collapsed);
	}

	return true;
}


// ==========================================
// 2. 公共接口
// ==========================================

/**
 * SetPlayerName
 *
 * 【大厂模式】同时写缓存 (数据源) + 写 UI (视图)
 * 这样即使后续 SetAsHost 改了 Text_PlayerName 显示后缀 (例如 "玩家甲（房主）"),
 * GetPlayerName() 也能返回 "玩家甲" 这个真名供踢人/逻辑使用
 *
 * 注意: 不再为 SetAsHost 单独写"防重复拼接"逻辑,
 *       因为旧实现在 "Text_PlayerName 含 '（房主）' 后缀" 时取名会得到 "玩家甲（房主）"
 *       然后踢人 RPC 送去一个带后缀的字符串 → 踢人失败!
 *       现在统一读 CachedPlayerName 杜绝此类隐患
 */
void UPlayerLabelWidget::SetPlayerName(const FString& InPlayerName)
{
	// 先写缓存 (数据源)
	CachedPlayerName = InPlayerName;

	// 再写 UI
	if (Text_PlayerName)
	{
		Text_PlayerName->SetText(FText::FromString(InPlayerName));
	}
}


/**
 * GetPlayerName
 *
 * 【大厂模式】直接读数据缓存, 不再反读 TextBlock
 * 原因: 房主身份会在 Text_PlayerName 后面追加"（房主）"后缀,
 *       反读会得到 "玩家甲（房主）", 踢人时会送这个错误字符串给服务器
 *
 * @return 当前条目的**原始玩家名**, 即第一次 SetPlayerName 时传入的字符串
 */
FString UPlayerLabelWidget::GetPlayerName() const
{
	return CachedPlayerName;
}


/**
 * SetRemoveButtonVisibility
 *
 * @param bIsVisible true=显示，false=折叠
 */
void UPlayerLabelWidget::SetRemoveButtonVisibility(bool bIsVisible)
{
	if (Btn_RemovePlayer)
	{
		// 如果可见，设为 Visible；如果不可见，设为 Collapsed (折叠，不占空间)或者 Hidden
		Btn_RemovePlayer->SetVisibility(bIsVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}


// ==========================================
// 3. 踢人事件
// ==========================================

/**
 * OnRemoveButtonClicked
 *
 * 1. 拿到当前这个标签代表的倒霉蛋的名字
 * 2. 调用 PC->Server_KickPlayer
 */
void UPlayerLabelWidget::OnRemoveButtonClicked()
{
	// 1. 拿到当前这个标签代表的倒霉蛋的名字
	FString PlayerNameToKick = GetPlayerName();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, FString::Printf(TEXT("房主下达逐客令，踢出玩家: %s"), *PlayerNameToKick));
	}

	// 2. 获取看这块屏幕的玩家自己的控制器（绝对是房主，因为只有房主能看到这按钮）
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
	{
		// 3. 呼叫对讲机，向服务器大脑开火
		PC->Server_KickPlayer(PlayerNameToKick);
	}
}


// ==========================================
// 4. 状态机
// ==========================================

/**
 * SetReadyState
 *
 * 设置准备状态
 * @param bIsReady true=绿色"已准备", false=灰色"未准备"
 *
 * 【大厂 P0 防御 - Bug1 修复关键】:
 *   房主/AI 条目不应该显示准备状态, 故本接口在两种身份下直接 return,
 *   不会触碰 Text_IsReady 的可见性 → 解决"房主客户端页面无法切换有没有准备"
 *
 * 设计理由:
 *   旧逻辑下 RefreshRoomUI 创建房主条目的顺序是:
 *     SetPlayerName → SetReadyState(快照里的 PS_bIsReady) → SetAsHost(true)
 *   SetReadyState 会把 Text_IsReady 设成 Visible "已准备/未准备",
 *   然后 SetAsHost 把它设成 Collapsed —— 后者覆盖前者, 看似没问题。
 *
 *   但 bug 实际触发场景:
 *     1. 房主客户端刚进房间, HostPlayerName 同步时延 → 房主自己的条目
 *        被暂时判定为"普通玩家", 走 RefreshRoomUI 不会调 SetAsHost
 *     2. OnRep_IsReady 在 NetworkUpdate 中触发, URoomInsidePage 重新 RefreshRoomUI,
 *        但若房主身份数据还没下发到本地 PlayerState, 房主自己的 widget
 *        会一直被当作普通玩家处理 → 看到"未准备"字样
 *   新逻辑让"房主/AI"这个标记独立于 HostPlayerName 数据流,
 *   保证 Text_IsReady 永远不会被错误显示
 */
void UPlayerLabelWidget::SetReadyState(bool bIsReady)
{
	// 【Bug1 防御】房主/AI 无准备概念, 直接拦截, 不修改 Text_IsReady
	if (bIsHostEntry || bIsAIEntry)
	{
		return;
	}

	if (Text_IsReady)
	{
		// 【P0 显式可见性】: 普通玩家需要显示 "已准备/未准备" 文本 → Visible
		// 与 SetAsHost/SetAsAI 设的 Collapsed 形成对称, 避免 Initialize 后默认 Visible 残留下误显示
		Text_IsReady->SetVisibility(ESlateVisibility::Visible);

		if (bIsReady)
		{
			Text_IsReady->SetText(FText::FromString(TEXT("已准备")));
			Text_IsReady->SetColorAndOpacity(FSlateColor(FLinearColor::Green)); // 绿色显眼
		}
		else
		{
			Text_IsReady->SetText(FText::FromString(TEXT("未准备")));
			Text_IsReady->SetColorAndOpacity(FSlateColor(FLinearColor::Gray)); // 灰色低调
		}
	}
}


/**
 * SetAsHost
 *
 * 1. 状态位 bIsHostEntry = true (Bug1 防御: 阻截后续 SetReadyState)
 * 2. 玩家名后追加"（房主）"后缀（数据源 CachedPlayerName 不动！）
 * 3. 隐藏 Text_IsReady（房主无需准备）
 * 4. 强制隐藏踢人按钮（防自踢）
 *
 * 【关键】: GetPlayerName() 返回**原始名** (CachedPlayerName),
 *           即不带"（房主）"后缀 —— 这样踢人 RPC 不会被错误指向
 */
void UPlayerLabelWidget::SetAsHost(bool bIsHost)
{
	if (bIsHost)
	{
		// 1. 写状态位 - 阻止 SetReadyState 再次显示准备文本
		bIsHostEntry = true;

		// 2. 拼接房主专属后缀 - 注意只动 UI, 不动 CachedPlayerName
		if (Text_PlayerName)
		{
			const FString BaseName = CachedPlayerName; // 从数据源读, 不从 UI 读
			const FString FinalName = FString::Printf(TEXT("%s（房主）"), *BaseName);
			Text_PlayerName->SetText(FText::FromString(FinalName));
		}

		// 3. 房主无准备概念, 永久隐藏
		if (Text_IsReady)
		{
			Text_IsReady->SetVisibility(ESlateVisibility::Collapsed);
		}

		// 4. 防自踢: 隐藏踢人按钮 (即便 SetRemoveButtonVisibility(true) 后续被调用也不开)
		if (Btn_RemovePlayer)
		{
			Btn_RemovePlayer->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}


/**
 * SetAsAI
 *
 * AI 玩家: 隐藏准备文本, 写状态位 bIsAIEntry=true
 */
void UPlayerLabelWidget::SetAsAI()
{
	// 写状态位 - 阻止 SetReadyState 再次显示准备文本
	bIsAIEntry = true;

	// AI 不需要准备状态, 直接隐藏
	if (Text_IsReady)
	{
		Text_IsReady->SetVisibility(ESlateVisibility::Collapsed);
	}
}


/**
 * ApplyIdentity
 *
 * 【大厂 P0】由 URoomInsidePage::OnRoomServiceHostChanged 调用,
 * 用于"创建时未识别为房主 → 后期 GameState 同步下来后"再补一次刷新。
 *
 * 设计对比:
 *   - 旧设计: 只用 SetAsHost/SetAsAI, 但只在 RefreshRoomUI 创建 widget 时调一次,
 *             如果创建时 HostPlayerName 还没同步, 房主 widget 就漏标了
 *   - 新设计: URoomInsidePage 监听 RoomService::OnHostChanged,
 *             一旦本地房主身份变化, 就遍历 VBox 找到"本地账号对应的 widget"
 *             调用 ApplyIdentity(true, false) 补一次刷新
 *
 * @param bIsHostEntry  是否为房主条目
 * @param bIsAIEntry   是否为 AI 条目
 */
void UPlayerLabelWidget::ApplyIdentity(bool bInIsHostEntry, bool bInIsAIEntry)
{
	// 1. 如果身份发生了"成为房主"的升级, 走 SetAsHost 的全套副作用 (加后缀+隐藏+防自踢)
	if (bInIsHostEntry && !bIsHostEntry)
	{
		SetAsHost(true);
	}
	// 2. 如果身份降级 (理论上 RoomService 不会让房主变回普通玩家, 保留入口做对称)
	else if (!bInIsHostEntry && bIsHostEntry)
	{
		bIsHostEntry = false;
		if (Text_PlayerName)
		{
			Text_PlayerName->SetText(FText::FromString(CachedPlayerName));
		}
		// 不主动把 Text_IsReady 切回 Visible - 这由 RefreshRoomUI 重新创建时用 SetReadyState 处理
	}

	// 3. AI 身份切换
	if (bInIsAIEntry && !bIsAIEntry)
	{
		SetAsAI();
	}
	else if (!bInIsAIEntry && bIsAIEntry)
	{
		bIsAIEntry = false;
		// 同理, 不主动恢复 Text_IsReady
	}
}
