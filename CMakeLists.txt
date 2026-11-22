# obs-nv-boost 仓库根入口
#
# 插件目标定义在 obs-nvboost/ 子目录（依赖 OBS 父工程提供的 target：
# OBS::libobs / OBS::opts-parser / set_target_properties_obs 等，
# 必须挂在 OBS 源码树内构建，无法独立 configure）。
#
# CI 将本仓库 checkout 到 obs-studio/plugins/obs-nv-boost，
# 由 plugins/CMakeLists.txt 追加 add_subdirectory("obs-nv-boost" obs-nvboost-build)
# 挂载，需要本文件作为 add_subdirectory() 的入口（同 obs-qsv-onevpl-boost 的布局）。
add_subdirectory(obs-nvboost)
