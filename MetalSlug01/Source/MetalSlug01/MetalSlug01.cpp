// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// MetalSlug01 模块主入口实现
// ==========================================
//
// 文件作用:
//   1. 通过 IMPLEMENT_PRIMARY_GAME_MODULE 宏向 UE 注册主游戏模块
//   2. FMetalSlug01Module 继承 FDefaultGameModuleImpl,重写 StartupModule/ShutdownModule
//   3. 在模块加载/卸载钩子里集中处理项目级初始化 (日志通道/全局服务)
//
// 模块生命周期:
//   StartupModule: 模块 DLL 被加载时调用 — 此时可注册 LogCategory/全局服务
//   ShutdownModule: 模块 DLL 被卸载时调用 — 此时可清理全局资源
//
// 注意:
//   - 不要在 StartupModule 里访问 World/Subsystem (World 还未初始化)
//   - LogCategory 必须在任何 UE_LOG 之前注册 (DEFINE_LOG_CATEGORY 宏展开)
//   - 本模块的 LogCategory 由 Logs/MetalSlugLogChannels.h 提供
// ==========================================

// ==========================================
// 头文件包含区
// ==========================================
// 引入本模块的主头文件，触发所有的项目级宏定义
#include "MetalSlug01.h"

// 引入 UE 的模块管理相关头文件
#include "Modules/ModuleManager.h"

// 引入自定义 LogCategory 实现 (DEFINE_LOG_CATEGORY 宏在此展开)
#include "Logs/MetalSlugLogChannels.h"

// ==========================================
// 模块注册实现区
// ==========================================
// 实现主游戏模块, 继承 FDefaultGameModuleImpl 用于重写 StartupModule / ShutdownModule
// 在模块加载/卸载钩子里集中处理项目级初始化
class FMetalSlug01Module : public FDefaultGameModuleImpl
{
public:
    /**
     * StartupModule — 模块加载钩子
     *
     * @brief  UE 引擎加载 MetalSlug01 模块 DLL 时自动调用
     * @note   此时 LogCategory 已通过 DEFINE_LOG_CATEGORY 注册完成, 可安全使用 UE_LOG
     * @note   不要在此访问 World/Subsystem (尚未初始化), 仅做模块级初始化
     */
    virtual void StartupModule() override
    {
        UE_LOG(LogGameFlow, Log, TEXT("[MetalSlug01] 模块启动, 业务日志通道已就绪"));
    }

    /**
     * ShutdownModule — 模块卸载钩子
     *
     * @brief  UE 引擎卸载 MetalSlug01 模块 DLL 时自动调用 (退出游戏/PIE 结束)
     * @note   在此清理全局资源 (本项目目前仅打印日志)
     */
    virtual void ShutdownModule() override
    {
        UE_LOG(LogGameFlow, Log, TEXT("[MetalSlug01] 模块关闭"));
    }
};

IMPLEMENT_PRIMARY_GAME_MODULE(FMetalSlug01Module, MetalSlug01, "MetalSlug01");
