// 版权声明：在项目设置的描述页面填写您的版权信息。

// 每天任务 Widget 实现文件

// ==========================================
// DailyTaskWidget 实现 — 每天任务 Widget
// ==========================================
//
// 文件作用:
//   1. 实现 UDailyTaskWidget — 极简的单天任务项 Widget
//   2. 所有状态由父级维护, 本 Widget 只声明控件
//
// 架构优势:
//   - 保持 Widget 极轻
//   - 父级 (UDailyUpgradeRewardPage) 统筹所有状态
//   - 避免子 Widget 状态飘移
// ==========================================
#include "UI/Activity/Pages/DailyUpgradeReward/DailyTaskWidget.h"

#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"


/**
 * @brief 每天任务 Widget 实现文件
 * @note 仅包含基础控件声明，无业务逻辑实现
 * @details 状态由父级 (UDailyUpgradeRewardPage) 维护:
 * - 选中高亮: SetVisibility(Image)
 * - 锁定: SetVisibility(LockIcon)
 * - 文本: SetText(DayText)
 *
 * 架构优势: 保持 Widget 极轻, 父级统筹所有状态
 */
