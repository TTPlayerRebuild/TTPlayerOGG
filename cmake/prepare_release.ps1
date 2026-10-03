[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$PreviousVersion,
    [Parameter(Mandatory = $true)][string]$Repository,
    [Parameter(Mandatory = $true)][string]$Commit,
    [string]$ArtifactDirectory = 'artifact',
    [string]$ServerUrl = 'https://github.com'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'version.ps1')
$version = (Get-OggBuildVersion $Version).Name
if ($PreviousVersion -and -not (Read-OggVersionTag $PreviousVersion)) { throw 'Invalid previous release version.' }
if ($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or $Commit -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'Invalid repository or commit.'
}
$archiveName = "ttp_ogg-$version.zip"
$hash = (Get-FileHash -LiteralPath (Join-Path $ArtifactDirectory $archiveName) -Algorithm SHA256).Hash.ToLowerInvariant()
$expected = (Get-Content -LiteralPath (Join-Path $ArtifactDirectory 'SHA256SUMS.txt') -Encoding UTF8 -Raw).Trim()
if ($expected -cne "$hash  $archiveName") { throw 'OGG package SHA-256 verification failed.' }
$repoUrl = "$ServerUrl/$Repository"
$logUrl = if ($PreviousVersion) { "$repoUrl/compare/$PreviousVersion...$version" } else { "$repoUrl/commits/$version" }
$notes = @"
**更新记录**: $logUrl

解压 ttp_ogg-$version.zip，关闭播放器后，将 AddIn/ttp_ogg.dll 放入安装目录。
支持 Ogg Vorbis / Opus 播放、定位、文本标签编辑及封面读写。
同一 x86 DLL 面向 XP SP3、Win7 和新系统，CPU 需要 SSE2。
ZIP 仅包含 AddIn/ttp_ogg.dll 和 SHA256SUMS.txt；版权与许可证保留在下方链接的源码仓库，不嵌入 DLL。

[源码、构建及验证说明]($repoUrl/tree/$Commit)
[libogg 版权与许可证]($repoUrl/blob/$Commit/third_party/libogg/COPYING)
[libvorbis 版权与许可证]($repoUrl/blob/$Commit/third_party/libvorbis/COPYING)
[libopus 版权、许可证及专利许可链接]($repoUrl/blob/$Commit/third_party/opus/COPYING)
[opusfile 版权与许可证]($repoUrl/blob/$Commit/third_party/opusfile/COPYING)
[VC-LTL 许可证]($repoUrl/blob/$Commit/docs/licenses/VC-LTL-LICENSE.txt)
[YY-Thunks 许可证]($repoUrl/blob/$Commit/docs/licenses/YY-Thunks-LICENSE.txt)
"@
$notes | Set-Content -LiteralPath (Join-Path $ArtifactDirectory 'release-notes.md') -Encoding UTF8
