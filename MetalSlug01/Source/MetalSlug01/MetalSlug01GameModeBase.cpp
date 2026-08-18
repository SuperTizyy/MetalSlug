// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// MetalSlug01GameModeBase 实现 (项目最基础 GameMode)
// ==========================================
//
// 文件作用:
//   1. AMyGameModeBase 构造函数 — 配置默认 Pawn/HUD/PlayerController
//   2. 目前构造函数几乎全部注释 — 战斗地图使用更专门的 ARoomGameMode
//   3. 本类主要用于登录/菜单地图, 不承担具体游戏逻辑
//
// 关键约束 (大厂原则 — 构造函数纯净):
//   - 构造函数只能使用 ConstructorHelpers 或 StaticClass 等静态方式加载资源
//   - 不能调用 SpawnActor 等运行时函数 (此时 World 尚未完全初始化)
//   - 所有运行时逻辑应在 BeginPlay/PostLogin 等钩子里实现
// ==========================================

// ==========================================
// 头文件包含区
// ==========================================
// 引入当前 GameMode 的头文件
#include "MetalSlug01GameModeBase.h"

// 引入自定义 HUD 类，以便在构造函数中将 HUDClass 指向它
// 目的: 用我们自己写的 AMyGameHUD 替代引擎默认的 HUD，从而挂载血条/准星/聊天框等
#include "UI/MyGameHUD.h"

// 引入 UE 提供的"类查找助手" ConstructorHelpers
// 作用: 在 C++ 构造函数中按资源路径加载蓝图/资产/类对象
#include "UObject/ConstructorHelpers.h"

/**
 * AMyGameModeBase 构造函数
 *
 * @brief  在 GameMode 创建时配置默认的玩家角色蓝图和 HUD 类
 * @note   触发时机: 游戏进入关卡、GameMode 被引擎实例化时由引擎自动调用
 * @note   关键约束: 构造函数中只能使用 ConstructorHelpers 或 StaticClass 等静态方式加载资源
 *         不能调用 SpawnActor 等运行时函数,因为此时 World 尚未完全初始化
 *
 * 【当前状态】
 * - 大部分配置项 (DefaultPawnClass/HUDClass/PlayerControllerClass) 已注释掉
 * - 原因: 战斗地图使用 ARoomGameMode (更专门的子类), 本类主要服务登录/菜单地图
 * - 启用方式: 取消对应行注释, 配 Blueprint 资源路径即可激活
 */
AMyGameModeBase::AMyGameModeBase()
{
	// // ==========================================
	// // 1. 设置默认生成的角色类 (Default Pawn)
	// // ==========================================
	// // 假设你的玩家蓝图路径是 /Content/Blueprints/Characters/BP_MSPlayer.uasset
	// // 注意：路径结尾必须带有 _C 才是引用蓝图生成的类（这是 UE 的硬性约定！）
	// // 目的: 让所有使用本 GameMode 的关卡在玩家加入时自动 Spawn 这个 Pawn
	// static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Script/Engine.Blueprint'/Game/Blueprints/Characters/BP_MSPlayer.BP_MSPlayer_C'"));
	//
	// // 如果找到了这个蓝图类，就把它设置为默认玩家角色
	// // 引擎在生成玩家时会自动 Spawn 这个类
	// if (PlayerPawnBPClass.Class != nullptr)
	// {
	// 	DefaultPawnClass = PlayerPawnBPClass.Class;
	// }
	//
	// // ==========================================
	// // 2. 如果你有自定义的 PlayerController，也在这里设置
	// // ==========================================
	// // 目的: 用我们自定义的 PlayerController 替代引擎默认的，从而实现自定义输入/UI/相机控制
	// // 当前已注释，留给后续扩展
	// // PlayerControllerClass = AMSPlayerController::StaticClass();

	// ==========================================
	// 3. 绑定自定义 HUD
	// ==========================================
	// 所有进入关卡的玩家都会自动获得这个 HUD 实例
	// 目的: 用我们自己写的 AMyGameHUD 替代引擎默认的 HUD，从而挂载血条/准星/聊天框等
	//HUDClass = AMyGameHUD::StaticClass();
}
