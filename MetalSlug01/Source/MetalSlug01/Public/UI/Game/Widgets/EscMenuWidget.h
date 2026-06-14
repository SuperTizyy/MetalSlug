// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EscMenuWidget.generated.h"

// 前向声明
class UButton;


/**
 * @class UEscMenuWidget
 * @brief 对战 ESC 菜单面板组件
 *
 * 职责说明:
 * - 游戏中按 ESC 键呼出
 * - 提供"继续游戏"和"退出游戏"两个选项
 * - 退出: 返回大厅或完全退出
 *
 * 架构理念:
 * 1. 防连点机制: bIsExitProcessing + ExitCooldownTime
 * 2. 状态机分离: Widget 只管 UI, Pause/InputMode 由 RoomPlayerController 管理
 * 3. 防御性: PlayerController 拿不到时降级 QuitGame
 * 4. 注意: 显示/隐藏由 WBP_GameHUD 通过 SetVisibility 控制
 */
UCLASS()
class METALSLUG01_API UEscMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 公开接口
	// ==========================================

	/**
	 * 恢复游戏（隐藏 ESC 菜单，由 GameHUD 调用 SetVisibility）
	 */
	UFUNCTION(BlueprintCallable, Category = "EscMenu")
	void ResumeGame();

protected:
	// ==========================================
	// 2. 生命周期
	// ==========================================

	/**
	 * 初始化: 绑定两个按钮的点击
	 */
	virtual bool Initialize() override;

	/**
	 * Widget 构造完毕并加入视口后调用
	 * 1. 初始化防连点状态
	 * 2. 默认防连点时间 1.0s
	 */
	virtual void NativeConstruct() override;

private:
	// ==========================================
	// 3. 按钮点击回调
	// ==========================================

	/**
	 * "继续游戏"按钮点击事件
	 * 通知 RoomPlayerController->HideEscMenu()
	 */
	UFUNCTION()
	void OnResumeClicked();

	/**
	 * "退出游戏"按钮点击事件
	 * 1. 防连点检查
	 * 2. 禁用按钮 + 标记正在处理
	 * 3. 调 RoomPlayerController->HideEscMenu() 同步状态
	 * 4. 调 RoomPlayerController->LeaveRoom() 返回大厅
	 */
	UFUNCTION()
	void OnExitClicked();

	// ==========================================
	// 4. 辅助函数
	// ==========================================

	/**
	 * 获取当前 PlayerController（已 Cast 为 RoomPlayerController）
	 */
	class ARoomPlayerController* GetRoomPlayerController() const;

	/**
	 * 退出游戏: 返回大厅（内部委托给 RoomPlayerController）
	 */
	void ReturnToLobby();

	/**
	 * 退出游戏: 完全退出（降级方案: PC->ConsoleCommand("quit")）
	 */
	void QuitGame();

	/**
	 * 重置按钮点击状态（供外部调用，防止防连点误判）
	 */
	UFUNCTION(BlueprintCallable, Category = "EscMenu")
	void ResetExitButtonState();

	// ==========================================
	// 5. 防连点机制
	// ==========================================

	/**
	 * 是否正在处理退出逻辑（防止多次快速点击）
	 */
	UPROPERTY()
	bool bIsExitProcessing;

	/**
	 * 防连点时间阈值（秒）
	 * 默认 1.0s
	 */
	UPROPERTY(EditDefaultsOnly, Category = "EscMenu|Settings", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float ExitCooldownTime;

	// ==========================================
	// 6. UI 组件
	// ==========================================

	/** "继续游戏"按钮 */
	UPROPERTY(meta = (BindWidget))
	UButton* Button_Resume;

	/** "退出游戏"按钮 */
	UPROPERTY(meta = (BindWidget))
	UButton* Button_Exit;
};
