// 版权声明：在项目设置的描述页面填写您的版权信息。

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
    /** 模块加载时调用: 此时 LogCategory 已通过 DEFINE_LOG_CATEGORY 注册 */
    virtual void StartupModule() override
    {
        UE_LOG(LogGameFlow, Log, TEXT("[MetalSlug01] 模块启动, 业务日志通道已就绪"));
    }

    /** 模块卸载时调用 */
    virtual void ShutdownModule() override
    {
        UE_LOG(LogGameFlow, Log, TEXT("[MetalSlug01] 模块关闭"));
    }
};

IMPLEMENT_PRIMARY_GAME_MODULE(FMetalSlug01Module, MetalSlug01, "MetalSlug01");
