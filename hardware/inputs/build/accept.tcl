set root [file dirname [file dirname [file normalize [info script]]]]
set out [file normalize [get_property DIRECTORY [current_project]]]/acceptance
file mkdir $out
write_checkpoint -force $out/route_candidate.dcp
report_timing -delay_type max -max_paths 15 -path_type full_clock_expanded -file $out/critical_paths.rpt
report_timing_summary -delay_type min_max -report_unconstrained -file $out/timing.rpt
report_utilization -file $out/utilization.rpt
report_utilization -hierarchical -file $out/utilization_hier.rpt
report_route_status -file $out/route.rpt
report_drc -file $out/drc.rpt
report_cdc -details -file $out/cdc.rpt
report_methodology -file $out/methodology.rpt
check_timing -verbose -file $out/check_timing.rpt
report_timing -delay_type max -to [get_ports {audio_din audio_bclk audio_lrc}] -max_paths 3 -file $out/audio_outputs_max.rpt
report_timing -delay_type min -to [get_ports {audio_din audio_bclk audio_lrc}] -max_paths 3 -file $out/audio_outputs_min.rpt
foreach port {audio_din audio_bclk audio_lrc} {
    foreach type {max min} {
        set paths [get_timing_paths -delay_type $type -to [get_ports $port] -max_paths 1]
        if {[llength $paths]!=1 || [get_property SLACK [lindex $paths 0]]<0} {error "Invalid $port $type timing"}
    }
}
foreach type {max min} {
    set paths [get_timing_paths -delay_type $type -max_paths 1]
    if {![llength $paths] || [get_property SLACK [lindex $paths 0]]<0} {error "Invalid $type timing"}
}
if {[llength [get_drc_violations -filter {SEVERITY == Error}]]} {error "DRC errors"}
puts "PASS: SYSTEM640_ROUTED_SETUP_HOLD_DRC"
