# Source inside Vivado 2019.1 GUI, from the project repository.
set root [file normalize [file join [file dirname [info script]] ..]]
create_project panel $root/build/vivado -part xc7z020clg400-1
set_property board_part [lindex [get_board_parts digilentinc.com:zybo-z7-20:part0:*] end] [current_project]
create_bd_design panel_io
proc ip {name kind config} {
    set cell [create_bd_cell -type ip -vlnv xilinx.com:ip:$kind:* $name]
    if {[llength $config]} {set_property -dict $config $cell}
}
proc wire {a b} {connect_bd_net [get_bd_pins $a] [get_bd_pins $b]}
ip ps processing_system7 {}
apply_bd_automation -rule xilinx.com:bd_rule:processing_system7 -config {make_external "FIXED_IO, DDR" apply_board_preset "1" Master "Disable" Slave "Disable"} [get_bd_cells ps]
ip control axi_gpio {CONFIG.C_IS_DUAL 1 CONFIG.C_GPIO_WIDTH 8 CONFIG.C_ALL_OUTPUTS 1 CONFIG.C_DOUT_DEFAULT 0x00000000 CONFIG.C_GPIO2_WIDTH 32 CONFIG.C_ALL_INPUTS_2 1}
apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config {Master "/ps/M_AXI_GP0" Slave "/control/S_AXI" intc_ip "New AXI Interconnect" Clk_master "Auto" Clk_slave "Auto" Clk_xbar "Auto" master_apm "0" ddr_seg "Auto"} [get_bd_intf_pins control/S_AXI]
set seg [lindex [get_bd_addr_segs -of_objects [get_bd_addr_spaces ps/Data]] 0]
set_property offset 0x41200000 $seg
create_bd_port -dir I -from 3 -to 0 sw
create_bd_port -dir O -from 3 -to 0 led
foreach {name low high} {remote 0 3 mask 4 7} {
    ip $name xlslice [list CONFIG.DIN_WIDTH 8 CONFIG.DIN_FROM $high CONFIG.DIN_TO $low CONFIG.DOUT_WIDTH 4]
    wire control/gpio_io_o $name/Din
}
foreach {name op} {difference xor enabled and output_value xor} {
    ip $name util_vector_logic [list CONFIG.C_SIZE 4 CONFIG.C_OPERATION $op]
}
connect_bd_net [get_bd_ports sw] [get_bd_pins difference/Op1] [get_bd_pins output_value/Op1]
wire remote/Dout difference/Op2
wire difference/Res enabled/Op1
wire mask/Dout enabled/Op2
wire enabled/Res output_value/Op2
connect_bd_net [get_bd_ports led] [get_bd_pins output_value/Res]
ip identity xlconstant {CONFIG.CONST_WIDTH 24 CONFIG.CONST_VAL 4282959}
ip feedback xlconcat {CONFIG.NUM_PORTS 3 CONFIG.IN0_WIDTH 4 CONFIG.IN1_WIDTH 4 CONFIG.IN2_WIDTH 24}
connect_bd_net [get_bd_ports sw] [get_bd_pins feedback/In0]
wire output_value/Res feedback/In1
wire identity/dout feedback/In2
wire feedback/dout control/gpio2_io_i
regenerate_bd_layout
validate_bd_design
save_bd_design
set diagram [get_files panel_io.bd]
generate_target all $diagram
add_files -norecurse [make_wrapper -files $diagram -top]
set_property top panel_io_wrapper [current_fileset]
add_files -fileset constrs_1 -norecurse $root/hardware/panel.xdc
update_compile_order -fileset sources_1
open_bd_design $diagram
launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "Implementation failed"}
set out $root/build/artifacts
file mkdir $out
set run [get_property DIRECTORY [get_runs impl_1]]
file copy -force $run/panel_io_wrapper.bit $out/panel_io_wrapper.bit
file copy -force $run/panel_io_wrapper.sysdef $out/panel.hdf
puts PANEL_BIT_READY
