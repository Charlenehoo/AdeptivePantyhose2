# AttachBipedObject 直接 hook 可行性调查报告

**版本**：v3.0.6 设计期
**日期**：2026-11-02
**报告人**：Charlene Hoo
**状态**：源码调查完成，两个待确认点未验证，决策待定
**关联**：

- [`actorupdate-manager-abi-report-2026-11-01.md`](actorupdate-manager-abi-report-2026-11-01.md) — 3.0.6beta 崩溃根因，原决策"放弃 SE"
- [`skee-behavior-notes.md`](skee-behavior-notes.md) — SKEE 行为笔记，§2.1 OnAttach 调用链
- [`onattach-thread-affinity-report-2026-10-30.md`](onattach-thread-affinity-report-2026-10-30.md) — v3.0.5 OnAttach 跨线程调查

---

## 0. 与 ABI 报告的关系

ABI 报告（`actorupdate-manager-abi-report-2026-11-01.md`）的结论是：**SE 1.5.97 锁在 RaceMenu 0.4.16，`IActorUpdateManager::AddInterface` 不存在，v3.0.3+ 的 mesh 更新只走 `OnAttach`，所以 SE 1.5.97 放弃支持，冻结 v3.0.2**。

本文档调查一条**替代路径**：如果自己 hook `AttachBipedObject` 游戏函数，直接拿到 `OnAttach` 信号，不依赖 SKEE 的 `IActorUpdateManager` 总线，是否可以绕开 `AddInterface` 的 ABI 问题，从而保留 SE 1.5.97 支持。

**本文档不推翻 ABI 报告，只暂缓其 §5 的"放弃 SE"决策**，直到本调查的两个待确认点（§6）有结论。如果两个待确认点都通过，ABI 报告的"放弃 SE"应修订为"保留 SE，但路径从 SKEE 总线改为自 hook"；如果不通过，ABI 报告的决策成立，回到"门禁 + no op"。

---

## 1. 问题回顾

### 1.1 当前 SKEE 依赖的两层

```

层级 A：OnAttach 事件的来源
InterfaceExchangeMessage → interfaceMap
→ QueryInterface("ActorUpdateManager") → IActorUpdateManager
→ AddInterface(&AttachListener) ← 3.0.6beta 崩在这里（旧 SKEE vtable 越界）

层级 B：morph 读写的来源
InterfaceExchangeMessage → interfaceMap
→ QueryInterface("BodyMorph") → IBodyMorphInterface
→ SetMorph / ClearMorph / ApplyVertexDiff / UpdateModelWeight

```

**层级 B 绕不掉**。`IBodyMorphInterface` 的 morph 存储（`actorMorphs[formID][morphName][key]`）和 D3D 上传逻辑（`MorphFileCache`）只有 SKEE 有。自己实现等于 fork 几千行 SKEE 核心。

**层级 A 可以绕**。`IActorUpdateManager` 只是 SKEE 内部的"OnAttach 事件总线"，它本身不做 morph，只把 mesh attach 信号转发给注册的 observer。绕开它，就是自己 hook 同一个游戏函数，直接拿到 `OnAttach` 信号。

### 1.2 收益假设

如果绕开层级 A：

- 不再调 `AddInterface`，**vtable 越界崩溃的路径消失**
- 不再依赖 `ActorUpdateManager` 的存在，**旧 SKEE 只要 BodyMorph >= v4 就能用**
- SE 1.5.97（RaceMenu 0.4.16）可能保留支持

---

## 2. 调查方法

读 SKEE 源码（`github.com/Expired6978/SKSE64Plugins`），定位：

1. `AttachBipedObject` 的 hook 点（`skee64/SKEEHooks.cpp`）
2. `OnAttach` 七个参数的重建逻辑（`AttachBipedObject_Hooked` + `InstallArmorAddonHook`）
3. 对比 CommonLibSSE-NG（`extern/CommonLibSSE`）的字段暴露

---

## 3. 关键发现

### 3.1 `AttachBipedObject` 的 REL::ID 跨版本稳定

`skee64/SKEEHooks.cpp:121,150`：

```cpp
// SE 1.5.97
static const uintptr_t AttachBipedObject = 0x0021CE30;  // 15711
// AE 1.6.x
static const uintptr_t AttachBipedObject = 0x00217380;  // 15711
```

运行时地址不同（SE `0x0021CE30`，AE `0x00217380`），但 **Address Library ID 都是 15711**。这意味着：

```cpp
REL::Relocation<AttachBipedObject_t> g_attachBipedObject{ REL::ID(15711) };
```

一行代码覆盖 SE + AE，不需要 `#ifdef`。**这是自己 hook 可行的第一个前提，已满足。**

### 3.2 函数签名

`skee64/SKEEHooks.cpp:329`：

```cpp
NiAVObject* AttachBipedObject_Hooked(
    Biped* bipedInfo,       // BipedAnim*
    NiNode* objectRoot,
    UInt32 bipedIndex,
    UInt32 unkIndex,
    UInt8 unk5,
    UInt8 unk6,
    UInt64 unk7)
```

7 个参数，前 4 个有意义，后 3 个 SKEE 作者未搞清楚。返回值 `NiAVObject*` 是 attach 后的 mesh 节点。

### 3.3 参数重建全部基于公开结构

`skee64/SKEEHooks.cpp:329-402` 的完整实现拆解：

```cpp
NiAVObject* AttachBipedObject_Hooked(Biped* bipedInfo, NiNode* objectRoot, UInt32 bipedIndex,
                                     UInt32 unkIndex, UInt8 unk5, UInt8 unk6, UInt64 unk7) {
    NiAVObject* retVal = AttachBipedObject_Original(bipedInfo, objectRoot, bipedIndex, unkIndex, unk5, unk6, unk7);

    NiPointer<TESObjectREFR> reference;
    UInt32 handle = bipedInfo->handle;
    LookupREFRByHandle(handle, reference);
    if (reference)
        InstallArmorAddonHook(reference, bipedInfo->unk10[bipedIndex], bipedInfo->root, retVal);

    return retVal;
}

void InstallArmorAddonHook(TESObjectREFR* refr, Biped::Data& params, NiNode* boneTree, NiAVObject* resultNode) {
    // ... 参数校验 ...

    NiNode* node3P = refr->GetNiRootNode(0);
    NiNode* node1P = refr->GetNiRootNode(1);

    NiNode* rootNode = nullptr;
    NiNode* parent = boneTree->m_parent;
    do {
        if (parent == node1P) rootNode = node1P;
        if (parent == node3P) rootNode = node3P;
        parent = parent->m_parent;
    } while (parent);

    bool isFirstPerson = (rootNode == node1P);
    if (node1P == node3P) isFirstPerson = false;
    if (rootNode != node1P && rootNode != node3P) return;

    if (params.armor->formType == TESObjectARMO::kTypeID && params.addon->formType == TESObjectARMA::kTypeID) {
        NiPointer<NiAVObject> node = resultNode;
        g_actorUpdateManager.OnAttach(refr, static_cast<TESObjectARMO*>(params.armor),
                                      static_cast<TESObjectARMA*>(params.addon), node, isFirstPerson,
                                      isFirstPerson ? node1P : node3P, boneTree);
    }
}
```

**`OnAttach` 七个参数的映射：**

| `OnAttach` 参数 | SKEE 来源                                                   | CommonLibSSE-NG 对应                                    |
| --------------- | ----------------------------------------------------------- | ------------------------------------------------------- |
| `refr`          | `bipedInfo->handle` → `LookupREFRByHandle`                  | `BipedAnim::actorRef` → `TESObjectREFR::LookupByHandle` |
| `armor`         | `bipedInfo->unk10[bipedIndex].armor`                        | `BipedAnim::objects[i].item` → `As<TESObjectARMO>()`    |
| `addon`         | `bipedInfo->unk10[bipedIndex].addon`                        | `BipedAnim::objects[i].addon`                           |
| `object`        | `AttachBipedObject_Original` 返回值                         | 原始函数返回值 `retVal`                                 |
| `isFirstPerson` | `refr->GetNiRootNode(0/1)` 比较 + `boneTree->m_parent` 上溯 | `TESObjectREFR::GetNiRootNode` + `NiNode::parent`       |
| `skeleton`      | `isFirstPerson ? node1P : node3P`                           | `GetNiRootNode`                                         |
| `root`          | `bipedInfo->root`                                           | `BipedAnim::root`                                       |

**没有一处依赖 SKEE 内部状态。** 这是自己 hook 可行的第二个前提，已满足。

### 3.4 CommonLibSSE-NG 字段完整暴露

`extern/CommonLibSSE/include/RE/B/BipedAnim.h`（1888 字节，全文关键部分）：

```cpp
struct BIPOBJECT {
    TESForm*              item;          // 00  ← armor
    TESObjectARMA*        addon;         // 08  ← addon（直接有！）
    TESModel*             part;          // 10
    BGSTextureSet*        skinTexture;   // 18
    NiPointer<NiAVObject> partClone;     // 20  ← object
    BSTHeapObjectArena<ModelDBHandle, 16> handleList;  // 28
    BSTSmartPointer<WeaponAnimationGraphManagerHolder> weaponManager;  // 60
    bool                  skinned;       // 68
    ModelDBHandle         partHandle;    // 70
};
static_assert(sizeof(BIPOBJECT) == 0x78);

class BipedAnim : public BSIntrusiveRefCounted {
public:
    // ...
    std::uint32_t   pad0004;                                 // 0004
    NiNode*         root;                                    // 0008  ← root
    BIPOBJECT       objects[BIPED_OBJECTS::kTotal];          // 0010  ← SKEE 的 unk10
    BIPOBJECT       bufferedObjects[BIPED_OBJECTS::kTotal];  // 13C0  ← SKEE 的 unk13C0
    ObjectRefHandle actorRef;                                // 2770  ← SKEE 的 handle
    std::uint32_t   pad2774;                                 // 2774
};
static_assert(sizeof(BipedAnim) == 0x2778);
```

**关键**：`BIPOBJECT::addon` 直接暴露（`TESObjectARMA*`），不需要从 `armor->armorAddons` 反查。这是最后一个不确定点的解除。

`extern/CommonLibSSE/include/RE/T/TESObjectREFR.h:341-342`：

```cpp
static NiPointer<TESObjectREFR> LookupByHandle(RefHandle a_refHandle);
static bool                     LookupByHandle(RefHandle a_refHandle, NiPointer<TESObjectREFR>& a_refrOut);
```

`LookupByHandle` 静态方法存在，`refr` 重建无问题。

### 3.5 SKEE 的 hook 机制

`skee64/SKEEHooks.cpp:1397-1418`：

```cpp
struct AttachBipedObjectHook_Entry_Code : Xbyak::CodeGenerator {
    AttachBipedObjectHook_Entry_Code(void* buf, uintptr_t address) : Xbyak::CodeGenerator(4096, buf) {
        Xbyak::Label retnLabel;
        mov(rax, rsp);
        mov(ptr[rax + 0x20], r9b);
        jmp(ptr[rip + retnLabel]);
        L(retnLabel);
        dq(address + 7);   // 原始函数 + 7 字节（跳转指令长度）
    }
};

void* codeBuf = g_localTrampoline.StartAlloc();
AttachBipedObjectHook_Entry_Code code(codeBuf, AttachBipedObject.GetUIntPtr());
g_localTrampoline.EndAlloc(code.getCurr());
AttachBipedObject_Original = (_AttachBipedObject)codeBuf;

g_branchTrampoline.Write6Branch(AttachBipedObject.GetUIntPtr(), (uintptr_t)AttachBipedObject_Hooked);
```

SKEE 用 Xbyak 手写 trampoline（为了兼容其他插件对同一函数的 hook），`Write6Branch` 写 6 字节跳转。**这种 hook 方式理论上不排斥第二个 hook**——SKSE 的 `GetTrampoline()` 在 CommonLibSSE-NG 里返回同一个底层 trampoline，`write_call<6>` 会检测已有跳转并生成正确的链式跳转。

**但需要实测验证**（见 §6.1）。

---

## 4. 两个方案的精确对比

| 维度                           | 方案 A：门禁 + no op（ABI 报告 §4） | 方案 B：自己 hook `AttachBipedObject`     |
| ------------------------------ | ----------------------------------- | ----------------------------------------- |
| **SE 1.5.97 支持**             | ❌ 放弃（v3.0.2 冻结）              | ✅ **可能保留**（待 §6.2 确认）           |
| 对 `IActorUpdateManager` 依赖  | 强（必须存在且 >= v2）              | **无**                                    |
| 对 `IBodyMorphInterface` 依赖  | 强                                  | 强（不变）                                |
| `AddInterface` vtable 越界风险 | 门禁拦截                            | **路径消失**                              |
| 代码量                         | ~0（改 `Initialize` 返回 bool）     | **~100 行**（可照抄 SKEE）                |
| 与 SKEE hook 链兼容性          | 不涉及                              | **待验证**（§6.1）                        |
| 与 IED / 其他 D3D mod 冲突     | 无                                  | 有（共享 `AttachBipedObject` trampoline） |
| 维护成本                       | 低                                  | 中（跟 Address Library + SKEE hook 点）   |
| 调试难度                       | 低                                  | 中（hook 链出问题时难定位）               |

**核心权衡**：方案 B 用 ~100 行代码 + hook 链风险，换 SE 1.5.97 支持。

---

## 5. 方案 B 的实现框架（待验证后落地）

### 5.1 新增 `src/Integration/SKEE/AttachHook.{h,cpp}`

```cpp
// AttachHook.h
#pragma once

namespace Hook::AttachBipedObject {
    // 在 kPostPostLoad 调用。注册 AttachBipedObject 的 hook，
    // 与 SKEE 的 hook 形成链。失败返回 false。
    [[nodiscard]] auto Install() -> bool;
}
```

```cpp
// AttachHook.cpp（完整实现见 §5.3）
```

### 5.2 `SKEEIntegration::Initialize()` 简化

不再查 `IActorUpdateManager`、不再 `AddInterface`：

```cpp
auto SKEEIntegration::Initialize() -> bool {
    // ... InterfaceExchangeMessage 分发不变 ...
    // ... QueryInterface("BodyMorph") + GetVersion() >= 4 检查不变 ...

    m_bodyMorph = bodyMorph;
    REX::INFO("SKEEIntegration: initialized (bodyMorphVersion={})", bodyMorphVersion);
    return true;
    // 删除：QueryInterface("ActorUpdateManager")
    // 删除：ActorUpdateManager::GetVersion() 检查
    // 删除：AddInterface(&AttachListener::GetSingleton())
}
```

`m_updateManager` 成员删除。

### 5.3 `AttachHook.cpp` 的 `DispatchOnAttach`

照抄 SKEE 的 `InstallArmorAddonHook`，只保留我们需要的部分（不调 Overlay / Override 等其他观察者）：

```cpp
void DispatchOnAttach(RE::TESObjectREFR* a_refr, RE::BIPOBJECT& a_params,
                      RE::NiNode* a_boneTree, RE::NiAVObject* a_resultNode) {
    if (a_refr == nullptr || a_params.item == nullptr || a_params.addon == nullptr
        || a_boneTree == nullptr || a_resultNode == nullptr) return;

    // 与 SKEE 完全一致：从 boneTree 往上遍历，判断 1P / 3P
    RE::NiNode* node3P = a_refr->GetNiRootNode(0);
    RE::NiNode* node1P = a_refr->GetNiRootNode(1);
    RE::NiNode* rootNode = nullptr;
    for (RE::NiNode* parent = a_boneTree->parent; parent != nullptr; parent = parent->parent) {
        if (parent == node1P) rootNode = node1P;
        if (parent == node3P) rootNode = node3P;
    }
    bool isFirstPerson = (rootNode == node1P);
    if (node1P == node3P) isFirstPerson = false;
    if (rootNode != node1P && rootNode != node3P) return;

    if (a_params.item->GetFormType() != RE::FormType::Armor
        || a_params.addon->GetFormType() != RE::FormType::ArmorAddon) return;

    auto* armor = a_params.item->As<RE::TESObjectARMO>();
    if (armor == nullptr) return;

    AttachListener::GetSingleton().OnAttach(
        a_refr, armor, a_params.addon, a_resultNode, isFirstPerson,
        isFirstPerson ? node1P : node3P, a_boneTree);
}
```

### 5.4 `AttachListener` 保持不动

`AttachListener::OnAttach` 的签名和实现完全不变——它实现 `IAddonAttachmentInterface` 只是为了签名兼容，**现在不注册到 SKEE 的 `IActorUpdateManager`，而是被自己的 hook 直接调用**。

### 5.5 `main.cpp`

```cpp
case SKSE::MessagingInterface::kPostPostLoad: {
    LOG_DEBUG("OnMessage: PostPostLoad");
    g_skeeAvailable = SKEEIntegration::GetSingleton().Initialize();
    if (g_skeeAvailable) {
        // hook 独立于 SKEE 接口，但只在 BodyMorph 可用时才装——
        // 没有 BodyMorph 的话 OnAttach 里的 EvaluateRules / ApplyVertexDiff 全是 no-op
        if (!Hook::AttachBipedObject::Install()) {
            g_skeeAvailable = false;
        }
    }
    break;
}
```

---

## 6. 待确认事项

### 6.1 hook 链兼容性（阻塞方案 B）

SKEE 用 `g_branchTrampoline.Write6Branch`，你用 `SKSE::GetTrampoline().write_call<6>`。两者是否形成正确的链，**未验证**。

**验证方法**：

1. 写最小 hook（日志打印 `[AttachHook] called`，不调任何逻辑），`kPostPostLoad` 注册
2. 进游戏穿裤袜（触发 `AttachBipedObject`）
3. 确认**两个日志都出现**：
   - `AdeptivePantyhose.log` 里有 `[AttachHook] called`（你的 hook 触发）
   - `skee.log` 里有 `MorphCache::CacheFile - Parsing: cpb_s.tri`（SKEE 的 hook 仍触发）

**两者都触发 = 链正确；只有一个 = 链断了。**

**如果链断了**，备选方案：

- 尝试不同的 `write_call` 长度（`<5>` / `<6>` / `<7>`）
- 用 `write_branch` 代替 `write_call`
- 如果都不行，**方案 B 放弃**，回到方案 A

### 6.2 SE 1.5.97 的 BodyMorph 版本（阻塞方案 B 的 SE 部分）

即使 hook 链正确，SE 1.5.97 要能用 v3.0.6，需要 RaceMenu 0.4.16 的 `IBodyMorphInterface::GetVersion() >= 4`。

**确认方法**：

- 让 AvatarWenyihaobb 提供 `Data\SKSE\Plugins\skee64.dll` 的文件版本（右键 → 属性 → 详细信息）
- 或实机跑一次，看 `AdeptivePantyhose.log` 里 `SKEEIntegration: initialized (bodyMorphVersion=N)` 的 N 是多少

**如果 < 4**：即使自己 hook 拿到 `OnAttach`，`ApplyVertexDiff` 的行为也不满足 v3.0.3+ 需求，SE 1.5.97 还是只能停在 v3.0.2。方案 B 的 SE 收益消失。

### 6.3 `ObjectRefHandle` → `RefHandle` 转换（不阻塞，实现时查）

`BipedAnim::actorRef` 是 `ObjectRefHandle`，`LookupByHandle` 要 `RefHandle`。查 `extern/CommonLibSSE/include/RE/B/BSPointerHandle.h` 确认转换方式。

### 6.4 `GetNiRootNode` 是否存在（不阻塞，实现时查）

grep 没匹配到 `GetNiRootNode`。查 `TESObjectREFR.h` 确认。如果没有，用 `Get3D()` + `skyrim_cast<NiNode*>` 替代。

---

## 7. 决策树

```
§6.1 hook 链验证通过？
├── 否 → 方案 B 放弃，回到方案 A（门禁 + no op），ABI 报告 §5 成立
└── 是 → §6.2 SE BodyMorph >= 4？
    ├── 否 → 方案 B 对 AE 可用，对 SE 无收益。
    │        选方案 B（AE 走自 hook，不依赖 IActorUpdateManager）
    │        但 SE 仍放弃。ABI 报告 §5 修订为"SE 放弃，AE 走自 hook"
    └── 是 → 方案 B 全面可用。
              ABI 报告 §5 修订为"保留 SE，路径从 SKEE 总线改为自 hook"
              v3.0.6 同时支持 SE + AE
```

---

## 8. 与 ABI 报告的交叉修订（待 §7 有结论后）

如果 §7 走"方案 B 全面可用"：

- `actorupdate-manager-abi-report-2026-11-01.md` §5.1 的"SE 最终版 = v3.0.2"修订为"SE 由 v3.0.6 继续支持（自 hook 路径）"
- §5.2 的"不做 `#ifdef` 双路径"保留，因为 `REL::ID(15711)` 跨版本稳定，不需要编译分支
- §7 验证清单新增"SE 1.5.97 + v3.0.6：`[AttachHook] called` + `[Attach]` 正常"
- §8 变更记录同步

如果 §7 走"方案 A"：

- ABI 报告不变，本文档记录"自 hook 路径已调查，因 §6.1 或 §6.2 不通过而放弃"
- 作为未来 SKEE 更新时的参考（如果未来 SE 的 BodyMorph 升级到 v4，可以重新评估）

---

## 9. 相关文件

| 文件                                                       | 作用                                                 |
| ---------------------------------------------------------- | ---------------------------------------------------- |
| `skee64/SKEEHooks.cpp:121,150`                             | `AttachBipedObject` 的 REL::ID（SE / AE 两份）       |
| `skee64/SKEEHooks.cpp:329-402`                             | `AttachBipedObject_Hooked` + `InstallArmorAddonHook` |
| `skee64/SKEEHooks.cpp:1397-1418`                           | Xbyak trampoline + `Write6Branch`                    |
| `extern/CommonLibSSE/include/RE/B/BipedAnim.h`             | `BipedAnim` / `BIPOBJECT` 字段定义                   |
| `extern/CommonLibSSE/include/RE/B/BipedObjects.h`          | `BIPED_OBJECTS::kTotal = 42`                         |
| `extern/CommonLibSSE/include/RE/T/TESObjectREFR.h:341-342` | `LookupByHandle` 静态方法                            |
| `docs/actorupdate-manager-abi-report-2026-11-01.md`        | 原 ABI 报告，§5 决策待本文档修订                     |
| `docs/skee-behavior-notes.md` §2.1                         | SKEE 的 OnAttach 调用链                              |

---

**报告结束。**
