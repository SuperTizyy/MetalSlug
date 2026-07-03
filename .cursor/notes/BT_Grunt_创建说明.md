# BT_Grunt 行为树结构 (v22 - 2026.07.09)

> **架构定位**: AI 100% 由 BT 决策, C++ 仅提供原子能力
> **核心原则**: BT 负责"何时做", C++ 负责"怎么做"

---

## 0. 架构原则

### 分工边界

| 层级 | 职责 | 实现方式 |
|------|------|----------|
| **BTService** | 派生世界**原始事实** | 周期写入 BB (0.1s) |
| **BTDecorator** | 读事实 + 做**决策** | 条件判断 (原生/C++) |
| **BTTask** | **原子能力** | 不可拆分的动作 |

### 决策节点 vs 原子能力

**决策 = Decorator**：判断"当前状态是否满足某个条件"，是 BT 的职责：
- `BTDecorator_TooClose` — D ≤ AR → 撤退
- `BTDecorator_InAttackRange` — AR-M ≤ D ≤ AR+M → 攻击
- `BTDecorator_ShouldChase` — D > AR+M → 追击
- `BTDecorator_CooldownReady` — 冷却是否结束
- `BTDecorator_HPThreshold` — 血量是否告急

**原子 = Task**：执行"一个不可分割的动作"，是 C++ 的职责：
- `BTTask_FaceTarget` — 面向目标
- `BTTask_MoveAwayFromTarget` — 远离目标
- `BTTask_PlayAttackMontage` — 播放攻击动画
- `BTTask_WaitMontageFinish` — 等待动画结束
- `BTTask_PlayDeath` — 死亡

### 重要说明

UE 原生 `Compare BB Entries` 装饰器只有 2 个操作符：`Is Equal To` / `Is Not Equal To`。
**没有** `Less Than` / `Greater Than` / `Less Than Or Equal` 等比较操作。

因此，所有距离区间判断（≤ ≥ < >）由 C++ 自定义 Decorator 实现。

---

## 1. 完整树形结构

```
ROOT (BB_AI_Melee)
│
└── Selector (状态选择器, 优先级: Death > Retreat > Attack > Chase)
      │
      ├── [Service] BTService_UpdateDistance (Interval=0.1s)
      │     TargetKey      → "TargetActor"
      │     DistanceKey    → "DistanceToTarget"
      │     HasTargetKey   → "bHasTarget"       (可选)
      │     AttackRangeKey → "AttackRange"
      │
      ├── [Service] BTService_UpdateHealth (Interval=0.1s)
      │     HealthPercentKey → "HealthPercent"
      │
      ├── [Service] BTService_RefreshTarget (Interval=0.3s)
      │     TargetKey → "TargetActor"
      │     ScanRadius → 3000
      │
      ├── Sequence "Death"  ← 最高优先级
      │     ├── [Decorator] BTDecorator_HPThreshold
      │     │      HealthPercentKey → "HealthPercent"
      │     │      Mode → LessThan
      │     │      Threshold → 0.01
      │     │      FlowAbortMode → Self
      │     └── [Task] BTTask_PlayDeath
      │
      ├── Sequence "Retreat"
      │     ├── [Decorator] BTDecorator_TooClose
      │     │      FlowAbortMode → Self
      │     ├── UE 原生 Rotate to face BB entry
      │     │      Blackboard Key → "TargetActor"
      │     │      Precision → 5.0
      │     └── [Task] BTTask_MoveAwayFromTarget
      │            RetreatDistance → 100 (cm)
      │
      ├── Sequence "Attack"
      │     ├── [Decorator] BTDecorator_CooldownReady
      │     │      CooldownEndTimeKey → "CooldownEndTime"
      │     │      FlowAbortMode → Self
      │     ├── [Decorator] BTDecorator_InAttackRange
      │     │      HysteresisMargin → 10 (cm)
      │     │      FlowAbortMode → Self
      │     ├── UE 原生 Rotate to face BB entry
      │     │      Blackboard Key → "TargetActor"
      │     │      Precision → 5.0
      │     ├── [Task] BTTask_PlayAttackMontage
      │     └── [Task] BTTask_WaitMontageFinish
      │            TimeoutSeconds → 10
      │
      └── Sequence "Chase"
            ├── [Decorator] BTDecorator_ShouldChase
            │      HysteresisMargin → 10 (cm, 必须与 InAttackRange 一致)
            │      FlowAbortMode → Self
            ├── UE 原生 Rotate to face BB entry
            │      Blackboard Key → "TargetActor"
            │      Precision → 5.0
            └── UE 原生 Move To
                   Blackboard Key → "TargetActor"
                   AcceptableRadius → 50
                   bAllowStrafe → false
```

---

## 2. BB Key 完整清单

| Key 名 | 类型 | 默认值 | 写者 | 读者 |
|--------|------|--------|------|------|
| `TargetActor` | Object | None | BTService_RefreshTarget | 几乎所有 |
| `DistanceToTarget` | Float | -1.0 | BTService_UpdateDistance | 决策 Decorator |
| `bHasTarget` | Bool | false | BTService_UpdateDistance | (可选) |
| `AttackRange` | Float | 180.0 | BTService_UpdateDistance | 决策 Decorator |
| `HealthPercent` | Float | 1.0 | BTService_UpdateHealth | HPThreshold Decorator |
| `CooldownEndTime` | Float | 0.0 | BTTask_PlayAttackMontage | CooldownReady Decorator |

---

## 3. 决策 Decorator 详解

**核心原则**: 用 `FBlackboardKeySelector` 而不是硬编码字符串。BB Key 名由 **BT 编辑器手动绑定** 到 BB 资产对应的 Key (BB_AI_Melee.uasset), 改 Key 名不需要改 C++。

### BTDecorator_TooClose

```
Details 面板绑定:
  DistanceKey    → "DistanceToTarget"
  AttackRangeKey → "AttackRange"

v23.1: 触发条件: DistanceToTarget < AttackRange (严格小于)
       含义:     AI 离目标太近, 需要撤退
       决策结果: true  → 进入 Retreat Sequence
                 false → 不进入 (可能被 Attack/Chase 捕获)
FlowAbort: Self (AI 在 Attack/Chase 时贴近, 自我中断去 Retreat)

  - v22 用 <=, 边界值 D=180 同时触发 TooClose+InAttackRange → BT 选择不确定
  - v23.1 用 <, D=180 不触发 TooClose, 进 Attack; D=179 才进 Retreat → 清晰划分
```

### BTDecorator_InAttackRange

```
Details 面板绑定:
  DistanceKey    → "DistanceToTarget"
  AttackRangeKey → "AttackRange"
  HysteresisMargin → 10 (cm, BT 资源层硬编码, 必须与 ShouldChase 一致)

触发条件: (AttackRange - HysteresisMargin) <= DistanceToTarget <= (AttackRange + HysteresisMargin)
默认值:   AR=180, Margin=10 → 区间 [170, 190]
含义:     AI 在攻击距离内, 可以攻击
决策结果: true  → 进入 Attack Sequence
          false → 不进入 (可能被 Retreat/Chase 捕获)
FlowAbort: Self (AI 离开攻击区间时自我中断)
```

### BTDecorator_ShouldChase

```
Details 面板绑定:
  DistanceKey    → "DistanceToTarget"
  AttackRangeKey → "AttackRange"
  HysteresisMargin → 10 (cm, BT 资源层硬编码, 必须与 InAttackRange 一致)

触发条件: DistanceToTarget > (AttackRange + HysteresisMargin)
默认值:   AR=180, Margin=10 → 阈值 190
含义:     AI 离目标太远, 需要追击
决策结果: true  → 进入 Chase Sequence
          false → 不进入 (可能被 Attack/Retreat 捕获)
FlowAbort: Self (AI 进入攻击区间时自我中断)
```

### 区间无缝隙分析

```
0         170        180        190              cm
|----------|----------|----------|------------------|
                              InAttackRange 上界
                    AR=180 (派生自 BB.AttackRange)
           InAttackRange 下界
Retreat:  D <  180  (TooClose 严格小于, v23.1)
Attack:   170 <= D <= 190  (InAttackRange)
Chase:    D >  190  (ShouldChase)

区间划分 (v23.1 后):
  D = 179: < 180 → 进 Retreat (退 5cm 后 D' = 184 → 落 Attack)
  D = 180: 不 < 180 → 进 Attack (因为 AR-10 ≤ 180 ≤ AR+10)
  D = 181: 进 Attack (因为 AR-10 ≤ 181 ≤ AR+10)
```

---

## 4. C++ Task 原子能力详解

**核心原则**: 只保留 UE 原生做不到的原子能力。已经能用 UE 原生节点的不要自己造轮子。

### BTTask_MoveAwayFromTarget

```
Details 面板绑定:
  TargetKey        → "TargetActor"
  StepDistance     → 5 (cm)  ← v23 默认 5cm, v22 旧值 100cm 是错的
  AcceptanceRadius → 5 (cm)  ← 移动到达容差
  MaxWaitTime      → 2 (s)   ← 被阻挡最大等待

职责: 让 AI 沿远离目标方向退一步 (面朝敌人后退, v23.2 修复回头走)
算法:
  1. 保存 Pawn 原 Movement 设置
  2. 临时:
     - OrientRotationToMovement = false
     - UseControllerDesiredRotation = true
  3. AIC->SetFocus(TargetActor, EAIFocusPriority::Gameplay)
  4. Dir = (AI位置 - 目标位置).GetSafeNormal()
  5. StepBackLoc = AI位置 + Dir × StepDistance (默认 5cm)
  6. NavMesh 投影
  7. MoveTo StepBackLoc
  8. 任务完成/Abort/超时 → 恢复原 Movement 设置 + ClearFocus

v23.2 修复回头走根因:
  - BP_MeleeGrunt 的 CharacterMovement 默认 OrientRotationToMovement = ✔
  - OrientRotationToMovement = ✔ 时, MoveTo 会强制 AI 朝向 = 移动方向
  - 退步方向是 AI 后方 → AI 背对玩家走 = 看起来 "回头走"

  v23.2 解法 (大厂标准):
  - 退步期间临时关 OrientRotationToMovement (MoveTo 不再抢朝向)
  - 开 UseControllerDesiredRotation (让 Controller 控制朝向)
  - AIC->SetFocus(TargetActor, EAIFocusPriority::Gameplay)
    (Gameplay 优先级最高, 压过 MoveTo 默认 MoveFocus)
  - 结果: AI 朝向固定朝 Target, MoveTo 走后退方向

v23 默认 5cm 不是 100cm 的原因 (回顾):
  - v22 RetreatDistance=100cm 是错的
  - v23 退 5cm: 退完 D' = 185 → 落 Attack 区间 → 立刻打玩家 → 不抖动

返回: InProgress (异步, 等 MoveTo 完成) → Succeeded
超时: 2s (被阻挡时强制 Succeeded)
前置: BTDecorator_TooClose 已保证 D < AR 才进
```

**重要约束**: 
- 此 Task 启动时会**临时修改 Pawn 的 Movement 设置**
- 任务完成/Abort 时**自动恢复原值**, 不污染其他分支
- 但建议: BP_MeleeGrunt 默认 `Use Controller Desired Rotation = ✔` (这样 v23.2 不需要再开)

### BTTask_PlayAttackMontage

```
职责: 触发 AI 攻击
实现: AIChar->OnAIRequestAttack_Simple()
返回: Succeeded (同步, 动画等待由 WaitMontageFinish 接管)
副作用: 写 BB.CooldownEndTime = Now + AttackCooldown
前置: Decorator 已保证在攻击区间且冷却结束
```

### BTTask_WaitMontageFinish

```
职责: 等待攻击动画播放完毕
实现: Tick 检查 IsCurrentlyAttacking()
返回: InProgress → Succeeded (动画结束)
超时: 10s (防卡死)
Abort: 清 CurrentlyAttacking 状态
```

### BTTask_PlayDeath

```
职责: 死亡 (哨兵节点)
实现: 实际死亡由 HealthComponent->Die 自动触发
返回: Succeeded
前置: Decorator_HPThreshold 已保证 HP < 0.01
```

---

## 5. 需要在 UE 编辑器做的事

### BB_AI_Melee.uasset (重要)

Decorator 使用 `FBlackboardKeySelector`, Key 名必须在 BB 资产里手动存在:

| Key 名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `TargetActor` | Object | None | 已是历史 Key, 不变 |
| `DistanceToTarget` | Float | -1.0 | **新建** — BTService_UpdateDistance 派生写入 |
| `AttackRange` | Float | 180.0 | **新建** — BTService_UpdateDistance 派生自 ConfigSO |
| `bHasTarget` | Bool | false | **新建** (可选) — BTService_UpdateDistance 派生 |
| `HealthPercent` | Float | 1.0 | 已是历史 Key, 不变 |
| `CooldownEndTime` | Float | 0.0 | 已是历史 Key, 不变 |

### BB Key → ConfigSO 关联链

策划改 ConfigSO 数据 → BTService 派生写入 BB → Decorator 自动跟随:

```
UAIBehaviorConfigSO.AttackRange = 300
      │
      ▼
UAIRuntimeConfigComponent.GetScaledCombat().AttackRange = 300
      │
      ▼
BTService_UpdateDistance.Tick (0.1s)
      │
      ▼
BB.AttackRange = 300
      │
      ▼
BTDecorator_InAttackRange → (300-10) <= D <= (300+10) 即 [290, 310]
BTDecorator_TooClose     → D <= 300
BTDecorator_ShouldChase  → D > 310
```

**1 处改配置 → 3 个 Decorator 自动跟随, BT 资源层 0 改动**。这就是大厂 ConfigSO + FBlackboardKeySelector 标准做法。

### BT_MeleeAI.uasset

1. 根 Selector 挂 3 个 Service (UpdateDistance / UpdateHealth / RefreshTarget)
2. 建 4 个 Sequence（按第 1 节树形结构）
3. 各 Decorator 在 Details 面板**手动绑定 BB Key** (按第 3 节)
4. 各 FlowAbortMode 全部设 Self
5. `InAttackRange` 和 `ShouldChase` 的 HysteresisMargin **必须一致**（默认都是 10）

---

## 6. 新增 C++ 文件清单 (v22)

```
Public/Systems/AI/Decorators/
├── BTDecorator_TooClose.h/cpp      ← 新增 (D ≤ AR → 撤退)
├── BTDecorator_InAttackRange.h/cpp ← 新增 (AR-M ≤ D ≤ AR+M → 攻击)
└── BTDecorator_ShouldChase.h/cpp   ← 新增 (D > AR+M → 追击)

Public/Systems/AI/Tasks/
└── BTTask_MoveAwayFromTarget.h/cpp ← 新增 (远离目标, UE 原生无此能力)

Private/Systems/AI/Tasks/
├── BTTask_MoveAwayFromTarget.cpp
├── BTTask_PlayAttackMontage.cpp    ← 新增 (业务: OnAIRequestAttack_Simple)
├── BTTask_WaitMontageFinish.cpp    ← 新增 (业务: IsCurrentlyAttacking)
└── BTTask_PlayDeath.cpp            ← 新增 (死亡 + Destroy)

修改的文件:
├── BTService_UpdateDistance.h/cpp    ← 精简, 只派生原始事实 (Distance + AttackRange)
└── AIBehaviorTypes.h               ← 更新 BB Key 常量
```

**删除**:
- `BTTask_FaceTarget.h/cpp` — UE 原生 `Rotate to face BB entry` 已提供, 底层都用 SetFocalPoint

**为什么不用自己写 FaceTarget**:
- UE 原生节点底层就是 AAIController::SetFocalPoint
- 自定义版本与原生版本功能完全等价, 是冗余造轮子
- 大厂原则: BT 工具节点能用就用, 不另起一套

**为什么不用自己写 MoveToTarget**:
- UE 原生 `Move To` 已支持 BB Key 动态参数 (UE5.5+)
  - `AcceptableRadius`: FValueOrBBKey_Float
  - `bTrackMovingGoal`: FValueOrBBKey_Bool
  - `bAllowPartialPath`: FValueOrBBKey_Bool
- Chase Sequence 直接用 UE 原生 `Move To` 即可 (指向 BB.TargetActor)

**为什么需要自定义 MoveAwayFromTarget**:
- UE 原生没有 "Move Away" 能力
- 计算方向 + 投影 NavMesh 是真正的原子能力, 必须 C++

**Decorator 为什么用 FBlackboardKeySelector**:
- 大厂标准做法, 让 BT 编辑器手动绑定 BB Key (而不是硬编码字符串)
- 改 BB Key 名不需要改 C++
- BT 资产层透明可读, 策划在 BT Details 面板一看就知道绑定到哪个 Key

---

## 7. 验证清单

1. **后退验证**: 玩家贴近 AI (D < 180cm), 看 Retreat Sequence 激活且 AI 真后退 100cm
2. **攻击验证**: 玩家站在 170~190cm, 看 Attack Sequence 激活且播攻击动画
3. **追击验证**: 玩家走远 (D > 190cm), 看 Chase Sequence 激活且 AI 追过来
4. **区间边界验证**:
   - D = 180cm: TooClose ✓ (Retreat), InAttackRange ✓ (Attack) → 按 Selector 优先级
   - D = 190cm: TooClose false, InAttackRange ✓ → Attack
   - D = 191cm: TooClose false, InAttackRange false, ShouldChase ✓ → Chase
5. **抖动验证**: 玩家站在 180±5cm 来回踱步, Selector 不应高频横跳
6. **配置生效**: ConfigSO 改 AttackRange=300, 所有区间自动更新
7. **冷却验证**: 攻击后立刻贴近, 应等 AttackCooldown 秒后才能再攻击

---

## 8. 架构总结

```
ConfigSO (策划配置)
      │
      ▼
BTService_UpdateDistance (0.1s 派生)
      │
      ├── BB.DistanceToTarget
      ├── BB.AttackRange
      └── BB.bHasTarget
      │
      ▼
BTDecorator (C++ 决策节点)
      │
      ├── TooClose?      → D ≤ AR
      ├── InAttackRange? → AR-M ≤ D ≤ AR+M
      └── ShouldChase?   → D > AR+M
      │
      ▼
BTTask (C++ 原子能力, 仅 UE 原生做不到的)
      │
      ├── MoveAwayFromTarget  ← UE 原生没有 "Move Away" 能力
      ├── PlayAttackMontage   ← 业务接口 (项目自定)
      ├── WaitMontageFinish   ← 业务接口 (项目自定)
      └── PlayDeath           ← 哨兵节点
      │
      ▼
UE 原生节点 (BT 编辑器自带, 直接用)
      │
      ├── Rotate to face BB entry   ← 替代 FaceTarget (底层 SetFocalPoint)
      └── Move To                   ← 替代 MoveToTarget (BB Key 动态参数 UE5.5+)
```

**策划改 ConfigSO → Service 自动派生到 BB → Decorator 实时决策 → Task/UE 原生 执行原子动作**
