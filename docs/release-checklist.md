# 发布清单（Release / Beta）

> 一个版本 = **一个 commit + 一个 tag + 一个 zip**。beta 与 release 用**同一套构建配置**，
> 只是版本串不同——不维护第三套渠道。

---

## 0. 版本号与渠道

| 项 | 规则 | 示例 |
| --- | --- | --- |
| `PE_VERSION` | 语义化版本 `MAJOR.MINOR.PATCH` | `0.3.0` |
| `PE_CHANNEL` | `release` / `beta` / `dev` | `beta` |
| git tag | release：`v0.3.0`；beta：`v0.3.0-beta.1` | `v0.3.0` |
| 窗口标题/日志 | 自动显示 `版本-渠道 (SHA)` | `0.3.0-beta (110ceba)` |
| 存档文件 | release `cs2d_save.ini`；beta/dev 加后缀 | `cs2d_save_beta.ini` |

渠道只影响：标题标识、存档后缀、dev 专用命令行参数（`--killplayer`、`--record`）。
**不改变玩法逻辑，也不影响确定性哈希。**

---

## 1. 发布前检查（逐项打勾）

```powershell
# 1) 工作区干净、分支正确
git status
git log --oneline -1

# 2) 全量构建（release 渠道）
cmake -S PixelEngine -B build-release -G "MinGW Makefiles" `
      -DCMAKE_BUILD_TYPE=Release "-DPE_VERSION=0.3.0" "-DPE_CHANNEL=release"
cmake --build build-release --parallel 8
```

- [ ] 构建无 error（warning 记录条数）
- [ ] `gameplay_tests` **全绿**（当前基线 182 条）
- [ ] 启动日志显示正确 `[Build] 版本-渠道 (SHA)`
- [ ] 手动跑一局：主菜单 → 选模式/地图/阵营 → 购买 → 交战 → 结算 → 返回
- [ ] 存档：设置改动后重启仍在；`cs2d_save.ini` 位置正确
- [ ] 开镜/死斗/购买等最近改动手动过一遍（见 docs/handoff-windows-ai.md 未验证项）

### 与基线对比（BOT/平衡改动必做）

```powershell
powershell -ExecutionPolicy Bypass -File tools\package.ps1 -Version 0.3.0 -Channel release
```

打包脚本会：构建 → 跑 182 条测试 → 跑 60 局基准并与 `tools/dev_replay/baseline/summary.csv` 对比 →
有告警时要求确认 → 打 zip。详细判读见 `tools/dev_replay/WORKFLOW.md`。

- [ ] 基准对比无告警，或告警原因已确认并在 PR 里写明
- [ ] 若行为是**预期变化**：已按 WORKFLOW 第 6 节刷新基线（新 commit 重跑 60 局）

### macOS（shijin）

- [ ] 同一 commit 在 macOS 构建通过
- [ ] 主菜单/对局/小鸡/开镜可用（PIX-11 清单）
- [ ] 记录 macOS 版本、架构、编译命令

---

## 2. 打 tag 与产物

```powershell
# release
git tag -a v0.3.0 -m "PixelEngine 0.3.0"
git push origin v0.3.0

# beta（同一个 commit 也可以）
git tag -a v0.4.0-beta.1 -m "PixelEngine 0.4.0-beta.1"
git push origin v0.4.0-beta.1
```

- [ ] tag 指向的 commit 与打包用的 SHA 一致
- [ ] `dist\PixelEngine-0.3.0-release-<sha>.zip` 已生成，`version.txt` 里的 SHA 对得上
- [ ] zip 不含 `build-*` / `build-evidence` / 存档 / 日志

---

## 3. 发布说明（写进 GitHub Release / 给玩家）

```markdown
## PixelEngine 0.3.0

**改动**
- ...

**修复**
- ...

**已知问题**
- ...

SHA: <commit>
构建: MSVC/MinGW + CMake，Windows x64；macOS 由 shijin 验证
```

- [ ] 改动列表来自 `git log v上版本..v本版本 --oneline`（挑用户可感知的）
- [ ] 已知问题照实写（不要把未验证项写成已验证）

---

## 4. 发布后

- [ ] 用 zip 里的 exe 在**干净目录**启动一次（验证资源相对路径）
- [ ] 检查首次启动生成 `cs2d_save.ini`（不应读到开发机残留）
- [ ] 如有线上问题，记录到 Linear 并关联本版本 tag

---

## 5. 常见错误

| 错误 | 后果 | 避免方式 |
| --- | --- | --- |
| 用 `build-dev` 的 exe 当发布包 | 带 `--killplayer`/`--record`，标题显示 dev | 只用 `build-release` 的产物 |
| 忘记改 `PE_VERSION` | 两个版本标题一样，bug 报告无法定位 | 打包前先 `git log -1` 核对 |
| 基线没刷新就发布 | 下次对比全是噪声 | 确认行为变化是预期后立刻刷基线 |
| beta 用 dev 渠道构建 | 存档后缀与玩家档混乱 | beta 用 `PE_CHANNEL=beta` |
| 打包目录里混入 `build-evidence` | zip 巨大 | 打包脚本只收 `EngineApp.exe + assets + README + version.txt` |
