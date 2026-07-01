// Copyright (c) 2026.

#include "Systems/AI/AIBlackboardKeyRegistrySubsystem.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"

void UAIBlackboardKeyRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UAIBlackboardKeyRegistrySubsystem::Deinitialize()
{
    KeyNameToID.Reset();
    RegisteredBB = nullptr;
    Super::Deinitialize();
}

void UAIBlackboardKeyRegistrySubsystem::RegisterBlackboardData(UBlackboardData* BB)
{
    if (!BB)
    {
        return;
    }

    // 如果是同一个资产 - 不重复构建
    if (RegisteredBB == BB)
    {
        return;
    }

    KeyNameToID.Reset();
    RegisteredBB = BB;

    // 遍历 BlackboardData 内所有 Entries, 建立 Name -> KeyID 索引
    // 设计: UE 5.6 中 FBlackboardEntry 的 Key 名字段是 EntryName
    const int32 NumKeys = BB->GetNumKeys();
    for (int32 i = 0; i < NumKeys; ++i)
    {
        const FBlackboardEntry* Entry = BB->GetKey(i);
        if (Entry)
        {
            KeyNameToID.Add(Entry->EntryName, static_cast<uint8>(i));
        }
    }
}

FName UAIBlackboardKeyRegistrySubsystem::ResolveKey(EAIBlackboardKey Key) const
{
    return AIBlackboardKeyNames::Get(Key);
}

bool UAIBlackboardKeyRegistrySubsystem::HasKey(EAIBlackboardKey Key) const
{
    const FName KeyName = AIBlackboardKeyNames::Get(Key);
    return KeyNameToID.Contains(KeyName);
}
