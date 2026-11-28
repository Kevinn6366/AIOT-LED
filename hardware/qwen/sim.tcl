cd build
open_project qwen_csim
set_top qwen_qproj
add_files ../hardware/qwen/qproj.cpp
add_files -tb ../tests/qproj_tb.cpp
open_solution sol
set_part xc7z020clg400-1
create_clock -period 20
csim_design -clean
exit
