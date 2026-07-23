// MetalSlug01. All Rights Reserved.
#include "Systems/Session/SessionResult.h"
#include "Logging/LogMacros.h"

// 定义日志分类:RoomMatchMode(零兜底相关)
DEFINE_LOG_CATEGORY_STATIC(LogRoomMatchMode, Log, All);

// ==========================================
// 字符串 ↔ ERoomMatchMode 转换(单一真理源)
// ==========================================

ERoomMatchMode URoomMatchModeUtils::ParseMatchModeFromGameModeString(const FString& GameModeString)
{
	// 零兜底:输入为空 → 显式失败(不允许静默默认 Melee)
	if (GameModeString.IsEmpty())
	{
		UE_LOG(LogRoomMatchMode, Error,
			TEXT("[ParseMatchModeFromGameModeString] 输入字符串为空, 拒绝默认分配. 调用方必须显式拒绝 GameState->SetCurrentMatchMode(None)."));
		return ERoomMatchMode::None;
	}

	// 中文 DisplayName 严格匹配(与 LANRoomPage ComboBox 选项字符串完全一致)
	// 注:不允许模糊匹配/Contains — 大厂原则:严格匹配才能避免歧义
	if (GameModeString.Equals(TEXT("刀战模式"), ESearchCase::CaseSensitive))
	{
		return ERoomMatchMode::Melee;
	}
	if (GameModeString.Equals(TEXT("生化模式"), ESearchCase::CaseSensitive))
	{
		return ERoomMatchMode::Zombie;
	}

	// 零兜底:未识别 → 显式失败 + 列出所有有效选项(让策划/开发者知道怎么改)
	UE_LOG(LogRoomMatchMode, Error,
		TEXT("[ParseMatchModeFromGameModeString] 未识别的 GameMode 字符串 '%s'. 有效选项: '刀战模式' / '生化模式'. 拒绝默认分配."),
		*GameModeString);
	return ERoomMatchMode::None;
}

FString URoomMatchModeUtils::GetGameModeStringFromMatchMode(ERoomMatchMode Mode)
{
	switch (Mode)
	{
	case ERoomMatchMode::Melee:
		return TEXT("刀战模式");
	case ERoomMatchMode::Zombie:
		return TEXT("生化模式");
	case ERoomMatchMode::None:
	default:
		// 零兜底:None 模式不应该被写回 SessionSettings — 显式报错
		UE_LOG(LogRoomMatchMode, Error,
			TEXT("[GetGameModeStringFromMatchMode] ERoomMatchMode::None 不允许写回 SessionSettings. 调用方应该提前拒绝创房."));
		return FString();
	}
}
