cmake_minimum_required(VERSION 3.24)
project(TtpOgg LANGUAGES C CXX RC)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "ttp_ogg requires MSVC and -A Win32")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(cmake/legacy_windows.cmake)
include(cmake/size_release.cmake)
include(cmake/vorbis.cmake)
include(cmake/opus.cmake)
include(cmake/version.cmake)
add_library(ttp_ogg SHARED src/plugin.cpp src/reader.cpp src/comments.cpp src/pictures.cpp
  src/ttp_ogg.def "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc")
add_dependencies(ttp_ogg ttp_ogg_version)
target_include_directories(ttp_ogg PUBLIC include PRIVATE src)
target_compile_definitions(ttp_ogg PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
  _WIN32_WINNT=0x0501 _CRT_SECURE_NO_WARNINGS)
target_compile_options(ttp_ogg PRIVATE /W4 /permissive- /EHsc /GR- /Os /Gy /Gw /GF
  /utf-8 /fp:precise /arch:SSE2)
target_link_libraries(ttp_ogg PRIVATE vorbisfile vorbis opusfile ogg ole32 uuid shlwapi)
target_link_options(ttp_ogg PRIVATE /OPT:REF /OPT:ICF /INCREMENTAL:NO /DEBUG:NONE)
set_target_properties(ttp_ogg PROPERTIES PREFIX "" OUTPUT_NAME "ttp_ogg")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(TARGET ttp_ogg POST_BUILD
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_legacy_imports.py"
    "$<TARGET_FILE:ttp_ogg>"
    --exports "${ttplayer_yy_thunks_SOURCE_DIR}/Config/x86/5.1.2600.txt"
    --exports "${ttplayer_yy_thunks_SOURCE_DIR}/Config/x86/6.1.7600.txt"
    --report "$<TARGET_FILE_DIR:ttp_ogg>/legacy-imports.json" VERBATIM)
install(TARGETS ttp_ogg RUNTIME DESTINATION AddIn COMPONENT Runtime)
