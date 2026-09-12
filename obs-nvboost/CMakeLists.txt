cmake_minimum_required(VERSION 3.28...3.30)

# ---------------------------------------------------------------------------
# NVENC SDK 头文件版本不在此强制：目标 SDK 版本由外部注入的 FFnvcodec_INCLUDE_DIR
# 决定（见 scripts/Setup-FFnvcodec.ps1，从 third-party/nv-codec-headers 按 tag 提取），
# 由此实现"每个驱动对应的 SDK 编译出对应可用版本"的核心目标。
# ---------------------------------------------------------------------------
find_package(FFnvcodec REQUIRED)

message(STATUS "obs-nvboost: targeting NVENC SDK ${FFnvcodec_VERSION} (include: ${FFnvcodec_INCLUDE_DIR})")

if(NOT TARGET OBS::opts-parser)
  add_subdirectory("${CMAKE_SOURCE_DIR}/shared/opts-parser" "${CMAKE_BINARY_DIR}/shared/opts-parser")
endif()

if(OS_LINUX AND NOT TARGET OBS::glad)
  add_subdirectory("${CMAKE_SOURCE_DIR}/deps/glad" "${CMAKE_BINARY_DIR}/deps/glad")
endif()

add_library(obs-nvboost MODULE)

target_sources(
  obs-nvboost
  PRIVATE
    $<$<PLATFORM_ID:Linux>:nvenc-opengl.c>
    $<$<PLATFORM_ID:Windows>:nvenc-d3d11.c>
    cuda-helpers.c
    cuda-helpers.h
    nvenc-cuda.c
    nvenc-helpers.c
    nvenc-helpers.h
    nvenc-internal.h
    nvenc-opts-parser.c
    nvenc-properties.c
    nvenc.c
    obs-nvenc.c
    obs-nvenc.h
)

target_link_libraries(
  obs-nvboost
  PRIVATE OBS::libobs OBS::opts-parser FFnvcodec::FFnvcodec $<$<PLATFORM_ID:Linux>:OBS::glad>
)

# 强制注入的 SDK 头优先：OBS::libobs 传递的 .deps/obs-deps 自带 ffnvcodec 头（13.1）
# 若排在前面会遮蔽 FFnvcodec_INCLUDE_DIR，导致所有 SDK 矩阵都编译成同一份 13.1 产物
target_include_directories(obs-nvboost SYSTEM PRIVATE BEFORE "${FFnvcodec_INCLUDE_DIR}")

# 能力探测子进程：在独立进程中加载 nvEncodeAPI64.dll 查询设备/驱动能力，
# 驱动问题不会波及 OBS 主进程（对应 nvenc-helpers.c 中的 nvenc_check()）
add_executable(obs-nvboost-test)
target_sources(obs-nvboost-test PRIVATE obs-nvboost-test.cpp)
target_link_libraries(obs-nvboost-test PRIVATE FFnvcodec::FFnvcodec)
target_include_directories(obs-nvboost-test SYSTEM PRIVATE BEFORE "${FFnvcodec_INCLUDE_DIR}")

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in obs-nvboost.rc)
  target_sources(obs-nvboost PRIVATE obs-nvboost.rc)
endif()

set_target_properties_obs(obs-nvboost PROPERTIES FOLDER plugins/obs-nvboost PREFIX "")
set_target_properties_obs(obs-nvboost-test PROPERTIES FOLDER plugins/obs-nvboost)
