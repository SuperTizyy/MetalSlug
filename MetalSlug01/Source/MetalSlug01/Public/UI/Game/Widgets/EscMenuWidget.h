#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EscMenuWidget.generated.h"

// 前向声明
class UButton;

/**
 * 对战ESC菜单面板组件
 * 在游戏中按ESC键呼出，提供"继续游戏"和"退出游戏"两个选项
 * 支持从游戏返回大厅或完全退出游戏
 * 注意：显示/隐藏由 WBP_GameHUD 通过 SetVisibility 控制
 */
UCLASS()
class METALSLUG01_API UEscMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 公开接口
	// ==========================================

	// 恢复游戏（隐藏ESC菜单，由GameHUD调用SetVisibility）
	UFUNCTION(BlueprintCallable, Category = "EscMenu")
	void ResumeGame();

protected:
	// ==========================================
	// 生命周期函数
	// ==========================================

	// 初始化函数
	virtual bool Initialize() override;

	// Widget构造完毕并加入视口后调用
	virtual void NativeConstruct() override;

private:
	// ==========================================
	// 按钮点击回调
	// ==========================================

	// "继续游戏"按钮点击事件
	UFUNCTION()
	void OnResumeClicked();

	// "退出游戏"按钮点击事件
	UFUNCTION()
	void OnExitClicked();

	// ==========================================
	// 辅助函数
	// ==========================================

	// 获取当前PlayerController
	class ARoomPlayerController* GetRoomPlayerController() const;

	// 退出游戏：返回大厅
	void ReturnToLobby();

	// 退出游戏：完全退出
	void QuitGame();

	// 重置按钮点击状态（供外部调用，防止防连点误判）
	UFUNCTION(BlueprintCallable, Category = "EscMenu")
	void ResetExitButtonState();

	// ==========================================
	// 防连点机制
	// ==========================================

	// 是否正在处理退出逻辑（防止多次快速点击）
	UPROPERTY()
	bool bIsExitProcessing;

	// 防连点时间阈值（秒）
	UPROPERTY(EditDefaultsOnly, Category = "EscMenu|Settings", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float ExitCooldownTime;

	// ==========================================
	// UI组件绑定
	// ==========================================

	// "继续游戏"按钮
	UPROPERTY(meta = (BindWidget))
	UButton* Button_Resume;

	// "退出游戏"按钮
	UPROPERTY(meta = (BindWidget))
	UButton* Button_Exit;
};
