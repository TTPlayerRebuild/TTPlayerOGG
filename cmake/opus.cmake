# Latest stable upstream releases checked on 2026-10-03. Sources stay in the
# ignored build cache; every network download is pinned by SHA-256.
FetchContent_Declare(ttplayer_opus
  URL https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz
  URL_HASH SHA256=6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(ttplayer_opusfile
  URL https://downloads.xiph.org/releases/opus/opusfile-0.12.tar.gz
  URL_HASH SHA256=118d8601c12dd6a44f52423e68ca9083cc9f2bfe72da7a8c1acb22a80ae3550b
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
foreach(feature BUILD_TESTING BUILD_PROGRAMS BUILD_SHARED_LIBRARY DRED OSCE
    INSTALL_PKG_CONFIG_MODULE INSTALL_CMAKE_CONFIG_MODULE)
  set(OPUS_${feature} OFF CACHE BOOL "Small static decoder distribution" FORCE)
endforeach()
# One portable path avoids AVX/OSXSAVE requirements on legacy Windows. Link-time
# elimination removes the unused encoder; standard Opus decoding stays enabled.
set(OPUS_DISABLE_INTRINSICS ON CACHE BOOL "Portable SSE2 baseline" FORCE)
set(OPUS_STATIC_RUNTIME ON CACHE BOOL "VC-LTL runtime" FORCE)
FetchContent_MakeAvailable(ttplayer_opus ttplayer_opusfile)
# opusfile 0.12 has no CMake project. This is its upstream libopusfile source list,
# without libopusurl (network I/O belongs to the host's IStream).
add_library(opusfile STATIC
  "${ttplayer_opusfile_SOURCE_DIR}/src/info.c"
  "${ttplayer_opusfile_SOURCE_DIR}/src/internal.c"
  "${ttplayer_opusfile_SOURCE_DIR}/src/opusfile.c"
  "${ttplayer_opusfile_SOURCE_DIR}/src/stream.c")
target_include_directories(opusfile PUBLIC "${ttplayer_opusfile_SOURCE_DIR}/include"
  "${ttplayer_opus_SOURCE_DIR}/include")
target_link_libraries(opusfile PUBLIC opus ogg)
target_compile_definitions(opusfile PRIVATE _CRT_SECURE_NO_WARNINGS)
foreach(codec opus opusfile)
  target_compile_options(${codec} PRIVATE /Os /Gy /Gw /GF /fp:precise /arch:SSE2
    /wd4244 /wd4267 /wd4996)
endforeach()
