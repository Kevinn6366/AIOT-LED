# Source inside Vivado 2019.1 GUI, from the project repository.
set root [file normalize [file join [file dirname [info script]] ..]]
set project_dir $root/build/rgb-vivado
if {[info exists ::env(AIOT_GPT_RTL)]} {
    set project_dir $root/build/gpt-panel
}
if {[info exists ::env(AIOT_QWEN_IP)]} {set project_dir $root/build/qwen-flow-panel}
create_project panel $project_dir -part xc7z020clg400-1
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
ip rgb axi_gpio {CONFIG.C_IS_DUAL 1 CONFIG.C_GPIO_WIDTH 24 CONFIG.C_ALL_OUTPUTS 1 CONFIG.C_GPIO2_WIDTH 24 CONFIG.C_ALL_OUTPUTS_2 1}
apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config {Master "/ps/M_AXI_GP0" Slave "/rgb/S_AXI" Clk_master "Auto" Clk_slave "Auto" Clk_xbar "Auto"} [get_bd_intf_pins rgb/S_AXI]
set rgbseg [get_bd_addr_segs -of_objects [get_bd_addr_spaces ps/Data] -filter {NAME =~ *rgb*}]
set_property offset 0x41210000 $rgbseg
# FCLK / 65536 gives a common PWM period. Limit peak duty to 255/512.
ip counter c_counter_binary {CONFIG.Output_Width 16 CONFIG.CE false}
wire ps/FCLK_CLK0 counter/CLK
ip phase xlslice {CONFIG.DIN_WIDTH 16 CONFIG.DIN_FROM 15 CONFIG.DIN_TO 7 CONFIG.DOUT_WIDTH 9}
wire counter/Q phase/Din
ip pwm_sample xlconcat {CONFIG.NUM_PORTS 6}
proc connect_rgb_lamp {lamp output_pin index} {
    foreach {color low} {r 16 g 8 b 0} {
        set n ld${lamp}_${color}
        ip ${n}_level xlslice [list CONFIG.DIN_WIDTH 24 CONFIG.DIN_FROM [expr {$low+7}] CONFIG.DIN_TO $low CONFIG.DOUT_WIDTH 8]
        wire rgb/$output_pin ${n}_level/Din
        ip ${n}_compare c_addsub {CONFIG.Add_Mode Subtract CONFIG.A_Type Unsigned CONFIG.B_Type Unsigned CONFIG.A_Width 9 CONFIG.B_Width 8 CONFIG.Out_Width 10 CONFIG.Latency 1 CONFIG.CE false}
        wire ps/FCLK_CLK0 ${n}_compare/CLK
        wire phase/Dout ${n}_compare/A
        wire ${n}_level/Dout ${n}_compare/B
        ip ${n}_pulse xlslice {CONFIG.DIN_WIDTH 10 CONFIG.DIN_FROM 9 CONFIG.DIN_TO 9 CONFIG.DOUT_WIDTH 1}
        wire ${n}_compare/S ${n}_pulse/Din
        create_bd_port -dir O $n
        connect_bd_net [get_bd_pins ${n}_pulse/Dout] [get_bd_ports $n] [get_bd_pins pwm_sample/In$index]
        incr index
    }
}
connect_rgb_lamp 5 gpio_io_o 0
connect_rgb_lamp 6 gpio2_io_o 3
ip identity xlconstant {CONFIG.CONST_WIDTH 18 CONFIG.CONST_VAL 21063}
ip feedback xlconcat {CONFIG.NUM_PORTS 4 CONFIG.IN0_WIDTH 4 CONFIG.IN1_WIDTH 4 CONFIG.IN2_WIDTH 6 CONFIG.IN3_WIDTH 18}
connect_bd_net [get_bd_ports sw] [get_bd_pins feedback/In0]
wire output_value/Res feedback/In1
wire pwm_sample/dout feedback/In2
wire identity/dout feedback/In3
wire feedback/dout control/gpio2_io_i
if {[info exists ::env(AIOT_GPT_RTL)]} {
    set rtl [file normalize $::env(AIOT_GPT_RTL)]
    # Vivado runs synthesis in a separate directory. Resolve the Spinal ROM files there.
    set fd [open $rtl/ZyboGPTTop.v r]
    set source [read $fd]
    close $fd
    foreach rom [glob $rtl/*.bin] {
        set name [file tail $rom]
        set source [string map [list "\"$name\"" "\"$rom\""] $source]
    }
    set core $project_dir/ZyboGPTTop.v
    set fd [open $core w]
    puts -nonewline $fd $source
    close $fd
    add_files -norecurse [list $core $rtl/ZyboGPTWrapper.v]
    update_compile_order -fileset sources_1
    create_bd_cell -type module -reference ZyboGPTWrapper language_model
    apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config {Master "/ps/M_AXI_GP0" Slave "/language_model/s_axi" Clk_master "Auto" Clk_slave "Auto" Clk_xbar "Auto"} [get_bd_intf_pins language_model/s_axi]
    set mapped [get_bd_addr_segs -of_objects [get_bd_addr_spaces ps/Data] -filter {NAME =~ *language_model*}]
    if {[llength $mapped] != 1} {error "Missing language model address segment"}
    set_property offset 0x43C00000 $mapped
}
if {[info exists ::env(AIOT_QWEN_IP)]} {
    set_property ip_repo_paths [file normalize $::env(AIOT_QWEN_IP)] [current_project]
    update_ip_catalog
    set_property -dict {CONFIG.PCW_USE_S_AXI_HP0 1 CONFIG.PCW_USE_S_AXI_HP2 1 CONFIG.PCW_USE_S_AXI_HP1 1 CONFIG.PCW_USE_S_AXI_HP3 1} [get_bd_cells ps]
    create_bd_cell -type ip -vlnv xilinx.com:hls:qwen_qproj:1.0 qproj
    # Leave the legacy panel, GPT, PS GP0 and telemetry on FCLK0.
    # The matrix accelerator and its four HP memory ports use the faster clock.
    ip qclk clk_wiz {CONFIG.PRIM_IN_FREQ 50.000 CONFIG.CLKOUT1_REQUESTED_OUT_FREQ 110.000 CONFIG.USE_RESET false}
    wire ps/FCLK_CLK0 qclk/clk_in1
    ip qreset proc_sys_reset {CONFIG.C_EXT_RESET_HIGH 0}
    wire qclk/clk_out1 qreset/slowest_sync_clk
    wire ps/FCLK_RESET0_N qreset/ext_reset_in
    wire qclk/locked qreset/dcm_locked

    # The existing GP0 AXI interconnect inserts the clock converter on this MI.
    set fabric [get_bd_cells ps_axi_periph]
    set index [get_property CONFIG.NUM_MI $fabric]
    set mi [format M%02d $index]
    set_property CONFIG.NUM_MI [expr {$index+1}] $fabric
    connect_bd_intf_net [get_bd_intf_pins ps_axi_periph/${mi}_AXI] [get_bd_intf_pins qproj/s_axi_control]
    wire qclk/clk_out1 ps_axi_periph/${mi}_ACLK
    wire qreset/peripheral_aresetn ps_axi_periph/${mi}_ARESETN
    foreach {port hp core} {0 hp qhp 2 hp2 qhp2 1 hp1 qhp1 3 hp3 qhp3} {
        ip $core axi_protocol_converter {CONFIG.MI_PROTOCOL AXI3}
        connect_bd_intf_net [get_bd_intf_pins qproj/m_axi_${hp}] [get_bd_intf_pins ${core}/S_AXI]
        connect_bd_intf_net [get_bd_intf_pins ${core}/M_AXI] [get_bd_intf_pins ps/S_AXI_HP${port}]
        wire qclk/clk_out1 ${core}/aclk
        wire qclk/clk_out1 ps/S_AXI_HP${port}_ACLK
        wire qreset/peripheral_aresetn ${core}/aresetn
    }
    wire qclk/clk_out1 qproj/ap_clk
    wire qreset/peripheral_aresetn qproj/ap_rst_n
    assign_bd_address -offset 0x43C10000 -range 64K -target_address_space [get_bd_addr_spaces ps/Data] [get_bd_addr_segs qproj/s_axi_control/Reg]
    assign_bd_address -offset 0 -range 1G -target_address_space [get_bd_addr_spaces qproj/Data_m_axi_hp] [get_bd_addr_segs ps/S_AXI_HP0/HP0_DDR_LOWOCM]
    assign_bd_address -offset 0 -range 1G -target_address_space [get_bd_addr_spaces qproj/Data_m_axi_hp2] [get_bd_addr_segs ps/S_AXI_HP2/HP2_DDR_LOWOCM]
    assign_bd_address -offset 0 -range 1G -target_address_space [get_bd_addr_spaces qproj/Data_m_axi_hp1] [get_bd_addr_segs ps/S_AXI_HP1/HP1_DDR_LOWOCM]
    assign_bd_address -offset 0 -range 1G -target_address_space [get_bd_addr_spaces qproj/Data_m_axi_hp3] [get_bd_addr_segs ps/S_AXI_HP3/HP3_DDR_LOWOCM]
    ip qclock c_counter_binary {CONFIG.Output_Width 32 CONFIG.CE false}
    wire ps/FCLK_CLK0 qclock/CLK
    ip qcycles axi_gpio {CONFIG.C_GPIO_WIDTH 32 CONFIG.C_ALL_INPUTS 1}
    wire qclock/Q qcycles/gpio_io_i
    apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config {Master "/ps/M_AXI_GP0" Slave "/qcycles/S_AXI" Clk_master "Auto" Clk_slave "Auto" Clk_xbar "Auto"} [get_bd_intf_pins qcycles/S_AXI]
    set_property offset 0x41220000 [get_bd_addr_segs -of_objects [get_bd_addr_spaces ps/Data] -filter {NAME =~ *qcycles*}]
}
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
if {[info exists ::env(AIOT_GPT_RTL)]} {set out $root/build/gpt-artifacts}
if {[info exists ::env(AIOT_QWEN_IP)]} {set out $root/build/qwen-flow-artifacts}
file mkdir $out
set run [get_property DIRECTORY [get_runs impl_1]]
file copy -force $run/panel_io_wrapper.bit $out/panel_io_wrapper.bit
file copy -force $run/panel_io_wrapper.sysdef $out/panel.hdf
puts PANEL_BIT_READY
