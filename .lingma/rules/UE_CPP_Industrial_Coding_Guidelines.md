---
trigger: always_on
---
# UE C++ 工业级游戏项目 AI 编程规范指南

## 总体原则
- 严格遵循 Unreal Engine C++ 编码规范
- 确保代码高内聚、低耦合
- 重视性能优化与内存管理
- 提供清晰的中文注释说明

## 1. 代码结构与模块化

### 1.1 头文件规范
```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
// 第三方插件包含
#include "ThirdPartyPlugin/Public/SomeClass.h"
// 项目内其他模块包含
#include "ModuleName/Public/OtherClass.h"
// 当前模块包含
#include "CurrentClass.generated.h"
```

### 1.2 类声明规范
```cpp
/**
 * @brief 详细描述类的功能、用途和设计意图
 * @author 开发者姓名
 * @date 创建日期
 * @note 特殊注意事项
 */
UCLASS(ClassGroup=(Custom), BlueprintType)
class PROJECTNAME_API UClassName : public UParentClass
{
    GENERATED_BODY()

public:
    // 公共接口函数声明
    
protected:
    // 受保护成员和函数声明
    
private:
    // 私有成员和函数声明
};
```

## 2. 组件与系统设计

### 2.1 组件系统规范
```cpp
/**
 * @brief 游戏组件基类 - 实现通用的游戏逻辑组件功能
 * 该组件负责管理特定类型的游戏对象行为，支持热插拔和复用
 */
UCLASS(ClassGroup=(Gameplay), BlueprintType, Blueprintable)
class PROJECTNAME_API UGameComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    /**
     * @brief 初始化组件
     * @param OwnerComponent 拥有此组件的 Actor
     * @note 在 BeginPlay 之前调用
     */
    UFUNCTION(BlueprintCallable, Category = "Game Component")
    virtual void InitializeComponent(AActor* OwnerComponent);

    /**
     * @brief 激活组件功能
     * @note 启用组件的核心逻辑循环
     */
    UFUNCTION(BlueprintCallable, Category = "Game Component")
    virtual void ActivateComponent();

protected:
    /** 组件激活状态 */
    UPROPERTY(BlueprintReadOnly, Category = "Game Component")
    bool bIsActive;

    /** 组件拥有者 */
    UPROPERTY(BlueprintReadOnly, Category = "Game Component")
    AActor* OwnerActor;
};
```

### 2.2 子系统设计规范
```cpp
/**
 * @brief 游戏管理系统 - 统筹管理游戏内的各项核心功能
 * 实现跨场景的数据持久化、状态管理和功能调度
 */
UCLASS()
class PROJECTNAME_API UGameManagementSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /**
     * @brief 注册游戏服务
     * @param ServiceName 服务唯一标识
     * @param ServiceInterface 服务实例
     * @return 是否注册成功
     * @note 用于统一管理各类游戏服务
     */
    UFUNCTION(BlueprintCallable, Category = "Game Management")
    bool RegisterService(FName ServiceName, UObject* ServiceInterface);

    /**
     * @brief 获取游戏服务
     * @param ServiceName 服务唯一标识
     * @return 服务实例引用
     */
    UFUNCTION(BlueprintPure, Category = "Game Management")
    UObject* GetService(FName ServiceName);

private:
    /** 已注册的服务映射表 */
    UPROPERTY()
    TMap<FName, TObjectPtr<UObject>> RegisteredServices;
};
```

## 3. 内存管理与性能优化

### 3.1 智能指针使用规范
```cpp
/**
 * @brief 使用弱引用避免循环依赖
 * @note WeakObjectPtr 适用于可能被销毁的对象引用
 */
UPROPERTY()
TWeakObjectPtr<UObject> CachedObject;

/**
 * @brief 使用 TObjectPtr 保证垃圾回收安全
 * @note 推荐用于常规对象引用
 */
UPROPERTY()
TObjectPtr<UObject> ManagedObject;

/**
 * @brief 使用 TSharedPtr 管理共享资源
 * @note 适用于多个对象共同拥有的资源
 */
TSharedPtr<class SWidget> SharedWidget;
```

### 3.2 容器使用规范
```cpp
/**
 * @brief 根据使用场景选择合适的容器类型
 */
// 需要随机访问且频繁遍历 - 使用 TArray
TArray<TObjectPtr<AActor>> ActorList;

// 需要快速查找 - 使用 TMap
TMap<FName, TObjectPtr<UObject>> ObjectMap;

// 需要去重 - 使用 TSet
TSet<TObjectPtr<UObject>> UniqueObjects;

// 需要有序且快速查找 - 使用 TSortedMap
TSortedMap<FName, TObjectPtr<UObject>> SortedObjectMap;
```

## 4. 蓝图与C++交互

### 4.1 函数暴露规范
```cpp
/**
 * @brief 向蓝图暴露的函数 - 提供游戏逻辑接口
 * @param InputParameter 输入参数说明
 * @return 返回值说明
 * @note 该函数可在蓝图中调用，实现脚本化逻辑
 */
UFUNCTION(BlueprintCallable, Category = "Game Logic", meta = (DisplayName = "Custom Function Name"))
bool CustomGameFunction(const FString& InputParameter);

/**
 * @brief 仅在C++中使用的内部函数
 * @note 不暴露给蓝图，提高性能
 */
bool InternalUtilityFunction();
```

### 4.2 属性暴露规范
```cpp
/**
 * @brief 蓝图可读写属性 - 允许设计师调整数值
 * @note 在编辑器中可见，支持实时调整
 */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Game Settings")
float MaxHealth;

/**
 * @brief C++专用属性 - 仅在代码中使用
 * @note 不暴露给蓝图，避免不必要的访问开销
 */
UPROPERTY()
float InternalValue;
```

## 5. 异步与多线程

### 5.1 异步任务处理
```cpp
/**
 * @brief 异步加载资源任务
 * @param ResourcePath 资源路径
 * @param Callback 加载完成回调
 * @note 避免阻塞主线程，提升用户体验
 */
DECLARE_DYNAMIC_DELEGATE_OneParam(FResourceLoadedSignature, UObject*, LoadedResource);

UFUNCTION(BlueprintCallable, Category = "Async Loading")
void AsyncLoadResource(const FString& ResourcePath, const FResourceLoadedSignature& Callback);

/**
 * @brief 在后台线程执行耗时计算
 * @param WorkFunction 计算函数
 * @note 适用于复杂算法或大量数据处理
 */
template<typename T>
void ExecuteAsyncTask(TFunction<void()> WorkFunction);
```

## 6. 错误处理与日志

### 6.1 日志系统规范
```cpp
/**
 * @brief 游戏逻辑错误日志
 * @note 记录重要的游戏逻辑异常
 */
#define GAME_LOG_ERROR(Format, ...) \
    UE_LOG(LogTemp, Error, TEXT("[GameLogic] %s: " Format), *FString(__FUNCTION__), ##__VA_ARGS__)

/**
 * @brief 性能监控日志
 * @note 记录性能相关数据，便于优化分析
 */
#define PERF_LOG(Format, ...) \
    UE_LOG(LogTemp, Display, TEXT("[Performance] %s: " Format), *FString(__FUNCTION__), ##__VA_ARGS__)
```

### 6.2 异常安全处理
```cpp
/**
 * @brief 安全的组件获取函数
 * @param Actor 目标 Actor
 * @return 组件指针或 nullptr
 * @note 包含完整的有效性检查
 */
template<class T>
T* SafeGetComponentByClass(AActor* Actor)
{
    if (!IsValid(Actor))
    {
        UE_LOG(LogTemp, Warning, TEXT("尝试从无效 Actor 获取组件"));
        return nullptr;
    }

    return Actor->FindComponentByClass<T>();
}
```

## 7. 架构模式应用

### 7.1 观察者模式实现
```cpp
/**
 * @brief 事件通知接口 - 实现松耦合的消息传递
 */
UINTERFACE(MinimalAPI)
class UGameEventListener : public UInterface
{
    GENERATED_BODY()
};

/**
 * @brief 事件监听器实现
 */
class IGameEventListener
{
    GENERATED_BODY()

public:
    /**
     * @brief 响应游戏事件
     * @param EventData 事件数据
     */
    virtual void OnGameEvent(const FGameEventData& EventData) = 0;
};
```

### 7.2 工厂模式实现
```cpp
/**
 * @brief 游戏对象工厂 - 统一管理对象创建逻辑
 * @note 降低耦合度，便于扩展和维护
 */
UCLASS(Abstract)
class PROJECTNAME_API UGameObjectFactory : public UObject
{
    GENERATED_BODY()

public:
    /**
     * @brief 创建游戏对象
     * @param World 上下文世界
     * @param ObjectClass 对象类型
     * @return 创建的对象实例
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Factory")
    UObject* CreateObject(UObject* WorldContextObject, TSubclassOf<UObject> ObjectClass);
};
```

## 8. 测试与调试

### 8.1 单元测试集成
```cpp
/**
 * @brief 自动化测试宏定义
 * @note 便于集成单元测试框架
 */
#if WITH_DEV_AUTOMATION_TESTS
#define DEFINE_GAME_TEST(TestName, TestCategory) \
    IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestName, "Game." #TestCategory "." #TestName, \
        EAutomationTestFlags::ApplicationContextMask | \
        EAutomationTestFlags::ProductFilter)
#endif
```

### 8.2 调试辅助功能
```cpp
/**
 * @brief 调试绘制函数
 * @param World 绘制世界
 * @param Location 绘制位置
 * @param Text 显示文本
 * @note 仅在开发版本中生效
 */
#if ENABLE_DRAW_DEBUG
void DrawDebugText(UWorld* World, FVector Location, const FString& Text);
#endif
```

## 9. 文档与注释标准

### 9.1 函数注释模板
```cpp
/**
 * @brief 函数功能简述
 * @param ParamName 参数详细说明
 * @return 返回值说明
 * @throws 可能抛出的异常
 * @note 重要注意事项
 * @warning 潜在风险提醒
 * @see 相关函数参考
 * @since 版本号
 * @todo 待完善功能
 */
```

### 9.2 类注释模板
```cpp
/**
 * @file ClassName.h
 * @brief 文件功能概述
 * @author 作者信息
 * @date 创建日期
 * @version 版本信息
 * @copyright 版权声明
 *
 * @defgroup GameComponent 游戏组件模块
 * @{
 * @brief 游戏组件系统功能说明
 * 详细描述组件系统的设计理念、使用方法和最佳实践
 * @}
 */
```

## 10. 性能监控与优化

### 10.1 性能计时器
```cpp
/**
 * @brief 性能分析宏
 * @note 用于测量代码段执行时间
 */
#define SCOPE_CYCLE_COUNTER(Name) \
    SCOPED_NAMED_EVENT(Name, FColor::Red); \
    double StartTime = FPlatformTime::Seconds(); \
    struct FAutoCycleCounter \
    { \
        double Start; \
        FString CounterName; \
        FAutoCycleCounter(double InStart, const FString& InName) : Start(InStart), CounterName(InName) {} \
        ~FAutoCycleCounter() \
        { \
            double EndTime = FPlatformTime::Seconds(); \
            UE_LOG(LogTemp, Display, TEXT("%s 执行时间: %f ms"), *CounterName, (EndTime - Start) * 1000.0); \
        } \
    } AutoCounter(StartTime, TEXT(#Name));
```

此规范旨在确保代码质量、可维护性和团队协作效率，所有开发人员应严格遵守。