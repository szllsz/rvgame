# Portable package entry point. No original XPR, generated DCP, or sibling repo.
if {![regexp {^2025\.2(\.[0-9]+)?$} [version -short]]} {error "Vivado2025.2 family required"}
set package [file dirname [file dirname [file normalize [info script]]]]
set destination [file normalize ./system640_clean]
if {[llength $argv]>0} {set destination [file normalize [lindex $argv 0]]}
if {[file exists $destination]} {error "Reconstruction destination must be new"}
set_param general.maxThreads 1
create_project system640_clean $destination -part xc7z020clg400-2
set_property target_language Verilog [current_project]
set_property ip_repo_paths [list $package/ip_repo] [current_project]
update_ip_catalog
add_files [glob $package/rtl/*.v]
add_files [glob $package/rtl/*.mem]
add_files -fileset constrs_1 [glob $package/constraints/*.xdc]
foreach ip_file [glob $package/ip/*.xci] {
    if {[file tail $ip_file] ne "video_clock.xci"} {import_ip -files $ip_file}
}
# Inspect imported custom IP before rebuilding it with the selected release.
report_ip_status -file $destination/imported_ip_status_before.rpt
foreach migrated_ip [get_ips] {
    if {[get_property IS_LOCKED $migrated_ip]} {upgrade_ip $migrated_ip}
}
report_ip_status -file $destination/imported_ip_status_after.rpt
create_ip -name clk_wiz -vendor xilinx.com -library ip -version 6.0 -module_name video_clock
set clock_ip [get_ips video_clock]
set_property -dict [list CONFIG.PRIM_IN_FREQ 50.0 CONFIG.PRIM_SOURCE No_buffer CONFIG.CLKOUT2_USED true CONFIG.CLKOUT1_REQUESTED_OUT_FREQ 105.0 CONFIG.CLKOUT2_REQUESTED_OUT_FREQ 525.0] $clock_ip
set mult [get_property CONFIG.MMCM_CLKFBOUT_MULT_F $clock_ip]
set div [get_property CONFIG.MMCM_DIVCLK_DIVIDE $clock_ip]
set pixel_div [get_property CONFIG.MMCM_CLKOUT0_DIVIDE_F $clock_ip]
set serial_div [get_property CONFIG.MMCM_CLKOUT1_DIVIDE $clock_ip]
set pixel [expr {50.0*$mult/$div/$pixel_div}]
set serial [expr {50.0*$mult/$div/$serial_div}]
if {abs($pixel-105.0)>0.000001 || abs($serial-525.0)>0.000001} {error "Clock mismatch: $pixel / $serial"}
if {![get_property CONFIG.CLKOUT2_USED $clock_ip]} {error "Missing HDMI serial clock"}
report_property $clock_ip -file $destination/benchmark_clock_properties.rpt
set cf [open $destination/benchmark_clock.txt w]
puts $cf "input_mhz=50.0 pixel_mhz=$pixel serial_mhz=$serial mult=$mult div=$div pixel_div=$pixel_div serial_div=$serial_div"
close $cf
puts "PASS: SYNC_HDMI_CLOCK_REQUEST_AND_READBACK $pixel $serial"
create_ip -name clk_wiz -vendor xilinx.com -library ip -version 6.0 -module_name cpu_clock
set_property -dict [list CONFIG.PRIM_IN_FREQ 50.0 CONFIG.PRIM_SOURCE No_buffer CONFIG.CLKOUT1_REQUESTED_OUT_FREQ 105.0] [get_ips cpu_clock]
set cpu_ip [get_ips cpu_clock]
set cpu_mhz [expr {50.0*[get_property CONFIG.MMCM_CLKFBOUT_MULT_F $cpu_ip]/[get_property CONFIG.MMCM_DIVCLK_DIVIDE $cpu_ip]/[get_property CONFIG.MMCM_CLKOUT0_DIVIDE_F $cpu_ip]}]
if {abs($cpu_mhz-105.0)>0.000001} {error "Independent CPU clock mismatch: $cpu_mhz"}
puts "PASS: INDEPENDENT_CPU_CLOCK_READBACK $cpu_mhz"
# Generate the failing clock products before BD module/IP interpreters exist.
generate_target all [get_ips cpu_clock]
puts "PASS: CPU_CLOCK_PRODUCTS_GENERATED_BEFORE_BD"
# Import a completed2024.2 BD source; regenerate all products in this project.
import_files -norecurse $package/build/ps_sys.bd
set imported_bd [get_files -filter {FILE_TYPE == "Block Designs"}]
if {[llength $imported_bd]!=1} {error "Expected one imported BD"}
open_bd_design $imported_bd
report_ip_status -file $destination/bd_ip_status_before.rpt
set migrated_cells [get_bd_cells -hierarchical -filter {VLNV =~ xilinx.com:ip:*}]
if {[llength $migrated_cells]} {upgrade_bd_cells $migrated_cells}
report_ip_status -file $destination/bd_ip_status_after.rpt
update_module_reference ps_sys_control_0
set_property -dict [list CONFIG.FRAME_WIDTH 800 CONFIG.FRAME_HEIGHT 600] [get_bd_cells control]
foreach pad_port {gamepad_left gamepad_right gamepad_aux gamepad_sequence} {
    create_bd_port -dir O -from 31 -to 0 $pad_port
    connect_bd_net [get_bd_pins control/$pad_port] [get_bd_ports $pad_port]
}
set_property CONFIG.FREQ_HZ 105000000 [get_bd_ports cpu_clk]
set_property CONFIG.PAD_TIMEOUT_CYCLES 105000000 [get_bd_cells control]
puts "INPUT_INTERFACE_BEFORE_PROPAGATION [get_property CONFIG.FREQ_HZ [get_bd_intf_pins control/s_axi]]"
validate_bd_design
puts "INPUT_INTERFACE_AFTER_PROPAGATION [get_property CONFIG.FREQ_HZ [get_bd_intf_pins control/s_axi]]"
if {[get_property CONFIG.FREQ_HZ [get_bd_intf_pins control/s_axi]]!=105000000} {error "Imported module interface frequency mismatch"}
if {[get_property CONFIG.PAD_TIMEOUT_CYCLES [get_bd_cells control]]!=105000000} {error "Imported module timeout mismatch"}
if {[get_property CONFIG.FREQ_HZ [get_bd_ports pixel_clk]]!=105000000} {error "Video clock changed"}
if {[get_property CONFIG.FREQ_HZ [get_bd_ports cpu_clk]]!=105000000} {error "CPU clock mismatch"}
puts "PASS: FROZEN_BD_IMPORTED_PLATFORM_PARAMETERS_REBOUND"

set bd [get_files -filter {FILE_TYPE == "Block Designs"}]
if {[llength $bd]!=1} {error "Expected one BD"}
validate_bd_design
save_bd_design
puts "PASS: PLATFORM_STAGED_FOR_FRESH_SYNTHESIS_SESSION"
close_project
