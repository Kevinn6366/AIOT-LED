# Keep the routed datapath fixed while using the divider settings of the
# 110 MHz Clocking Wizard design and its matching SDK hardware definition.
set root [file normalize [file join [file dirname [info script]] ../..]]
set old $root/build/qwen-dual-panel/panel.runs/impl_1
set new $root/build/qwen-dual110-panel/panel.runs/impl_1
set out $root/build/qwen-eco110-artifacts
file mkdir $out
open_checkpoint $old/panel_io_wrapper_routed.dcp
set mmcm [get_cells -hier -filter {REF_NAME == MMCME2_ADV}]
if {[llength $mmcm]!=1} {error "Unexpected number of clock managers: $mmcm"}
set_property -dict {CLKFBOUT_MULT_F 20.625 CLKOUT0_DIVIDE_F 9.375} $mmcm
set fast [get_clocks -filter {NAME =~ clk_out1*}]
if {[llength $fast]!=1 || abs([get_property PERIOD $fast]-9.091)>0.015} {error "Expected a derived 110 MHz clock, got $fast [get_property PERIOD $fast]"}
report_clocks -file $out/clocks.rpt
report_timing_summary -file $out/timing.rpt
set setup [get_property SLACK [lindex [get_timing_paths -delay_type max -max_paths 1] 0]]
set hold [get_property SLACK [lindex [get_timing_paths -delay_type min -max_paths 1] 0]]
if {$setup<0 || $hold<0} {error "Timing failed $setup $hold"}
write_bitstream -force $out/panel_io_wrapper.bit
write_sysdef -force -hwdef $new/panel_io_wrapper.hwdef -bitfile $out/panel_io_wrapper.bit -file $out/panel.hdf
