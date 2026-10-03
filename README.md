# ttp_ogg

面向千千静听原版与 TTPlayer Rebuild 的 x86 **Ogg Vorbis / Opus 输入插件**。
依据工作区 `AddIn/ttp_ogg.dll` 的伪代码、二进制接口及本地行为重建，并对照旧版 `TTPlayer5719/AddIn/ttp_ogg.dll`。

宿主适配层与标签保存模块为重建实现，Vorbis、Opus 算法使用固定版本的 Xiph 官方库。Vorbis 恢复原插件行为；Opus 和封面是在相同宿主 ABI 上新增的功能。它不是原二进制的逐字节复刻，也不声称获得了厂商原始源码。

## 功能

- 原 `ttpGetSoundAddIn`、ReaderCreator、Reader、Metadata x86 ABI。
- 单一 `OGG Reader` 工厂，声明 `*.ogg;*.oga;*.opus`，根据真实 Ogg BOS 头选择解码器。
- 宿主 IStream、不可定位网络流、扩展读取与回调转发。
- IEEE float64 PCM、原六声道顺序、时长、码率、时间定位、同格式链式播放。
- Opus 输出 48 kHz，处理 pre-skip、末尾裁剪、头部增益及 family 0/1 的 1～8 声道；提供 R128 到宿主 ReplayGain 的查询适配。
- UTF-8 文本标签，大小写不敏感查找，保留同名多值，修改首个匹配值。
- 最终 Release 时保存；保留压缩音频包、时间位置、其他逻辑流及尾部附加数据。
- Unicode 路径、短写检查、失败回滚；原 ABI 没有显式保存结果方法，详见限制。

两种编码共用 Thumbnail 接口：读取、添加、替换和删除 PNG/JPEG/GIF 封面；支持多图、优先正面封面、兼容旧式 COVERART。

原 DLL 不提供 Thumbnail；本版新增宿主已有的 Thumbnail ABI，仍不提供 Encoder 或独立 Decoder 工厂。

## 构建

需要 Windows、现代 MSVC x86 C++ 工具、Windows SDK、CMake 3.24+、PowerShell 和 Python 3。

```powershell
./build.ps1 -Package
```

默认生成器为 `Visual Studio 18 2026`；也可显式指定已安装的兼容生成器。
与 `ttp_aac`、播放器重建项目分别配置和构建，不引用相邻项目源码。

依赖在配置时下载并验证固定 SHA-256：

| 依赖 | 版本 | 用途 |
| --- | --- | --- |
| libogg | 1.3.6 | Ogg 页及 CRC |
| libvorbis / vorbisfile | 1.3.7 | Vorbis 解码与定位 |
| libopus | 1.6.1 | Opus 解码 |
| opusfile | 0.12 | Ogg Opus 定位、裁剪、标签与图片解析 |
| VC-LTL | 5.3.1 | XP CRT 适配 |
| YY-Thunks | 1.2.2 | Windows API 兼容 |

以上均为 2026-10-03 核对的最新稳定版。仓库只保存上游版权与许可证，不提交下载的依赖源码。
采用 `/O1 /Os`、LTO、静态嵌入编解码库；同一 DLL 支持 XP SP3、Win7 及新系统，CPU 需要 SSE2。
构建后审查 XP/Win7 静态导入，不要求用户安装 VC 2012、UCRT 或额外的 Xiph DLL。

## 输出及版本

```text
build/Release/ttp_ogg.dll
build/Release/ttp_ogg-yyyy.MM.dd[pN].zip
build/Release/SHA256SUMS.txt
```

版本采用北京时间日期；Actions 的 `Release a Version` 根据已占用的日期标签分配 `pN`，并在编译前写入文件资源。
ZIP 只包含 `AddIn/ttp_ogg.dll` 和 `SHA256SUMS.txt`。
libogg、libvorbis、VC-LTL、YY-Thunks、libopus、opusfile 的许可证保留在仓库；按用户要求不嵌入 DLL，也不加入 ZIP。发行说明提供仓库许可证链接。

Actions 仅构建、审查导入、打包及按选项发布，不运行或上传本地测试。
本地测试与逆向证据位于工作区 `rebuild/tests/ogg_rebuild` 和 `rebuild/tests/ogg_analysis`，不属于本项目发行源码。

## 安装

关闭播放器，备份安装目录现有 `AddIn/ttp_ogg.dll`，用本项目 DLL 替换后重新打开播放器。
不要同时保留两个声明同一格式的 Ogg 插件变体。
开发与测试使用隔离目录；本次构建没有替换用户现用 AddIn。

## 验证与边界

见 [Vorbis 原版对照](docs/RECONSTRUCTION.md)、[Vorbis 基线验证](docs/VALIDATION.md) 和 [当前 Vorbis/Opus/封面实现与验证](docs/OPUS_COVER_IMPLEMENTATION.md)。

恢复目标是原插件公开功能与正常播放行为；有意修正错误被误报 EOF、未对齐小缓冲及保存短写等缺陷。
链间采样率或声道数变化仍不支持自动格式转换，但会明确返回错误。
测试样本逐字节相同不代表所有 Vorbis 文件必然与旧解码库逐字节一致。

## 许可证

本项目适配与保存代码采用 [MIT](LICENSE)。第三方代码分别受其自身许可证约束。
本项目未采用 `vcedit.c` 编辑器源码；注释写入按 Ogg/Vorbis 格式及实测行为独立实现。
依赖来源、版本和摘要见 [第三方来源](third_party/ORIGIN.md)。
