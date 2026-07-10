# Phase 4 — BP 蓝图编辑器侧操作清单 (v32 拆分)

**日期**: 2026.07.12
**范围**: UE 5.6 编辑器内 BP 蓝图侧的调整（不改 .cpp）
**为什么需要**: Phase 2 拆分后, BaseCharacter.cpp 改为转发壳, 所有 RPC UFUNCTION 声明**保留在 BaseCharacter 上**（UE RPC 必须在 Actor）, 但**所有非 RPC 方法**（LightAttack_Pressed/EnableComboWindow/ExecuteDeathLocal 等）也保留在 BaseCharacter 上作为转发壳, 所以 **BP 蓝图侧 0 改动也能跑**。

## 验证方法（推荐先用这个）— BP 蓝图侧**不需要任何改动**

Phase 2 的设计目标就是 BP 蓝图/动画蓝图/AI 蓝图/BTTask 一行不改也能跑:

- 玩家 AnimBP 调用 `LightAttack_Pressed` / `EnableComboWindow` / `CheckCombo` / `EndAttackState` → BaseCharacter 转发壳 → PlayerComboComponent ✅
- AI BTTask 调用 `OnAIRequestAttack_Simple` → BaseCharacter 转发壳 → AIAttackComponent ✅
- BP 蓝图调用 `TakeDamage` / `Die` / `ExecuteDeathLocal` / `EnableRagdoll` → BaseCharacter 转发壳 → CombatDeathComponent ✅
- BP 蓝图调用 `EquipWeapon` / `RequestWeaponSpawn` / `SpawnAndEquipWeapon` → BaseCharacter 转发壳 → WeaponAttachmentComponent ✅
- BP 蓝图调用 `RefreshCharacterIcon` → BaseCharacter 转发壳 → CharacterIconComponent ✅
- RPC 边界: `Server_PlayAttackAnim` / `Multicast_PlayAttackAnim` / `Server_ReportAIAttackHit` / `Multicast_Die` / `Client_RefreshCharacterIcon` / `Multicast_NotifyKill` / `OnRep_CurrentWeapon` — **声明 + RPC 属性都在 BaseCharacter 上**, 实现委托给 Component ✅

**结论**: Phase 4 验证 = 直接 PIE 跑测试, 不用改任何 .uasset。

---

## ⚠️ 但仍然推荐做的（可选改进 — 大厂原则 - 跨边界最小化）

虽然 BP 侧不改也能跑, 但为了"职责对等"大厂原则, **建议** 把以下 4 个 AnimNotify 调用从 BaseCharacter 显式改到具体 Component:

### 调用点 1: AnimBP_Player 的 EnableComboWindow
- **当前路径**: AnimNotify → `BaseCharacter.EnableComboWindow` (转发壳) → `PlayerCombo.EnableComboWindow`
- **推荐路径**: AnimNotify → `BaseCharacter.PlayerCombo.EnableComboWindow`
- **好处**: 编辑器一眼能看出"这个 Notify 属于 PlayerCombo 子系统", 不依赖隐式转发
- **风险**: 低 (一行 AnimNotify 改写), 但若忘改 → AnimNotify 失效 (连击窗口永远不亮)

### 调用点 2: AnimBP_Player 的 CheckCombo
- 同上, 改到 `PlayerCombo.CheckCombo`

### 调用点 3: AnimBP_Player 的 EndAttackState
- 同上, 改到 `PlayerCombo.EndAttackState`

### 调用点 4: AnimBP_Player 的 HeavyAttack / UseSkill (如果有)
- 这两个调用通常不在 AnimBP 里, 而是在 Enhanced Input 触发的 InputAction, 已在 SetupPlayerInputComponent 转发到 PlayerCombo, **不需要额外操作**

---

## 操作清单（如果决定做改进版）

### 步骤 1: 打开 BP_PlayerAnimBP (或类似名) 动画蓝图
- Content Browser → AnimStarterPack 或你自己的玩家 AnimBP
- 找到 AnimNotifyState / AnimNotify 节点
- 拖入新引用: `Try Get Pawn Owner → Cast to BaseCharacter → 拖出 Component 节点 PlayerCombo → 调用目标方法`

### 步骤 2: 对 3 个 Notify 节点重复步骤 1
- EnableComboWindow
- CheckCombo
- EndAttackState

### 步骤 3: 编译 + 保存 BP
- BP 编辑器右上角 Compile → Save

### 步骤 4: PIE 验证
- 玩家点击鼠标左键 → 应进入 Combo1
- 快速点击鼠标左键 → 应衔接 Combo2
- 武器挥空 (没命中) → 1 秒后 EndAttackState 自动解锁

---

## 不需要改的文件清单

```
✅ 不需要改: 所有 .uasset 蓝图（因为转发壳兼容）
✅ 不需要改: 所有 BTTask (AI 攻击入口仍走 BaseCharacter.OnAIRequestAttack_Simple)
✅ 不需要改: 所有 DataAsset / DataTable (字段没动)
✅ 不需要改: 所有 InputAction / InputMappingContext (SetupPlayerInputComponent 已转发)
✅ 不需要改: 所有 Widget BP (HUD 调用链都在 BaseCharacter 上保留)
```

---

## 真正需要测试的（Phase 5 之前的烟雾测试清单）

按 Phase 2 拆分后的设计, PIE 中必须验证:

| # | 测试项 | 期望结果 |
|---|---|---|
| 1 | 玩家点击鼠标左键 (LightAttack) | Combo1 播放, 移动锁定, 鼠标点击后衔接 Combo2 |
| 2 | 玩家被 AI 攻击 | 血量下降, 进入无敌期 3 秒, AI 攻击在无敌期内被拦截 |
| 3 | 玩家击杀 AI | AI 死亡, 武器掉落 + 溶解, AI 3 秒后复活到出生点 |
| 4 | 玩家被 AI 击杀 | 玩家死亡, 武器掉落 + 溶解, 玩家 5 秒后复活 |
| 5 | 玩家复活 | HUD 头像刷新, 武器图标刷新, 出生 3 秒无敌 |
| 6 | AI 复活 | CachedFactionTag 写入 Pawn.FactionTag, 出生 3 秒无敌 |
| 7 | AI 看到玩家 | AIPerception 正确识别 Offense/Defense, AI 开始追击 |
| 8 | AI 走到 AR 范围内 | 自动播放攻击蒙太奇, 武器 Trace 开启 |
| 9 | AI 攻击命中 | 玩家掉血 (走 Server_ReportHit 通道, 跟玩家一致) |
| 10 | 切换阵营 | Round 切换后, Pawn.FactionTag 同步, AI 敌我识别切换 |

---

## 大厂原则对账（v32）

| 原则 | 落地情况 |
|---|---|
| **单一真理源** | 状态字段全部集中在 Component 内, BaseCharacter 不留副本 |
| **零兜底** | 转发壳调 Component 前必有 `if (Component)` 守卫, 否则 Log Error |
| **RPC 边界** | RPC UFUNCTION 声明在 Actor, 实现委托 Component (UE 限制) |
| **职责对等** | 玩家 (PlayerCombo) 与 AI (AIAttack) 完全解耦, 互不影响 |
| **大厂可观测性** | 转发壳调用前 Log Error (组件未挂载) + 组件内部 Verbose 日志保留 |
| **架构师可扩展性** | 加新战斗系统 = 加新 Component + 转发壳, 不动 BaseCharacter 主流程 |

---

**结论**: Phase 4 — BP 蓝图侧 0 改动, 直接进入 Phase 5 编译验证。