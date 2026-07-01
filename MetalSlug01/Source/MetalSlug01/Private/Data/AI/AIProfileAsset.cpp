// Copyright (c) 2026.

#include "Data/AI/AIProfileAsset.h"
#include "Data/AI/AIBehaviorConfigSO.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

UAIBehaviorConfigSO* UAIProfileAsset::LoadBehaviorConfigSync()
{
    if (BehaviorConfig.IsValid())
    {
        return BehaviorConfig.Get();
    }
    return BehaviorConfig.LoadSynchronous();
}

void UAIProfileAsset::LoadBehaviorConfigAsync(FOnAIBehaviorConfigLoaded OnLoaded)
{
    if (BehaviorConfig.IsNull())
    {
        return;
    }

    // 已加载则直接回调
    if (BehaviorConfig.IsValid())
    {
        if (OnLoaded.IsBound())
        {
            OnLoaded.Execute();
        }
        return;
    }

    // 异步加载 (cpp 内部构造一次性 FStreamableDelegate, 把 UE 强类型适配到我们的 DECLARE_DELEGATE)
    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    TWeakObjectPtr<UAIProfileAsset> WeakThis(this);
    FStreamableDelegate StreamableDone = FStreamableDelegate::CreateLambda(
        [WeakThis, OnLoaded]()
        {
            if (OnLoaded.IsBound())
            {
                OnLoaded.Execute();
            }
        });
    Streamable.RequestAsyncLoad(BehaviorConfig.ToSoftObjectPath(), StreamableDone);
}

FPrimaryAssetId UAIProfileAsset::GetPrimaryAssetId() const
{
    static const FName AIProfileTypeName(TEXT("AIProfile"));
    const FName TagName = ProfileTag.IsValid() ? ProfileTag.GetTagName() : GetFName();
    return FPrimaryAssetId(AIProfileTypeName, TagName);
}
