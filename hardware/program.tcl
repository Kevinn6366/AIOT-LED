# Run with an attached ZYBO Z7-20 and the panel bitstream from panel.tcl.
set root [file normalize [file join [file dirname [info script]] ..]]
set bit $root/build/artifacts/panel_io_wrapper.bit
open_hw
connect_hw_server
open_hw_target
set devices [get_hw_devices -quiet xc7z020*]
if {[llength $devices] != 1} {error "Expected one connected XC7Z020"}
set dev [lindex $devices 0]
current_hw_device $dev
set_property PROGRAM.FILE $bit $dev
program_hw_devices $dev
refresh_hw_device $dev
close_hw_target
puts PANEL_JTAG_PROGRAMMED
