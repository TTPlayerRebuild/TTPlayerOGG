# Vorbis 重建验证记录

日期：2026-10-03。

本文保留 `2026.10.03` 的 Vorbis-only 基线。`2026.10.03p1` 的 Opus、封面、依赖版本及三系统复测见 [当前验证报告](OPUS_COVER_IMPLEMENTATION.md)。

## 本地 DLL 对照

参照 DLL 为 `RECONSTRUCTION.md` 指定的当前 AddIn 版本。
测试程序直接枚举真实插件，使用宿主 ABI 读取 PCM；未用系统解码器替代插件。

| 样本 | 原 DLL 与重建 DLL 的 PCM |
| --- | --- |
| 8 kHz / 单声道 | 逐字节一致 |
| 22.05 kHz / 单声道 | 逐字节一致 |
| 44.1 kHz / 双声道 | 逐字节一致 |
| 48 kHz / 双声道 | 逐字节一致 |
| 96 kHz / 双声道 | 逐字节一致 |
| 44.1 kHz / 三、四、五声道 | 各自逐字节一致 |
| 48 kHz / 六、七、八声道 | 各自逐字节一致 |

共 11 组本地生成的 1.6 秒 Vorbis 样本，每声道使用不同频率，覆盖原六声道重排。
测试计算了 double PCM 的有限性、RMS 和峰值，不使用旧测试工具只支持 float32 的 RMS 列作判断。
这证明本轮样本的一致性，不代表所有文件、编码器或异常输入都已穷尽。

其他通过项：

- 起始、中间、末尾附近定位；同格式链式读取及跨链定位。
- 变采样率链保留首段所有 PCM，随后明确返回 `E_NOTIMPL`，不再伪装 EOF。
- 非帧对齐缓存、过小缓存、空参数、重复打开及范围外定位。
- 不可定位流、每次最多 31 字节的分段读取、宿主扩展读取及回调转发。
- 注入读取失败后返回 `STG_E_READFAULT`。
- Unicode 标题、AlbumArtist、Genre、ReplayGain、自定义标签写入/删除/重读。
- 最终 Release 时保存；只读方式打开但文件可写时重新打开保存。
- 无 Vorbis 注释时使用宿主 `CreateStdContent` 后备，恢复调用前流位置；Set 转发。
- 大标题 200,000 个汉字，以及反复增大/缩小标签；注释跨 Ogg 页。
- 原始音频包保持不变，编辑后 PCM 与编辑前逐字节相同。
- 后续 chained stream、前置 ID3 及尾部标签保留。
- 模拟短写，回滚后整个文件与修改前逐字节一致。
- Opus 内容使用 `.ogg` 后缀仍被拒绝，符合本轮 Vorbis-only 范围。

证据保存在本地 `rebuild/tests/ogg_rebuild/artifacts/results.json`（67 条记录，包含样本生成和对照）、`host-results.json` 及 PCM/日志文件中。
播放器 PluginManager 读取与元数据路径另经现有 aac_validation 通用测试程序验证。

## 实际播放器与虚拟机

隔离目录只安装新 `ttp_ogg.dll` 输入插件，核对真实加载模块路径；播放 30 秒 Vorbis 样本，退出后检查保存的播放进度。

| 系统 | 原版播放器 | 重建版播放器 |
| --- | --- | --- |
| 本机 Windows 11 | 正确加载；播放进度超过 3 秒；正常退出 | 正确加载；播放进度超过 3 秒；正常退出 |
| Windows XP SP3 x86 | 正确加载；最终包约 2.57 秒；正常退出 | 正确加载；最终包约 3.59 秒；正常退出 |
| Windows 7 SP1 | 正确加载；最终包约 2.79 秒；正常退出 | 正确加载；最终包约 3.45 秒；正常退出 |

集成探针确认主窗口存在、未出现错误对话框、使用预期 DLL、无需强制结束。
原版自身退出码为 1，重建版为 0；探针结合上述证据将两者判定通过。
本机探针未经版本感知 manifest，GetVersionEx 会显示 6.2；不能据该行把本机误记为 Windows 8。
虚拟机经 Guest Control 启动测试；这不是桌面外观、交互或实际扬声器听感验收。

XP 与 Win7 还分别完成：直接加载/契约、网络分段流、六声道解码、链式文件、20 万汉字标签保存、重读解码及短写回滚。
编辑结果复制回主机后以独立 ffprobe 核对标题内容/长度，再检查音频包未变。
证据为本地 `vm-results.json` 与 `vm-tag-results.json`。
最终发行 DLL 再次完成本机 PCM 对照、两宿主播放及 XP/Win7 批量/两宿主播放；对应最终摘要记录在 `final-host-results.json` 与 `final-vm-results.json`。

## 构建及发行核验

- MSVC 19.51、Win32 Release，libogg 1.3.6、libvorbis 1.3.7。
- x86、PE 子系统最低版本 5.01。
- 最终静态依赖仅 `kernel32.dll`、`msvcrt.dll`、`ole32.dll`、`shlwapi.dll`。
- 85 个静态导入同时通过 XP 与 Win7 清单检查；无 MSVCR110、VCRUNTIME、UCRT 或额外 Xiph DLL 依赖。
- 唯一命名导出为 `ttpGetSoundAddIn`，序号 1。
- 文件版本、产品版本、ZIP 版本均为 `2026.10.03`。
- DLL 大小 213,504 字节；ZIP 大小 131,911 字节。
- DLL SHA-256：`1fb82f0ea03edd9159ed7b40b9ef7c08fb9245054b65fd6b5ca518aa51260ddc`。
- ZIP 核验仅有 `AddIn/ttp_ogg.dll` 与 `SHA256SUMS.txt`，内置与外置摘要一致。
- DLL 的四份第三方许可证资源逐字节核对仓库原文。

未执行 GitHub Actions 远程运行或发布；本地已执行同一个独立 build.ps1 的构建与打包路径。
未单独运行 Windows 10；Windows 11、XP 和 Win7 的实际结果不能替代该系统的专门验收。
尚未覆盖超过 2 GiB 文件、断电中断、所有网络宿主实现及全部损坏输入。
