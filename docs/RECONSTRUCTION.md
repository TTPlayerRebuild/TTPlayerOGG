# ttp_ogg 实现与原版对照

日期：2026-10-03。本文件记录首阶段的 Vorbis 原版恢复。随后用户扩展范围，加入 Opus 和封面；当前实现见 [Opus 与封面扩展](OPUS_COVER_IMPLEMENTATION.md)。

## 参照与重建方法

主要参照 `AddIn/ttp_ogg.dll`，SHA-256：
`ee58d0603a5c4c7596219590d5e92e12748187b395e14dc1275256e0904dcdc5`。
其内置 libVorbis 1.3.3，依赖 MSVCR110.dll，导出表模块名为 dec_ogg.dll。

同时核对 `TTPlayer5719/AddIn/ttp_ogg.dll`，SHA-256：
`f04295825000c93370c195f6cfaf576d1b52ad3eddb8dbc8c41cf5a5de0f4174`。
两版 ABI 一致，但错误码、网络定位政策、通用标签后备及解码库实现并非全部相同。
本项目以当前 AddIn 版的公开行为为主要目标，不能同时复制两版相互不同的细节。

伪代码用于恢复控制流，二进制用于确认 GUID、虚表、资源和真实指令；原 DLL 用于行为对照。
解码算法使用 Xiph libvorbis 1.3.7，不把反编译出的旧算法与编译器运行库逐行粘入工程。
不声称还原了厂商工程或所有私有实现，也不把未覆盖输入的音频结果视为已证明等价。

## 接口映射

| 原当前版地址 | 恢复行为 | 源码位置 |
| --- | --- | --- |
| `605542FD`、`60554095` | ttpGetSoundAddIn；只枚举一个 ReaderCreator | `src/plugin.cpp` |
| `605540D2`、`605540F7` | OGG Reader；Vorbis/Ogg 音频文件(*.ogg) | `src/plugin.cpp` |
| `605527F4` | IUnknown/Reader/Metadata 身份与引用计数 | `src/reader.cpp` |
| `6055268D` | IStream 回调打开，时长、格式、注释 | `src/reader.cpp` |
| `60552873` | 扩展流 83E2BBBF... 的槽 14，事件和 60 秒参数 | `src/reader.cpp` |
| `60552F8B`、`60553325` | 1717A4A7... 槽 4 回调转发 | `src/reader.cpp` |
| `605521B6`、`60552125` | URL、可定位、可编辑能力位 | `src/reader.cpp` |
| `6055299A` | 分声道 float 转交错 double；六声道 0/2/1/5/3/4 | `src/reader.cpp` |
| `60552EFA` | 毫秒转秒定位 | `src/reader.cpp` |
| `60551FE8`、`605522BD`、`605522E8` | 时长、码率、固定 BitDepth=16 | `src/reader.cpp` |
| `60552B41`、`6055200A`、`60552FF4` | UTF-8 注释、首个同名字段查找与更新 | `src/reader.cpp` |
| `60551CF5` | 无 Vorbis 注释时的 CreateStdContent 后备 | `src/reader.cpp` |
| `60551491`、`60553619` | 最终释放时保存、必要时重开可写流 | `src/reader.cpp`、`src/comments.cpp` |
| `605559D2`、`60555C17` | 更新 Vorbis 注释且不重编码音频 | `src/comments.cpp` 的独立页编辑实现 |

原公开 Reader 共有 16 槽（含 IUnknown 三槽），Metadata 共七槽。
字符串和格式用 CoTaskMemAlloc 分配；不跨 DLL 使用不匹配的 C++ 容器所有权。
Start/Stop 保留原成功空操作，定位不改写调用方毫秒参数。
原 Thumbnail、Encoder、独立 Decoder 接口不存在，因此不声明。

## PCM 与链式流

- WAVE_FORMAT_IEEE_FLOAT，64-bit，blockAlign=channels×8，推荐缓冲为 1024 帧。
- BitDepth() 返回 16 是旧信息接口行为，实际解码缓存以 Format() 为准。
- 只对六声道做原版重排，其他声道保留原直接交错规则；并未擅自更改三/五/七/八声道布局。
- 同格式 chained stream 可连续读取与跨段定位。
- 采样率/声道数变化继续返回 E_NOTIMPL，不新增重采样或混音。

## 标签保存

原模块采用 Vorbis 专用的 vcedit 式重组；本项目的保存代码未采用 vcedit 源码。
新实现根据 Ogg lacing 替换首个选中 Vorbis 逻辑流的注释包：

1. 生成 vendor、UTF-8 注释数量/长度及 framing bit。
2. 验证输入页 CRC、目标序列号、连续页号和包延续标志。
3. 注释可增大或缩小，跨页时生成合法 lacing 与 continued 标志。
4. 保留音频包字节、音频 granule position、其他流和后续链；仅调整需要改变的页号和 CRC。
5. 全部生成成功后备份原文件，再复制回可写宿主流，检查实际写入长度、SetSize 和 Commit。
6. 失败时从备份恢复；回滚也失败则保留两份临时文件并通过 OutputDebugString 报告备份路径。

同名标签保留顺序，Set 更新第一个匹配值，空值在保存时过滤，与原行为一致。
未知字段及图片相关原始注释得以保留，但不因此声明封面接口。
修改未触及的 vendor/原始注释字节优先保留。

## 有意修正及保留的边界

| 项目 | 处理 |
| --- | --- |
| 内部错误被外层吞掉成为 EOF | 交付已有有效 PCM，下一次 Read 明确返回错误 |
| 反复 OV_HOLE | 限制连续恢复次数，避免忙循环 |
| 小于一帧的容量 | 返回不足缓冲错误；不返回假 EOF |
| 非帧对齐容量 | 向下取完整帧，避免越界 |
| Unicode 临时文件路径 | 使用宽字符 API |
| 只读打开但物理文件可写 | 最终释放前关掉自有读句柄，然后重新打开保存 |
| 短写 | 检测并回滚，不将未写完视为保存成功 |
| 无法定位的网络流 | 时长未知时返回 0，避免负秒数转成巨大无符号时长 |
| 非法空键、包含等号的键 | Set 拒绝，防止生成含义不明确的注释 |

保存仍受旧 ABI 限制：宿主没有 CommitMetadata 返回值槽，最终 Release 中的保存错误不能通过原返回值回传；诊断使用 OutputDebugString。
原位写回加备份不等于断电原子事务，写回中进程强制终止仍可能需要从临时备份恢复。
单注释包限制 64 MiB，前置标签扫描限制 16 MiB。Vorbisfile 的 Windows tell 回调为 signed long，超过 2 GiB 的流定位不列入已验证范围。
不承诺解码库升级后所有异常输入和所有文件的浮点样本均与 libVorbis 1.3.3 相同。

## 构建与发布

项目独立使用 MSVC x86、VC-LTL、YY-Thunks；仅从固定且带哈希的上游版本下载依赖。
日期资源与 ZIP 版本在编译前统一确定；`Release a Version` 在同日追加 pN。
发行 ZIP 不包含测试、伪代码、旧 DLL、宿主 EXE 或下载的源码目录。
未经用户进一步要求，本轮不发布 GitHub Release、不替换现用播放器插件。
