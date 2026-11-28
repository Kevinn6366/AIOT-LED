cd build
open_project qwen_fast_hls
set_top qwen_qproj
add_files ../hardware/qwen/qproj.cpp
open_solution sol
set_part xc7z020clg400-1
create_clock -period 20
csynth_design
export_design -format ip_catalog
exit
