# AdeptivePantyhose

一个 SKSE 插件：根据角色当前穿戴的装备动态调整 BodyMorph，在不同鞋型（高跟 / 平底 / 赤脚）与裤袜组合下消除脚部与衣物之间的接缝。

基于 CMake、vcpkg 和 [CommonLibSSE](https://github.com/powerof3/CommonLibSSE) 构建，通过 [SKEE](https://github.com/Expired6978/SKSE64Plugins) 的 BodyMorph 接口读写 morph。

## ✨ 特性

- **数据驱动**：装备识别与 morph 动作完全由 `MorphRules.json` 定义，改规则不需要重新编译。
- **无状态求值**：每次事件独立求值，不维护跨帧缓存，避免存档兼容性与状态漂移。
- **分层解耦**：配置解析与游戏领域知识分离，语法层可脱离 Skyrim 环境单测。
- **Fail fast**：配置错误在加载期以弹窗 + 日志终止，避免插件静默失效。
- **双版本构建**：SE 1.5.97 与 AE 1.6.1130+ 分别产出独立 DLL，互不覆盖。

详细设计见 [`docs/Architecture.md`](docs/Architecture.md)。

## 📦 环境要求

- Windows 10/11 x64
- Visual Studio 2022（勾选“使用 C++ 的桌面开发”，含 **C++ Clang tools for Windows**）
- CMake 3.31+（presets 需要；只用 `CMakeLists.txt` 手动配置时最低 3.26）、Ninja、Git
- vcpkg，并设置 `VCPKG_ROOT`
- Skyrim SE 1.5.97 + SKSE64 2.0.20
  或 Skyrim AE 1.6.1130+ + SKSE64 2.3.0+（两者各装一份 Address Library）
- [SKEE64](https://github.com/Expired6978/SKSE64Plugins)（运行期依赖，BodyMorph 接口 >= v4）
- VS Code + `llvm-vs-code-extensions.vscode-clangd`
- MO2

**clang-cl 和 clangd 扩展建议一定装上**，装了会舒服很多。

## 🚀 快速开始

```powershell
# 1. 准备 vcpkg
git clone https://github.com/microsoft/vcpkg.git C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\dev\vcpkg", "User")
# 设完推荐重开一下终端

# 2. 克隆项目（带 CommonLibSSE 子模块）
git clone --recurse-submodules "https://github.com/Charlenehoo/AdeptivePantyhose2.git"
cd AdeptivePantyhose2

# 3. 推荐用 Developer PowerShell for VS 2022

# 4. 先喂 clangd（按目标版本二选一）
cmake --preset clang-ae-debug          # AE 1.6.1130+
# cmake --preset clang-se-debug        # SE 1.5.97

# 5. 再构建（目标版本与上一步保持一致）
cmake --preset msvc-ae-debug
cmake --build --preset msvc-ae-debug
# cmake --preset msvc-se-debug
# cmake --build --preset msvc-se-debug
```

输出：

```
build/msvc-ae-debug/AdeptivePantyhose_AE.dll   # AE 版
build/msvc-se-debug/AdeptivePantyhose_SE.dll   # SE 版
```

首次构建会编译 vcpkg 依赖，耗时会长一些，耐心等等就好。

## 🛠 构建预设

**日常构建（MSVC）**

| Preset            | 用途                       |
| ----------------- | -------------------------- |
| `msvc-ae-debug`   | AE 日常开发（AE 用户推荐） |
| `msvc-ae-release` | AE 发布                    |
| `msvc-se-debug`   | SE 日常开发（SE 用户推荐） |
| `msvc-se-release` | SE 发布                    |

**clangd 索引**

| Preset             | 用途                      |
| ------------------ | ------------------------- |
| `clang-ae-debug`   | 喂 clangd，不用于产出插件 |
| `clang-ae-release` | 同上                      |
| `clang-se-debug`   | 同上                      |
| `clang-se-release` | 同上                      |

`clang-*` 的职责是给 clangd 喂编译数据库，插件推荐由 `msvc-*` 产出。

SE 与 AE 产物名不同（`_SE` / `_AE` 后缀），部署目录也不同，可以共存于同一份 MO2，按游戏版本启用对应 mod。

## 🧠 clangd

推荐固定用这个流程：

```powershell
cmake --preset clang-ae-debug    # 喂 clangd
cmake --preset msvc-ae-debug     # 实际构建
cmake --build --preset msvc-ae-debug
```

三条小建议：

1. `clang-cl` 推荐装上。
2. `llvm-vs-code-extensions.vscode-clangd` 推荐装上。
3. `--compile-commands-dir` 推荐指向 `build/clang-ae-debug`（模板已经写好了）。

`.clangd` 里 `CompilationDatabase` 默认走 `build/clang-ae-debug`。只做 SE 版开发的话，改成 `build/clang-se-debug` 即可——AE 的 CommonLibSSE 是 SE 的超集，用 AE 索引 SE 代码基本不会误报，除非代码里大量依赖 SE-specific 宏。

原因：`cl.exe` 生成的 `compile_commands.json` 里全是 clangd 解析不了的东西——MSVC 专有 flag、STL 内部扩展、隐式定义。喂给 clangd 容易满屏误报，索引体验会差很多。

## 🎮 部署

推荐用 MO2。配置前设置环境变量：

```powershell
$env:MO2 = "D:\MO2"
cmake --preset msvc-ae-debug
cmake --build --preset msvc-ae-debug
```

插件会自动复制到：

```
D:\MO2\mods\AdeptivePantyhose_AE\SKSE\Plugins\   # AE
D:\MO2\mods\AdeptivePantyhose_SE\SKSE\Plugins\   # SE
```

Debug 构建还会带上 PDB。SE 与 AE 部署到不同 mod 目录，MO2 里按游戏版本启用对应的那个即可，两个 mod 可以同时装着。

`assets/` 下的内容（含 `MorphRules.json` 与 BodySlide 项目）也会同步到 mod 目录。

想持久化的话：`[Environment]::SetEnvironmentVariable("MO2", "D:\MO2", "User")`

## ⚙️ 配置

规则文件位于 MO2 的：

```
mods/AdeptivePantyhose_AE/SKSE/AdeptivePantyhose/MorphRules.json   # AE
mods/AdeptivePantyhose_SE/SKSE/AdeptivePantyhose/MorphRules.json   # SE
```

与 [JSON Schema](assets/SKSE/AdeptivePantyhose/MorphRules.schema.json) 配套，编辑器可以补全和校验。

顶层分两段：

- **`Predicates`**：命名谓词表。叶子类型由 `PredicateCompiler` 的注册表决定，目前支持 `ByKeywords`（关键字匹配）与 `ByFormIDRange`（来源插件 + local FormID 范围）。组合子有 `AnyOf` / `AllOf` / `Not`，内置保留谓词 `Default` 恒为真。
- **`Morphs`**：morph 规则表，键为 SKEE BodyMorph 中的 morph 名。每个 morph 用 `TriggeredBy` 声明触发它的 biped 槽（如 `kFeet`），`Rules` 是有序列表，**第一个命中的 `When` 生效**。

一个最小示例：

```json
{
  "$schema": "./MorphRules.schema.json",
  "Predicates": {
    "KillerHeels": { "ByKeywords": ["SLA_KillerHeels"] },
    "CosplayPackHeels": {
      "ByFormIDRange": [
        {
          "Plugin": "[Caenarvon] Cosplay Pack.esp",
          "Min": "0xDDF",
          "Max": "0xDDF"
        }
      ]
    },
    "HighHeel": { "AnyOf": ["KillerHeels", "CosplayPackHeels"] }
  },
  "Morphs": {
    "NoHeel": {
      "TriggeredBy": "kFeet",
      "Rules": [
        { "Value": 0.0, "When": "HighHeel" },
        { "Value": 1.0, "When": "Default" }
      ]
    }
  }
}
```

`Value` 是 morph 的目标绝对值；`0.0` 表示清除该 morph。

配置错误会在启动时以 `REX::FAIL` 弹窗终止，并附带精确的 JSON 路径，几秒内就能定位。

## 📝 日志

```text
Documents/My Games/Skyrim Special Edition/SKSE/AdeptivePantyhose.log
```

SE 与 AE 共用同一份日志——SKSE 日志名来自插件的 `Plugin::kName`，两版构建的 `kName` 相同。**不要同时启用 SE 和 AE 两个 mod**，否则日志会互相覆盖。

`CMakeLists.txt` 会在 configure 阶段把它硬链接到项目根的 `AdeptivePantyhose.log`。如果 configure 时日志还不存在，跑一次游戏，再重新执行任意一条 configure 命令即可（例如 `cmake --preset msvc-ae-debug`）——任意 preset 都行，关键是重新 configure 而不是只 `cmake --build`。

## 📄 自定义

改 `CMakeLists.txt` 顶部：

```cmake
project(AdeptivePantyhose VERSION 3.0.0 LANGUAGES CXX)
set(PROJECT_AUTHOR "Charlene Hoo")
set(PROJECT_AUTHOR_EMAIL "CharleneHoo@hotmail.com")
```

改项目名后，DLL 名（含 `_SE` / `_AE` 后缀）、MO2 目录、日志名会同步变，`vcpkg.json` 里的 `"name"` 推荐保持小写同步改一下。

改版本号时记得三处一起改：`CMakeLists.txt` 的 `VERSION`、`vcpkg.json` 的 `"version"`、`docs/Architecture.md` 头部。

## 🧩 扩展规则

**加一条新规则**：直接编辑 `MorphRules.json`，不用重新编译。

**加一个新的叶子谓词类型**：在 `src/MorphRuleEngine/Leaves/` 下新建一个 `MakeXPredicate(const nlohmann::json&) -> std::expected<PredicatePtr, LeafError>`，在 `src/main.cpp` 的 `LoadMorphRules` 里 `compiler.RegisterLeaf("X", &Engine::MakeXPredicate)`，再在 `MorphRules.schema.json` 的 `PredicateLeaf` 里补一条分支。

**注意**：新增叶子类型前先评估——如果它表达的是"armor 属于哪一类"，大概率应该是谓词；如果表达的是"哪个槽位触发"，应该做成 `TriggeredBy` 的职责。

## ❓ 常见问题

**`VCPKG_ROOT` 未设置**  
推荐这样设一下：`[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\dev\vcpkg", "User")`，然后重开终端。

**`VCToolsInstallDir` 为空**  
换成 Developer PowerShell for VS 2022 就好。

**CommonLibSSE 缺失**  
`git submodule update --init --recursive`

**clangd 找不到 `compile_commands.json`**  
先跑一下 `cmake --preset clang-ae-debug`（或 `clang-se-debug`）。

**clangd 满屏误报**  
推荐检查四件事：装了 clang-cl 吗、装了 clangd 扩展吗、`--compile-commands-dir` 是不是 `build/clang-ae-debug`、`C_Cpp.intelliSenseEngine` 是不是 `"disabled"`。

**游戏里没加载插件**  
推荐依次看：SKSE 版本、Address Library、DLL 位置，然后翻日志。日志里出现 `MorphRules loaded: N morph(s)` 才算成功。

**SE 版装成了 AE 版（或反过来）**  
现象是"插件加载成功但事件回调不触发"。SE 用户看到日志里没有 `[Equip]` 行，就是 DLL 装错了。SE 用 `AdeptivePantyhose_SE.dll`，AE 用 `AdeptivePantyhose_AE.dll`。

**SKEE 未安装 / 版本过旧**  
日志里会有 `SKEEIntegration: SKEE not available, integration disabled` 或 `BodyMorph version too old`。此时插件本身加载成功，但所有 morph 操作静默失效。

**配置文件报错弹窗**  
弹窗里会带精确的 JSON 路径（如 `Predicates.WearingHighHeels.AnyOf[1]`）和人类可读的原因，照提示改就行。

**MO2 自动复制没生效**  
`echo $env:MO2` 最好指向 MO2 根目录，设完重开终端重新 `cmake --preset` 一下。

## 📜 License

GPL-3.0-or-later，见 `LICENSE`。

本项目链接了 GPL-3.0 的 CommonLibSSE，衍生插件推荐同样以 GPL-3.0 兼容协议开源。**请勿改为 MIT。**

## 👤 关于作者

Charlene Hoo  
[CharleneHoo@hotmail.com](mailto:CharleneHoo@hotmail.com)
