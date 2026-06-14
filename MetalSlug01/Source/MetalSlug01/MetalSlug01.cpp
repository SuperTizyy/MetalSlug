// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本模块的主头文件，触发所有的项目级宏定义
#include "MetalSlug01.h"

// 引入 UE 的模块管理相关头文件
// 作用: 提供 IMPLEMENT_PRIMARY_GAME_MODULE 宏以及 IModuleInterface 接口
#include "Modules/ModuleManager.h"

// ==========================================
// 模块注册实现区
// ==========================================
// 实现主游戏模块，这是 MetalSlug01 游戏的入口点
// 宏参数详解:
//   FDefaultGameModuleImpl: 默认的游戏模块实现类（不重写模块加载/卸载逻辑）
//   MetalSlug01:            模块的 C++ 类名（编译时生成的类型标识）
//   "MetalSlug01":          模块的字符串标识符（与 .Build.cs 中的模块名保持一致）
//
// 时机: 当 UE 启动时会自动调用此宏注册的模块实现，从而触发整个项目的初始化流程
// 目的: 让 UE 引擎能够正确识别、加载、初始化本项目模块
IMPLEMENT_PRIMARY_GAME_MODULE( FDefaultGameModuleImpl, MetalSlug01, "MetalSlug01" );
