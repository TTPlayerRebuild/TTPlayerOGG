# Vorbis、Opus 与封面实现及验证

日期：2026-10-03。发行版本：`2026.10.03p1`。

## 完成范围

1. 保留原 Vorbis 播放、时间定位、标签读取与最终 Release 保存行为，重新运行原 DLL 的 PCM 对照。
2. 两种编码均支持标签修改、删除和保存；保留压缩音频、时间戳及非目标逻辑流，写入失败执行备份回滚。
3. 新增 Ogg Opus 解码，处理有效时长、首尾裁剪、头部增益、R128 查询适配与 1～8 声道顺序。
4. 两种编码均提供原播放器已有的 Thumbnail 接口，支持内嵌封面读写。
5. 验证原版和重建版，以及 XP、Win7、本机 Windows 11。**按用户要求，Windows 11 作为 Windows 10 的替代验证环境；未执行原生 Windows 10 测试。**

Vorbis 对照基线见 [原版分析](RECONSTRUCTION.md) 和 [首阶段验证](VALIDATION.md)。Opus、封面是新增能力，不声称原 Ogg DLL 已提供这些功能。

## 一、依赖核对与独立构建

| 库 | 最新稳定版 | 本次处理 |
| --- | --- | --- |
| libogg | 1.3.6 | 现有版本已为最新，保留 |
| libvorbis / vorbisfile | 1.3.7 | 现有版本已为最新，保留 |
| libopus | 1.6.1 | 新增 |
| opusfile | 0.12 | 新增 |
| VC-LTL | 5.3.1 | 现有版本已为最新，保留 |
| YY-Thunks | 1.2.2 | 现有版本已为最新，保留 |

版本依据：[Xiph 官方下载](https://xiph.org/downloads/)、[Opus 官方下载](https://opus-codec.org/downloads/)、[VC-LTL 发布](https://github.com/Chuyu-Team/VC-LTL5/releases/latest)、[YY-Thunks 发布](https://github.com/Chuyu-Team/YY-Thunks/releases/latest)。下载地址、SHA-256 与许可证见 [第三方来源](../third_party/ORIGIN.md)。

上游源码只下载到忽略的构建缓存；仓库保留构建描述和版权文本。Opus 使用官方 CMake 静态目标，关闭测试、程序、DRED、OSCE、额外 SIMD 分派；保留标准浮点解码及生产安全检查。使用 `/O1 /Os`、LTO、SSE2 基线；链接时移除未使用的编码器。

opusfile 0.12 没有 CMake 工程，因此按其 Makefile 的 `info.c / internal.c / opusfile.c / stream.c` 建立静态库。网络读取继续由宿主 `IStream` 提供，不新增 opusurl/OpenSSL 依赖。

`ttp_ogg` 单独构建、打包，与 `ttp_aac` 和播放器源码没有构建依赖。Actions 只构建、检查导入和打包，不运行或上传本地测试。

## 二、解码、定位、裁剪与增益

### 真实内容识别

同一个 `OGG Reader` 工厂声明 `*.ogg;*.oga;*.opus`。`Open` 使用 libogg 校验 Ogg 页并识别 BOS 中的 Vorbis identification 或 `OpusHead`，而非依赖扩展名或任意字符串命中。前读字节传入实际解码器，非 seekable 流也不会丢失开头。

保留宿主扩展读取接口的 slot 14、60 秒超时和回调转发。短读可以继续；真实 I/O 错误返回 HRESULT，不当作正常 EOF。无法识别的输入返回原格式不支持错误码。

### Opus 输出与时间

- 使用 `op_open_callbacks / op_read_float / op_pcm_seek`；统一转成宿主兼容的 IEEE float64 PCM。
- Opus 输出固定 **48,000 Hz**；OpusHead 的 input sample rate 不作为输出采样率。
- `op_pcm_total` 已扣除 pre-skip 和尾部裁剪；时长为有效帧数除以 48 后向下取整到毫秒。
- 毫秒定位目标乘以 48，再交给官方库处理 pre-roll 和精确样本位置。
- 保留完整 PCM 帧；过小缓存报错，非对齐容量向下取整。发生后续错误时，先交付已经产生的 PCM，再返回错误。
- 同格式 chained Opus 可连续播放并跨段定位；声道数发生变化时明确报 `E_NOTIMPL`，不把它伪装为 EOF。

定位之后的解码状态需要收敛，故不强求与从头连续解码的所有样本逐字节相同。本次另用独立 stdio 回调的官方 opusfile 参考程序核对定位后的 PCM 和帧数。依据：[opusfile 定位文档](https://opus-codec.org/docs/opusfile_api-0.12/group__stream__seeking.html)。

### 声道

只接收播放器可解释的 mapping family 0/1；family 255 等无明确扬声器含义的布局明确拒绝。Opus family 1 从 Vorbis 顺序重排为 Windows 常用顺序，索引从 0 开始：

| 声道数 | 输出取源索引 |
| --- | --- |
| 1 / 2 | 原顺序 |
| 3 | 0, 2, 1 |
| 4 | 0, 1, 2, 3 |
| 5 | 0, 2, 1, 3, 4 |
| 6 | 0, 2, 1, 5, 3, 4 |
| 7 | 0, 2, 1, 6, 5, 3, 4 |
| 8 | 0, 2, 1, 7, 5, 6, 3, 4 |

Vorbis 路径继续采用原插件仅对六声道重排的行为，避免改变现有 Vorbis 对照结果。

### 增益

`op_set_gain_offset(OP_HEADER_GAIN, 0)` 明确只应用 Opus 头部增益。R128 不在解码器中额外应用，以免叠加宿主“自动调整音量”的处理。依据：[官方增益 API](https://opus-codec.org/docs/opusfile_api-0.12/group__stream__decoding.html)。

若没有实际的 `replaygain_track_gain / replaygain_album_gain`，对应 `Get` 查询可由 R128 Q7.8 字段提供：`R128 / 256 + 5 dB`，将 -23 LUFS 参考转换为宿主 ReplayGain 的 -18 LUFS 参考。头部增益已包含在 PCM 内，不再加第二次。真实 ReplayGain 字段优先；R128 原文保持原样，查询适配值不自动写回文件。

## 三、标签与可靠保存

两种编码共用原 Metadata ABI：UTF-8 字段、大小写不敏感查询、保留未知和重复字段；修改首个同名值，空值在序列化时删除。

Vorbis comment packet 保留 `03 vorbis` 头和 framing bit。OpusTags 使用自己的签名，不添加 Vorbis framing bit；通过 opusfile 保留要求保留的二进制 suffix。修正 comment packet 最后一个字节的 64 MiB 上限检查。

编辑器只替换目标逻辑流的第二个包，重新组织注释页、页序号、continuation 和 CRC；不重新编码音频，也不重新计算音频 granule position。chained 文件只编辑第一个选中逻辑流的元数据，后续流保持原样。

保存顺序：生成完整替换文件 → 完整备份原文件并 Commit → 写回、调整长度、Commit。写回失败恢复备份；回滚也失败则保留恢复文件并输出其路径。原 ABI 在最后一个 COM 引用释放时保存，没有可向界面返回提交 HRESULT 的独立方法，因此保存失败仍通过调试日志报告。

这一机制可处理测试覆盖的短写错误，**不保证进程终止或断电时的原子替换**。另一进程禁止写入时会保留原文件并报告失败。只读属性和非 seekable 网络流不声明可编辑能力。

## 四、封面

新增原播放器已有的 `B5E770AF-DFB0-43E5-9B0C-3EE98E7B6248` Thumbnail 接口。结构布局和方法顺序与原 FLAC 插件一致；`60301CB4` 的修改掩码仍为 MIME=1、描述=2、类型=4、数据=8。Reader 能力位增加封面写入标志 16。

- `METADATA_BLOCK_PICTURE` 使用标准 FLAC picture block + Base64；通过官方 opusfile 图片解析器读取，两种编码共用。
- 新写入图片识别真实 PNG/JPEG/GIF 内容，写入 MIME、UTF-8 描述、类型和图像尺寸。测试主要覆盖 PNG、JPEG。
- 读取旧 `COVERART / COVERARTMIME`；编辑图片后写回标准 picture block。
- 可添加、替换、删除多图；读取时将正面封面排在第一项，适应两个宿主的 index 0 播放封面查询。
- 每张图片最大 16 MiB，最多添加 64 张；全部注释包仍受 64 MiB 限制，不能将两个上限相乘理解为允许 1 GiB 标签。
- 每个条目拥有独立返回描述符；读取另一项不会改掉已返回的描述符。修改标签或封面后，调用方需重新取得指针。
- 损坏 Base64 不妨碍播放；URL 类型封面不作为内嵌图返回，也不发起联网请求。原字段可继续保留。

无效新图片和超限修改在改变内存状态前被拒绝。图片和文字标签一起进入最终 Release 的保存及回滚流程。

## 五、验证记录

所有测试代码、样本、原 DLL 证据和日志均留在工作区 `rebuild/tests/ogg_rebuild`；没有加入插件发行源码或 Actions。

### 本机算法与接口

- **67 条 Vorbis 回归记录**：11 组 8～96 kHz、1～8 声道样本，新旧 DLL PCM 逐字节相同；包括原版六声道排列。
- **94 条 Opus/封面记录**（包含生成与参考命令）：10 组 8/16/48 kHz 输入、voip/audio/lowdelay、1～8 声道；所有有效输出均为 59,376 帧、1,237 ms。与 FFmpeg/libopus 参考输出最大绝对差不超过 `1.529e-5`。
- 0、1、555、1220、1236 ms 定位与独立 opusfile 参考程序输出逐字节一致，尾部帧数准确。
- 正负 6 dB 头部增益；R128 查询转换；验证 R128 本身不使 PCM 再放大。
- 每次 31 字节非 seekable 流、单帧容量、非对齐容量、越界定位、I/O 错误。
- 同格式链式流、变化格式拒绝、family 255 拒绝。
- 20 万汉字标签、跨页增大/缩小、OpusTags suffix 保留、只读方式打开后保存、删除和短写回滚。
- 两种编码的 PNG/JPEG 添加、读取、描述修改、删除；正面优先、图片字节一致；旧 COVERART、标准字段、损坏 Base64。
- 重建版真实 PluginManager 完成两种编码的多字段保存、封面替换、关闭重开、再删除验证。

主要证据：`artifacts/results.json`、`opus-cover-results.json`、`opus-final-validation.json`。

### 实际宿主与旧系统

每个测试宿主使用隔离目录，只安装新 Ogg 输入插件，播放 30 秒样本；验证实际 DLL 路径、主窗口、无错误对话框、播放进度和正常退出。未替换用户现用 AddIn。

| 系统 | 原版播放器 | 重建版播放器 |
| --- | --- | --- |
| Windows XP SP3 x86 | Opus 播放进度 2,825 ms；正常退出 | 3,180 ms；正常退出 |
| Windows 7 SP1 | Opus 播放进度 3,581 ms；正常退出 | 3,250 ms；正常退出 |
| Windows 11，build 26200 | 最终包 Vorbis、Opus 均正常加载并推进播放 | 最终包 Vorbis、Opus 均正常加载并推进播放 |
| Windows 10 验证项 | 按用户要求采用上述 Win11 替代验证 | 同左；不是 Win10 原生实测 |

XP、Win7 的直接 DLL 批量检查分别 `failures=0`：接口契约、非 seekable 分段读取、八声道 PCM、链式文件、头部/R128 增益，以及两种编码的 20 万字标签、封面保存、关闭重读、短写回滚。

从虚拟机复制回的结果再次独立验证：标签为完整的 200,000 个汉字；PNG/JPEG 字节完全一致；音频包未改变；八声道 PCM 与本机相同；回滚文件与原文件逐字节相同。最终发行 DLL 的证据为 `opus-distribution-vm-results.json`、`vm-opus-final-XP/`、`vm-opus-final-Win7/` 和最终验证 JSON。传出较小的新文件时，工具覆盖已有大文件曾保留旧尾部，因此最终证据使用全新本地目录接收，并重新核对 DLL SHA-256。

这些检查覆盖解码、元数据、宿主加载和播放进度，未作为每个封面界面的像素比较或实际扬声器听感验收。原版正常退出码为 1、重建版为 0，结合窗口/进度和无强制结束判定。旧测试探针的 GetVersionEx 在本机返回 6.2，实际系统由本机平台信息确认是 Windows 11。

## 六、发行核验与剩余边界

- `build/Release/ttp_ogg-2026.10.03p1.zip`，只含 `AddIn/ttp_ogg.dll` 和 `SHA256SUMS.txt`。
- DLL 340,480 字节；ZIP 217,667 字节。
- DLL SHA-256：`af6ebd6cf22334f4335946b31a1bb125f82feaeeb686110233c9359bef442091`。
- ZIP SHA-256：`4d060586a6cf9b35a372b5d65dad5724aa9308392e01ffd9faf2d81c5da28dab`。
- 六份许可证保留在仓库，发行说明提供对应链接；按用户要求移除 DLL 中的许可证 RCDATA。已检查资源 32001–32006 不存在。文件版本、日期补丁版本、打包校验和发行说明脚本通过本地验证。
- x86 / PE 子系统 5.01；100 个静态导入同时通过 XP、Win7 清单，仅依赖 kernel32、msvcrt、ole32、shlwapi。无需另装 VC2012、UCRT 或 Xiph DLL。
- 未运行 GitHub Actions 发布。

当前范围为 Ogg 容器内的 Vorbis/Opus，未增加 MP4/WebM Opus、编码器、无定义的声道布局或跨链格式自动转换。超过 2 GiB 文件、断电以及所有网络宿主/损坏输入未穷尽测试。所有 Vorbis 输入都与旧库逐字节相同也未经穷尽证明。
