# Diagnostic copy of the b9ea48d routed design. Expose two row-phase flags and
# the idle bit on the three low bits of its read-only cycle-counter GPIO.
set build [file normalize [file join [file dirname [info script]] ../../build]]
file mkdir $build/qwen
cd $build/qwen
open_checkpoint $build/qwen-all-panel/panel.runs/impl_1/panel_io_wrapper_routed.dcp
set base panel_io_i/qproj/inst
proc one {objects} {
    if {[llength $objects] != 1} {error "Expected one object: $objects"}
    return [lindex $objects 0]
}
proc state_net {s} {
    return [one [get_nets $::base/ap_CS_fsm_state$s]]
}
proc flag {name start stop} {
    set f [create_cell -reference FDRE ${name}_reg]
    set gate [create_cell -reference LUT2 ${name}_or]
    set_property INIT 4'hE $gate
    set q [create_net ${name}_flag]
    set d [create_net ${name}_d]
    connect_net -net $q -objects [get_pins [list $f/Q $gate/I0]]
    connect_net -net $d -objects [get_pins [list $gate/O $f/D]]
    foreach {pin net} [list $gate/I1 [state_net $start] $f/R [state_net $stop] $f/C [one [get_nets -of_objects [get_pins $::base/ap_clk]]] $f/CE [one [get_nets -of_objects [lindex [get_pins -hier -filter {REF_PIN_NAME == P && REF_NAME == VCC}] 0]]]] {
        connect_net -hierarchical -net $net -objects [get_pins $pin]
    }
    return $q
}
set flags [list [flag weight_phase 28 38] [flag compute_phase 38 79] [one [get_nets -of_objects [one [get_pins -hier -filter {NAME == "panel_io_i/qproj/inst/ap_CS_fsm_reg[0]/Q"}]]]]]
for {set bit 0} {$bit < 3} {incr bit} {
    set pin [one [get_pins -hier -filter [format {NAME == "panel_io_i/qcycles/gpio_io_i[%d]"} $bit]]]
    set net [one [get_nets -of_objects $pin]]
    disconnect_net -net $net -objects $pin
    connect_net -hierarchical -net [lindex $flags $bit] -objects $pin
}
place_design -directive Quick
route_design
report_timing_summary -file profile-timing.rpt
write_bitstream -force profile.bit
