# Ogg 顺序流、取消恢复与位置边界

日期：2026-10-24。此记录接续 `COMPLETION_20261024.md`，属于后续源码修复与本地测试，不扩大前一轮发行包的验收范围。

## 复现的问题

使用内存 `IStream` 包装既有 Vorbis／Opus 样本，不建立巨大文件、不连接实际服务器，重新测试了上轮 `2026.10.24` 发行 DLL：

1. `Seek(CUR)` 可调用，但 `Stat` 不提供长度时，Opus 被误作可定位文件，1237 ms 的样本只输出 833 ms，PCM 从 2,850,048 字节减为 1,920,384 字节。
2. `Seek` 可用但长度报告 0 时，Vorbis 开流超过测试超时；Opus 开流失败。
3. 非定位流在 `Seek` 返回错误时若改写输出位置，插件错误地采纳该值，导致原本可顺序播放的流打开失败。
4. `ERROR_CANCELLED`／`E_ABORT` 后错误被永久锁存，`Start()` 无法继续。底层已经消耗部分字节时，直接清除错误还会丢失这些字节。

原版同名格式的正常顺序播放能力予以保留；以上问题来自重建插件的回调适配，不能当成原版伪代码本身的缺陷。

## 修复

- 只有长度已知、非零、可表示为有符号 64 位位置且 `Seek(CUR)` 成功，才向解码库提供定位能力。
- 长度未知或 0 的流仍然顺序解码，时长报告 0，不声明可定位；主动调用 `Seek` 返回 `E_NOTIMPL`。
- 不读取失败 Seek 的输出参数。已知位置／长度超过 `INT64_MAX` 明确拒绝；读取加法越界返回读取错误。
- 取消时保留底层已经返回、但尚未送入解码库的字节。`Start` 仅清除取消状态，先重放该小段字节再继续读取；不必回到文件开头，也适用于不可定位流。
- 硬读取错误保持失败，`Start` 不将损坏或设备错误伪装成正常结束。
- 不在不可定位的无标签流上调用可能消费数据的宿主 `CreateStdContent` 后备解析器；可定位流仍调用并恢复位置。
- 保留上轮独立维护实际读取位置的方式，宿主 Seek 输出始终为 0 的兼容性未回退。
- 定位期间的底层 Seek／Read 错误保留原 HRESULT。失败后重开解码器并回到此前已交付的 PCM 位置；如果回滚也失败，Read 锁存错误，Start 不会把不确定的位置当作有效，后续一次成功的显式 Seek 才恢复读取。

Vorbis 在部分定位失败后内部解码状态已经无效，直接再次调用 `ov_pcm_seek` 不足以恢复，因此采用上述重开路径。正常定位仍沿用原来的解码器定位，不增加每次定位的重开成本。回滚结果与另一个 Reader 正常定位到同一样本位置逐字节比较；这不等同于要求 Opus 的定位后 PCM 与未定位的连续历史状态绝对相同。

回调行为依据 [opusfile 的 OpusFileCallbacks](https://opus-codec.org/docs/opusfile_api-0.12/structOpusFileCallbacks.html) 与 [Vorbis 的 ov_open_callbacks](https://xiph.org/vorbis/doc/vorbisfile/ov_open_callbacks.html)。两者支持把识别时已经读取的前缀交给解码器，定位能力由回调明确声明。

## 验证

私有目录：`rebuild/tests/plugins_followup_20261024/ogg-stream`。没有新增 Actions 测试，也没有将测试源码加入插件仓库。

- 50 项流矩阵通过：两种编码各 25 项，涵盖 1／31／4096 字节短读、失败 Seek 输出污染、未知／零／超大长度、接近有符号 64 位上限的读取、输出恒零的 Seek、两种取消 HRESULT、取消前已消耗 0／13 字节、连续两次取消、硬读取错误。
- 所有正常和取消恢复用例的完整 PCM 均逐字节等于普通可定位流基线。异常用例按预期 HRESULT 与退出结果验收，不计作成功播放。
- 另 6 项无标签流测试通过：后备标签解析器故意读取 17 字节。可定位流调用后正确恢复；不可定位流不调用，音频无损失。
- 24 项定位失败测试通过：两种编码各 12 项；覆盖 E_ABORT、ERROR_CANCELLED、硬错误、失败前已移动游标、定位中的部分读取，以及回滚也失败后再次显式 Seek 恢复。
- 内存流只存储短样本，64 位边界由虚拟 `Stat`／`Seek` 返回值模拟，没有写入巨大占位文件。
- Release 构建及 XP／Win7 静态导入检查通过（4 个系统 DLL、100 个导入）。最终 `2026.10.24p1` DLL 的 89 项矩阵已在 XP SP3、Win7 SP1 和 Windows 11 全部通过；原版及重建版的 Vorbis／Opus 实际播放也在三系统通过。

```powershell
cmake --build ttp_ogg/build --config Release --target ttp_ogg --parallel 4
python rebuild/tests/plugins_followup_20261024/ogg-stream/build.py
python rebuild/tests/plugins_followup_20261024/ogg-stream/run.py after.json
python rebuild/tests/plugins_followup_20261024/ogg-stream/fallback.py
python rebuild/tests/plugins_followup_20261024/ogg-stream/build.py seek
python rebuild/tests/plugins_followup_20261024/ogg-stream/seek_test.py
python rebuild/tests/plugins_followup_20261024/ogg-stream/channels.py
```

实际使用的 vorbisfile `lib/vorbisfile.c` 的 `_get_data` 固定调用 `read_func(buffer, 1, READSIZE, ...)`，opusfile 的桥接函数也固定 `size=1`。因此本轮短读和取消重放不涉及 `fread` 的多字节元素余数丢失；此结论针对锁定版本的实际调用路径。

## Vorbis 多声道排列

同轮审阅发现上轮 Format 为所有标准 Vorbis 布局提供 Windows 掩码，但 Read 仅重排 6 声道，3／5／7／8 声道的 PCM 顺序不匹配声明。已依据 [Vorbis I §4.3.9](https://xiph.org/vorbis/doc/Vorbis_I_spec.html) 为 Vorbis 与 Opus 共用 Windows 排列表：

| 声道数 | 输出通道对应的 Vorbis 输入序号（从 0 开始） | Windows mask |
| --- | --- | --- |
| 3 | 0, 2, 1 | 0x7 |
| 5 | 0, 2, 1, 3, 4 | 0x37 |
| 6 | 0, 2, 1, 5, 3, 4 | 0x3f |
| 7 | 0, 2, 1, 6, 5, 3, 4 | 0x70f |
| 8 | 0, 2, 1, 7, 5, 6, 3, 4 | 0x63f |

1／2／4／6 声道保持上轮 PCM 逐字节不变。8 种各声道使用不同频率的样本与 FFmpeg 独立解码全帧对比，样本数相同，最大绝对误差为 `1.1920929e-7`。另生成 9 声道样本，验证保持应用定义的原顺序且 mask 为 0，不猜测扬声器布局。

私有 `prepare_vm.py` 生成平铺 `vmstage`、manifest 和 `run.cmd`，总计 **89 项**：50 流边界、6 标签后备解析、24 定位回滚和 9 声道排列。合法输出与期望 PCM 用 `fc /b` 比较，异常项核对退出码和 HRESULT。

## 未覆盖

2026-10-25 更新：真实 HTTP 的 Range、续传、未知长度、取消和超时已在三系统补测，详见 [HTTP 验证](HTTP_VALIDATION_20261025.md)。下文保留本页原轮次的覆盖边界。

这是标准 `IStream` 的确定性故障注入测试。HTTP 重连、服务器 Range 行为、网络等待时序、`E_PENDING` 在每次 Read 中的异步协议，以及开流过程中取消的恢复，仍需独立测试。链间声道或采样率变化、未知 Opus mapping family 的原有拒绝策略没有改变。
