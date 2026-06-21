// ==========================================
// 大厂标准：游戏实例实现
// ==========================================

// 引入本类头文件
#include "MetalSlugGameInstance.h"

// 引入 GameFlow 子系统 (用于触发 BootToLogin)
#include "Systems/GameFlowSubsystem.h"

// 引入统一日志通道
#include "Logs/MetalSlugLogChannels.h"

// 引入文件落盘工具（绕过 PIE 子进程早期 stdout 丢失问题）
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
// 【大厂架构 - 从 WorldContext 获取 PIEInstance 2026.06.30】
#include "Engine/Engine.h"

// ==========================================
// 生命周期
// ==========================================

void UMetalSlugGameInstance::Init()
{
	// ==========================================
	// 第一阶段: 让引擎完成所有 Subsystem 的自然初始化
	// ==========================================
	// 在调用 Super::Init() 之前，所有 GameInstanceSubsystem 都未初始化
	// Super::Init() 会触发它们按 Engine 内部顺序执行 Initialize()
	// 包括:
	//   - GameFlowSubsystem::Initialize()  (检查 DataTable)
	//   - UIViewService::Initialize()      (订阅 OnStateChanged + 预创建)
	//   - AccountSubsystem::Initialize()   (账号数据加载)
	//   - SessionManagerSubsystem::Initialize()
	//   - LANRoomPresenter::Initialize()
	Super::Init();

	UE_LOG(LogGameFlow, Log, TEXT("[GameInstance] 第一阶段完成：所有 Subsystem 已 Initialize"));

	// 【大厂架构 - 文件落盘探针 2026.06.30】绕过 PIE 早期 stdout 丢失问题
	{
		const FString LogFilePath = FPaths::ProjectSavedDir() / TEXT("AutoTestRoom.log");
		const FString Line = FString::Printf(
			TEXT("[%s][PID=%u] [GameInstance] Init() 第一阶段完成, CachedPIEInstanceIndex=%d\n"),
			*FDateTime::Now().ToString(),
			FPlatformProcess::GetCurrentProcessId(),
			CachedPIEInstanceIndex);
		FFileHelper::SaveStringToFile(
			Line,
			*LogFilePath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(),
			FILEWRITE_Append);
	}

	// ==========================================
	// 第二阶段: 编排启动序列 (大厂核心模式)
	// ==========================================
	// 此时:
	// - 所有 Subsystem 已就绪
	// - UIViewService 已订阅 GameFlow::OnStateChanged
	// - UIViewService 已同步初始状态 (但拿到的是 PreLogin, 不显示任何面板)
	//
	// 我们主动触发 PreLogin → Login 转换:
	// - GameFlowSubsystem::BootToLogin() 内部调用 TransitToState(Login)
	// - TransitToState 广播 OnStateChanged(Login)
	// - UIViewService 收到事件 → 在 StateToPanelMap 中查找 Login 对应面板
	// - 调用 ShowPanel(Login) → 显示 LoginPage 蓝图
	//
	// 这就是大厂 Single Source of Truth: 一个事件驱动所有 UI

	if (UGameFlowSubsystem* FlowSubsystem = GetSubsystem<UGameFlowSubsystem>())
	{
		UE_LOG(LogGameFlow, Log, TEXT("[GameInstance] 第二阶段：触发 BootToLogin (PreLogin → Login)"));
		FlowSubsystem->BootToLogin();
	}
	else
	{
		UE_LOG(LogGameFlow, Error, TEXT("[GameInstance] 未找到 GameFlowSubsystem！无法启动游戏流程"));
	}

	UE_LOG(LogGameFlow, Log, TEXT("[GameInstance] 启动编排完成"));
}

void UMetalSlugGameInstance::Shutdown()
{
	// 关闭期: 先调用 Super (Subsystem 自然 Deinitialize)
	Super::Shutdown();

	// 自定义清理逻辑可加在这里
	UE_LOG(LogGameFlow, Log, TEXT("[GameInstance] 已关闭"));
}

// ==========================================
// PIE 多客户端识别 (大厂架构 - 跨模式 2026.06.30)
// ==========================================

FGameInstancePIEResult UMetalSlugGameInstance::InitializeForPlayInEditor(
	int32 PIEInstanceIndex,
	const FGameInstancePIEParameters& Params)
{
	// ==========================================
	// 【P0】缓存 PIE Instance Index 给 Subsystem 查询
	// ==========================================
	// Editor 进程: PIEInstanceIndex 直接由编辑器传入
	// 子进程: 本函数可能不被调用（见 StartPlayInEditorGameInstance 的处理）
	// ==========================================
	CachedPIEInstanceIndex = PIEInstanceIndex;

	UE_LOG(LogGameFlow, Log,
		TEXT("[GameInstance] InitializeForPlayInEditor: PIEInstanceIndex=%d"),
		PIEInstanceIndex);

	// 【大厂架构 - 文件落盘探针 2026.06.30】
	{
		const FString LogFilePath = FPaths::ProjectSavedDir() / TEXT("AutoTestRoom.log");
		const FString Line = FString::Printf(
			TEXT("[%s][PID=%u] [GameInstance] InitializeForPlayInEditor: PIEInstanceIndex=%d (via Params)\n"),
			*FDateTime::Now().ToString(),
			FPlatformProcess::GetCurrentProcessId(),
			PIEInstanceIndex);
		FFileHelper::SaveStringToFile(
			Line,
			*LogFilePath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(),
			FILEWRITE_Append);
	}

	// 调用父类完成标准 PIE 初始化 (World 复制 + LocalPlayer 创建 + Init)
	return Super::InitializeForPlayInEditor(PIEInstanceIndex, Params);
}

FGameInstancePIEResult UMetalSlugGameInstance::StartPlayInEditorGameInstance(
	ULocalPlayer* LocalPlayer,
	const FGameInstancePIEParameters& Params)
{
	// ==========================================
	// 【大厂架构 - 跨进程 PIE PIEInstanceIndex 获取 2026.06.30】
	// ==========================================
	// InitializeForPlayInEditor 在跨进程 PIE 子进程中不被调用
	// (父类实现内有 CastChecked<UEditorEngine>(GetEngine())，子进程返回 UGameEngine 导致崩溃)
	// 因此改在 StartPlayInEditorGameInstance 中从 WorldContext 读取 PIEInstance
	//
	// 原理:
	//   - UGameInstance::Init() 末尾调用 StartPlayInEditorGameInstance
	//   - 此时 WorldContext 已通过 InitializeForPlayInEditor (Editor 进程)
	//     或通过子进程的 UGameEngine 内部逻辑设置好
	//   - 无论 Editor 还是子进程，GEngine->GetWorldContextFromWorld(GetWorld())
	//     都能返回正确的 WorldContext，其中 PIEInstance 字段保存了实例编号
	// ==========================================

	// 【大厂架构 - 文件落盘探针 2026.06.30】
	{
		const FString LogFilePath = FPaths::ProjectSavedDir() / TEXT("AutoTestRoom.log");
		FString Line = FString::Printf(
			TEXT("[%s][PID=%u] [GameInstance] StartPlayInEditorGameInstance 入口\n"),
			*FDateTime::Now().ToString(),
			FPlatformProcess::GetCurrentProcessId());

		// 尝试从 WorldContext 读取 PIEInstance
		if (UWorld* World = GetWorld())
		{
			if (const FWorldContext* WC = GEngine->GetWorldContextFromWorld(World))
			{
				CachedPIEInstanceIndex = WC->PIEInstance;
				Line += FString::Printf(
					TEXT("[%s][PID=%u]   → WorldContext->PIEInstance=%d, WorldName=%s\n"),
					*FDateTime::Now().ToString(),
					FPlatformProcess::GetCurrentProcessId(),
					WC->PIEInstance,
					*World->GetName());
			}
			else
			{
				Line += FString::Printf(
					TEXT("[%s][PID=%u]   → 无法获取 WorldContext (World=%s)\n"),
					*FDateTime::Now().ToString(),
					FPlatformProcess::GetCurrentProcessId(),
					*World->GetName());
				CachedPIEInstanceIndex = INDEX_NONE;
			}
		}
		else
		{
			Line += FString::Printf(
				TEXT("[%s][PID=%u]   → GetWorld() 返回 nullptr, 无法获取 PIEInstance\n"),
				*FDateTime::Now().ToString(),
				FPlatformProcess::GetCurrentProcessId());
			CachedPIEInstanceIndex = INDEX_NONE;
		}

		FFileHelper::SaveStringToFile(
			Line,
			*LogFilePath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(),
			FILEWRITE_Append);
	}

	UE_LOG(LogGameFlow, Log,
		TEXT("[GameInstance] StartPlayInEditorGameInstance: CachedPIEInstanceIndex=%d"),
		CachedPIEInstanceIndex);

	// 调用父类完成标准 PIE 初始化
	return Super::StartPlayInEditorGameInstance(LocalPlayer, Params);
}