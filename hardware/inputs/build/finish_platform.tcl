proc finish_platform_locally {} {
    global argv
if {![regexp {^2025\.2(\.[0-9]+)?$} [version -short]]} {error "2025.2 family required"}
set trial [file normalize [lindex $argv 0]]
set package $trial/inputs
set destination $trial/vivado
set_param general.maxThreads 1
open_project $destination/system640_clean.xpr
set bd [get_files -filter {FILE_TYPE == "Block Designs"}]
if {[llength $bd]!=1} {error "Expected one BD"}
open_bd_design $bd
validate_bd_design
save_bd_design
# Existing complete CPU-clock products remain; SystemC simulation is not used
# by RTL regression or implementation. Generate the required synthesis target.
generate_target synthesis $bd
add_files [make_wrapper -files $bd -top]
set_property top top [get_filesets sources_1]
update_compile_order -fileset sources_1
report_ip_status -file $destination/ip_status.rpt
foreach ip [get_ips] {
    if {[get_property IS_LOCKED $ip]} {error "Locked IP: $ip"}
}
set_property strategy Performance_Explore [get_runs impl_1]
add_files -fileset utils_1 [list $package/build/frame640_power.tcl $package/build/accept.tcl]
set_property STEPS.OPT_DESIGN.TCL.PRE $package/build/frame640_power.tcl [get_runs impl_1]
set_property STEPS.PLACE_DESIGN.ARGS.DIRECTIVE Explore [get_runs impl_1]
set_property STEPS.PHYS_OPT_DESIGN.IS_ENABLED true [get_runs impl_1]
set_property STEPS.PHYS_OPT_DESIGN.ARGS.DIRECTIVE Explore [get_runs impl_1]
set_property STEPS.ROUTE_DESIGN.ARGS.DIRECTIVE Default [get_runs impl_1]
set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.IS_ENABLED true [get_runs impl_1]
set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.ARGS.DIRECTIVE AggressiveExplore [get_runs impl_1]
set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.TCL.POST $package/build/accept.tcl [get_runs impl_1]
set taskReportChannel [open $destination/reconstruction.txt w]
puts $taskReportChannel "PART=[get_property PART [current_project]]"
puts $taskReportChannel "TOP=[get_property TOP [get_filesets sources_1]]"
puts $taskReportChannel "BD=[get_property NAME [get_bd_designs]]"
puts $taskReportChannel "IPs=[lsort [get_property IPDEF [get_ips]]]"
puts $taskReportChannel "STAGE=project_and_bd_reconstructed_no_synthesis_yet"
if {$taskReportChannel in {stdin stdout stderr}} {error "Refuse to close standard channel"}
close $taskReportChannel
puts "PASS: SYSTEM640_CLEAN_PROJECT_AND_BD"
close_project

}
if {[catch {finish_platform_locally} message options]} {
 puts stderr $message
 puts stderr [dict get $options -errorinfo]
 exit 1
}
