include(FetchContent)
# Upstream 1.3.7 predates CMake 4's removal of compatibility below 3.5.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
# The repository keeps licenses, not vendored upstream source trees.
FetchContent_Declare(ttplayer_ogg
  URL https://downloads.xiph.org/releases/ogg/libogg-1.3.6.tar.xz
  URL_HASH SHA256=5c8253428e181840cd20d41f3ca16557a9cc04bad4a3d04cce84808677fa1061
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(ttplayer_vorbis
  URL https://downloads.xiph.org/releases/vorbis/libvorbis-1.3.7.tar.xz
  URL_HASH SHA256=b33cc4934322bcbf6efcbacf49e3ca01aadbea4114ec9589d1b1e9d20f72954b
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static codec libraries" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "No upstream tests in distribution builds" FORCE)
set(INSTALL_DOCS OFF CACHE BOOL "No upstream documentation build" FORCE)
set(INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "No pkg-config installation" FORCE)
set(INSTALL_CMAKE_PACKAGE_MODULE OFF CACHE BOOL "No CMake package installation" FORCE)
FetchContent_MakeAvailable(ttplayer_ogg)
set(OGG_INCLUDE_DIR "${ttplayer_ogg_SOURCE_DIR}/include" CACHE PATH "libogg headers" FORCE)
set(OGG_LIBRARY ogg CACHE STRING "Static libogg target" FORCE)
FetchContent_MakeAvailable(ttplayer_vorbis)
foreach(codec ogg vorbis vorbisfile)
  target_compile_options(${codec} PRIVATE /Os /Gy /Gw /GF /fp:precise /arch:SSE2 /utf-8
    /wd4244 /wd4267 /wd4996)
endforeach()
