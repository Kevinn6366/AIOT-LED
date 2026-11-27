# Run with Xilinx SDK 2019.1 xsct.bat.
set root [file normalize [file join [file dirname [info script]] ..]]
set out $root/build/artifacts
file mkdir $out
set cc [file join $::env(XILINX_SDK) gnu aarch32 nt gcc-arm-linux-gnueabi bin arm-linux-gnueabihf-gcc.exe]
puts [exec $cc --version]
foreach {source target} {panel_io panel-io led_control led-control gpt gpt} {
    puts "Building $target.elf from $source.c"
    exec $cc -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -std=gnu11 -Wall -Wextra -Werror -O2 -g $root/device/$source.c -o $out/$target.elf
    puts [exec [file join [file dirname $cc] arm-linux-gnueabihf-size.exe] $out/$target.elf]
}
file copy -force $out/led-control.elf $root/build/led-control
