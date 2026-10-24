# ttp_ogg 后续补全与验收

日期：2026-10-24。发行版本：`2026.10.24`。

## 修改

多声道公布与 Windows 排列一致的掩码。读取改为标准 IStream，并按实际读取数及绝对定位维护 64 位位置，避免旧宿主 Seek 输出恒为 0 导致 opusfile 返回 OP_EINVAL。

## 验证

三系统 Vorbis／Opus PCM 校验一致；模拟 Seek 位置输出恒为 0 的流可完成解码、回零定位；原版和重建版三系统 Opus 实际播放通过。

Windows 11、XP SP3 x86、Win7 SP1 的组合回归各 33 项通过，其中 19 组 PCM 样本数及校验相同。这是组合测试数量，不代表本插件单独具有 33 个独立用例。Win11 按用户要求代为现代 Windows 验证，无独立 Win10 实测。

## 保留边界

仍拒绝链间声道／采样率变化、未知 Opus mapping family。此次位置修复未等同于穷尽超大文件和所有网络时序。

后续标准 IStream 边界测试发现并修正了未知长度误判、取消恢复和失败 Seek 输出污染；见 [顺序流边界记录](STREAM_BOUNDARIES_20261024.md)。该记录不将本文原有发行包的三系统验收范围扩大到后续构建。

测试代码和样本只保存在工作区 `rebuild/tests/plugins_complete_20261024`，不进入 Action 或发行 ZIP。运行 ZIP 仅含 DLL 与 SHA256SUMS；许可证留在仓库及既有对应源码包。本轮没有升级上游依赖版本或增加外部解码 DLL。

DLL 大小：353792 字节。SHA-256：`7e0189c3863f9544f3bf0d930cc79142d46128c14911f526bdb85872fbcd2dc6`。

总报告位于工作区 `rebuild/docs/AUDIO_PLUGIN_COMPLETION_20261024.md`；本文补充早期文档，不将旧轮次的通过范围扩大到未测分支。
