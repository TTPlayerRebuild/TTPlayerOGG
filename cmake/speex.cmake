include(FetchContent)
FetchContent_Declare(ttplayer_speex
  URL https://downloads.xiph.org/releases/speex/speex-1.2.1.tar.gz
  URL_HASH SHA256=4b44d4f2b38a370a2d98a78329fefc56a0cf93d1c1be70029217baae6628feea
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(ttplayer_speex)
file(READ "${ttplayer_speex_SOURCE_DIR}/configure.ac" _speex_configure)
if(NOT _speex_configure MATCHES "AC_INIT\\(\\[speex\\],\\[1\\.2\\.1\\]")
  message(FATAL_ERROR "Expected Speex 1.2.1 sources")
endif()
set(_speex_files cb_search.c exc_10_32_table.c exc_8_128_table.c filters.c gain_table.c
  hexc_table.c high_lsp_tables.c lsp.c ltp.c speex.c stereo.c vbr.c vq.c bits.c
  exc_10_16_table.c exc_20_32_table.c exc_5_256_table.c exc_5_64_table.c gain_table_lbr.c
  hexc_10_32_table.c lpc.c lsp_tables_nb.c modes.c modes_wb.c nb_celp.c quant_lsp.c
  sb_celp.c speex_callbacks.c speex_header.c window.c)
list(TRANSFORM _speex_files PREPEND "${ttplayer_speex_SOURCE_DIR}/libspeex/")
add_library(ttp_speex STATIC ${_speex_files})
target_include_directories(ttp_speex PUBLIC "${ttplayer_speex_SOURCE_DIR}/include"
  PRIVATE "${ttplayer_speex_SOURCE_DIR}/win32" "${ttplayer_speex_SOURCE_DIR}/libspeex")
target_compile_definitions(ttp_speex PRIVATE HAVE_CONFIG_H _CRT_SECURE_NO_WARNINGS)
target_compile_options(ttp_speex PRIVATE /fp:precise /arch:SSE2)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/speex-source-path.txt" "${ttplayer_speex_SOURCE_DIR}")
