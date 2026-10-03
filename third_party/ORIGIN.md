# 第三方依赖

源码由 CMake 在构建时下载，仓库保留本文件及版权文本。

| 项目 | 固定下载地址 | SHA-256 |
| --- | --- | --- |
| libogg 1.3.6 | https://downloads.xiph.org/releases/ogg/libogg-1.3.6.tar.xz | `5c8253428e181840cd20d41f3ca16557a9cc04bad4a3d04cce84808677fa1061` |
| libvorbis 1.3.7 | https://downloads.xiph.org/releases/vorbis/libvorbis-1.3.7.tar.xz | `b33cc4934322bcbf6efcbacf49e3ca01aadbea4114ec9589d1b1e9d20f72954b` |
| libopus 1.6.1 | https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz | `6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1` |
| opusfile 0.12 | https://downloads.xiph.org/releases/opus/opusfile-0.12.tar.gz | `118d8601c12dd6a44f52423e68ca9083cc9f2bfe72da7a8c1acb22a80ae3550b` |
| YY-Thunks 1.2.2 | https://github.com/Chuyu-Team/YY-Thunks/releases/download/v1.2.2/YY-Thunks-Objs.zip | `518ed7ef4825e8a41997fbccfa2c8090cf31a6038fd51520a2e49886f947f9fc` |
| VC-LTL 5.3.1 | https://github.com/Chuyu-Team/VC-LTL5/releases/download/v5.3.1/VC-LTL-Binary.7z | `7a18799ed3aa84a225610a5447a56bc534c5c98ccb8dec05caba0e3f633431ad` |

Xiph 库源码未经修改。构建层关闭共享库、上游测试及安装文档，设置 CMake 兼容版本、大小优化和精确浮点。
VC-LTL/YY-Thunks 采用 `cmake/legacy_windows.cmake` 的 XP 配置。

版权文本：

- [libogg COPYING](libogg/COPYING)
- [libvorbis COPYING](libvorbis/COPYING)
- [libopus COPYING（含专利许可链接）](opus/COPYING)
- [opusfile COPYING](opusfile/COPYING)
- [VC-LTL](../docs/licenses/VC-LTL-LICENSE.txt)
- [YY-Thunks](../docs/licenses/YY-Thunks-LICENSE.txt)

官方资料：[Vorbisfile 回调](https://xiph.org/vorbis/doc/vorbisfile/ov_open_callbacks.html)、[Vorbis 格式](https://xiph.org/vorbis/doc/Vorbis_I_spec.html)。

2026-10-03 核对最新稳定版：[Xiph 下载](https://xiph.org/downloads/)、[Opus 下载](https://opus-codec.org/downloads/)、[VC-LTL 发布](https://github.com/Chuyu-Team/VC-LTL5/releases/latest)、[YY-Thunks 发布](https://github.com/Chuyu-Team/YY-Thunks/releases/latest)。原有四项依赖已为最新；本次新增 libopus 1.6.1 / opusfile 0.12。

`cmake/opus.cmake` 关闭 Opus 的测试、程序、DRED、OSCE 及额外 SIMD 分派；保持标准浮点解码和生产安全检查，未改动库源码。opusfile 按上游 Makefile 的四个 libopusfile 源文件单独建立静态目标，不引入 libopusurl 或 OpenSSL。六份许可证仅保存在仓库，发行说明提供对应链接；按用户要求不嵌入 DLL。
