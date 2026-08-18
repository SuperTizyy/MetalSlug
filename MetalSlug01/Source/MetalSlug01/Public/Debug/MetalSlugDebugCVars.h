// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// MetalSlug 调试 CVar 集中定义
// ==========================================
//
// 【v241.1 修复 — C2374 重定义】
//   旧 (v241): 4 个 .cpp 各写 static TAutoConsoleVariable
//   - static 只在单文件内单实例, 跨 .cpp 链接报错 "C2374: 重定义"
//   - 链接器视角: 4 个文件都有 CVarShowTraceDebug 这个符号
//
//   新 (v241.1): header-only inline CVar
//   - inline TAutoConsoleVariable<int32>: C++17 标准保证跨编译单元单实例
//   - 所有 .cpp include 同一头文件 → 编译单元都看到同一个符号 → 链接通过
//   - 控制台命令全局唯一: g.MetalSlug.ShowTraceDebug 0/1
//
// 【使用规范】
//   .cpp 顶部:
//     #include "Debug/MetalSlugDebugCVars.h"
//   任何 DrawDebug* 调用前:
//     if (CVarShowTraceDebug.GetValueOnGameThread() == 0) { /* 跳过 */ }
//
// ==========================================

#include "HAL/IConsoleManager.h"

/**
 * Trace Debug 可视化统一开关
 * 控制台命令: g.MetalSlug.ShowTraceDebug 1 打开, 0 关闭 (默认)
 * 影响范围:
 *   - BaseWeapon::Multicast_PlayFireTraceVisual (武器 trace 红/绿线)
 *   - BTDecorator_HasClearShot (AI 视线 trace)
 *   - RangedLineStrategy (远距武器 LineTrace)
 *   - MeleeSwStrategy (刀战/母体 BoxTrace)
 */
inline TAutoConsoleVariable<int32> CVarShowTraceDebug(
	TEXT("g.MetalSlug.ShowTraceDebug"),
	0,
	TEXT("Toggle trace debug visualization (DrawDebugLine/Sphere/Box).\n")
	TEXT("0 = hide all trace debug visuals (default)\n")
	TEXT("1 = show all trace debug visuals\n")
	TEXT("Affects: BaseWeapon, BTDecorator_HasClearShot, RangedLineStrategy, MeleeSwStrategy"),
	ECVF_Default
);
