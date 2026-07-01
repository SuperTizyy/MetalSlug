// Copyright (c) 2026.
//
// Phase 1 数据驱动层 - Blackboard Key 中央索引
// 设计:
//   - UWorldSubsystem 挂在每个 World 上
//   - 初始化时遍历 BlackBoardData 中的所有 Key, 建立枚举<->FName 映射
//   - 所有 C++ / BP 读 Key 的位置均通过 ResolveKey(EAIBlackboardKey) 拿到 FName
//   - 字符串字面量 ("ImmediateTarget" 等) 全数下沉到 AIBehaviorTypes.h 集中管理

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Systems/AI/AIBehaviorTypes.h"
#include "AIBlackboardKeyRegistrySubsystem.generated.h"

class UBlackboardData;
struct FBlackboardEntry;

/**
 * UAIBlackboardKeyRegistrySubsystem
 *
 * 解决:
 *   - BT/BTTask 不再用 FName("ImmediateTarget") 这种裸字符串字面量
 *   - 改 Key 名时, C++ 编译期会爆, BP 改名会自动断引用
 *
 * 使用方式:
 *   const UAIBlackboardKeyRegistrySubsystem* Registry = GetWorld()->GetSubsystem<UAIBlackboardKeyRegistrySubsystem>();
 *   UBlackboardComponent* BB = AIController->GetBlackboardComponent();
 *   BB->SetValueAsObject(Registry->ResolveKey(EAIBlackboardKey::ImmediateTarget), Actor);
 */
UCLASS()
class METALSLUG01_API UAIBlackboardKeyRegistrySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    /**
     * 注册 BB 资产
     * 调用时机: ABaseAIController 在 OnPossess 时调用
     * 设计: BB 注册是一个幂等操作, 重复调用会刷新内部缓存
     */
    UFUNCTION(BlueprintCallable, Category = "AI|Blackboard")
    void RegisterBlackboardData(UBlackboardData* BB);

    /** 把枚举 Key 翻译成 BB 资产内的 FName */
    UFUNCTION(BlueprintPure, Category = "AI|Blackboard")
    FName ResolveKey(EAIBlackboardKey Key) const;

    /** 检测 BB 资产内是否存在该枚举对应的 Key (未注册的 Key 会返回 false) */
    UFUNCTION(BlueprintPure, Category = "AI|Blackboard")
    bool HasKey(EAIBlackboardKey Key) const;

    /** 内部: 已注册的 Key ID 缓存 */
    const TMap<FName, uint8>& GetKeyNameToIDMap() const { return KeyNameToID; }

    /** UWorldSubsystem 接口 */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    /** Key 名 -> UBlackboardKeyType ID 的缓存 */
    TMap<FName, uint8> KeyNameToID;

    /** 当前已注册的 BB 资产 - 防止重复注册 */
    UPROPERTY()
    TObjectPtr<UBlackboardData> RegisteredBB;
};
