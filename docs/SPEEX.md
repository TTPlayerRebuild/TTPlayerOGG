# Ogg Speex / SPX

`.spx` 接入原 Ogg Reader，与 Vorbis / Opus 以 BOS 识别包分流，不按文件名猜测。
使用固定 Speex 1.2.1 浮点核心，支持 NB/WB/UWB、单声道/立体声、每包多帧及额外头包。
严格验证原始 header 声道数、版本、模式、帧长、frames_per_packet 和分配上限，不依赖上游会钳制声道数的宽松解析。

输出沿用 Ogg Reader 的 IEEE float64 接口；核心结果是 float32，不是双精度 CELP 解码。Speex 的约 ±32768 幅度除以 32768 后无损扩宽，不削波、不先转 16-bit 整数。
按 Ogg granule 时间线识别前导延迟和 EOS 尾裁剪；单音频页没有中间 granule 锚点时，使用模式的标准编码 lookahead。
最终帧数以实际文件的 EOS granule 为准，不能假定外部编码器保存了输入 WAV 的精确长度。

本地 Seek 从 BOS 重建完整 CELP/立体声状态后丢弃至目标，优先保证与顺序解码一致；长文件定位可能较慢。
非定位、短读 IStream 支持顺序解码；缺失 EOS、损坏页/包返回错误，不用 PLC 静音掩盖损坏。
当前仅处理一个选定的 Speex logical stream；可定位文件遇到 chained Speex 明确拒绝，而不是只报第一段时长。

Speex 注释包不带 Vorbis 的类型/签名/帧标志；独立序列化并保留未知尾部，沿用现有保留音频 payload/granule 的 Ogg 注释重写、封面及终止式 Commit。
Speex 注释包限制为 16 MiB，修改时先验证总序列化大小；不会写出本 Reader 自己无法读回的过大标签。图片上限同时预留 base64 与包头开销。
许可与固定来源：`third_party/speex/`。私有测试入口：`rebuild/tests/audio_format_extensions_20261111`。
