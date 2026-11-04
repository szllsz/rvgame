# Export only an already-qualified routed checkpoint; never download or change boot files.
proc main {} {
    global argv
    if {![regexp {^2025\.2(\.[0-9]+)?$} [version -short]] || [llength $argv]!=2} {error "Expected Vivado2025.2 family, trial directory and fresh output directory"}
    set trial [file normalize [lindex $argv 0]]
    set out [file normalize [lindex $argv 1]]
    if {[file exists $out]} {error "Export output must be new"}
    file mkdir $out
    set_param general.maxThreads 1
    open_project $trial/vivado/system640_clean.xpr
    open_checkpoint $trial/acceptance/route_candidate.dcp
    foreach type {max min} {
        set paths [get_timing_paths -delay_type $type -max_paths 1]
        if {[llength $paths]!=1 || [get_property SLACK $paths]<0} {error "Routed timing gate failed: $type"}
    }
    report_drc -file $out/pre_bit_drc.rpt
    if {[llength [get_drc_violations -filter {SEVERITY == Error || SEVERITY == "Critical Warning"}]]!=0} {error "Blocking DRC"}
    report_methodology -file $out/pre_bit_methodology.rpt
    if {[llength [get_drc_violations -filter {SEVERITY == Error || SEVERITY == "Critical Warning"}]]!=0} {error "Blocking methodology"}
    write_bitstream $out/top.bit
    write_hw_platform -fixed -include_bit -file $out/system640.xsa
    puts "PASS: QUALIFIED_CPU_TRIAL_BIT_XSA_EXPORTED_NO_BOARD_NO_BOOT_CHANGE"
    close_project
}
if {[catch {main} message options]} {
    puts stderr $message
    puts stderr [dict get $options -errorinfo]
    exit 1
}
