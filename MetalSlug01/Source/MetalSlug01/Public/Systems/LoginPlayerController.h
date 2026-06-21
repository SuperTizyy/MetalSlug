// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"

// 引入 UE 原生 APlayerController 类（基类）
#include "GameFramework/PlayerController.h"

// UE 自动生成的头文件
#include "LoginPlayerController.generated.h"

class UUserWidget;

/**
 * @class ALoginPlayerController
 * @brief 登录地图的玩家控制器 (极简版)
 *
 * 职责 (【P0 架构清理 2026.06.28】):
 * - BeginPlay 仅做 PC 硬件初始化 (鼠标 / 输入模式)
 * - 不再主动 ShowPanel / 不再订阅 OnStateChanged / 不再延迟拉起 UI
 * - UI 编排完全由 UIViewService 接管 (单一职责 + 事件驱动)
 *
 * 架构理念:
 * 1. 单一职责: 仅负责"PC 硬件配置", 不参与 UI 编排
 * 2. 解耦: PC 不直接引用任何 Widget / Subsystem / Service
 * 3. 时序安全: 不在 BeginPlay 中捕获状态 (避免被 PostLoadMapWithWorld 之后的主动切状态覆盖)
 *
 * 修复历史:
 *   - 之前: LoginPC::BeginPlay 延迟 0.2s ShowPanel → 退房后 LANRoomPage 被旧值 Login 覆盖
 *   - 现在: LoginPC 完全不参与 UI 拉起, 由 UIViewService 监听 OnStateChanged 自动 ShowPanel
 */
UCLASS()
class METALSLUG01_API ALoginPlayerController : public APlayerController
{
    GENERATED_BODY()

protected:
    /**
     * UE 原生生命周期函数: Actor 首次初始化时调用
     * 仅配置: 鼠标显示 + UIOnly 输入模式
     */
    virtual void BeginPlay() override;

    /**
     * EndPlay: 仅 Super::EndPlay (无委托需要解绑)
     */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
