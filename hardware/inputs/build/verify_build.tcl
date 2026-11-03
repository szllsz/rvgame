proc main {} {
    global argv
    if {![regexp {^2025\.2(\.[0-9]+)?$} [version -short]]} {error "Vivado2025.2 family required"}
    if {[llength $argv]!=2} {error "Expected reconstructed XPR and package root"}
    set xpr [file normalize [lindex $argv 0]]
    set package [file normalize [lindex $argv 1]]
    set project_dir [file dirname $xpr]
    open_project $xpr
    if {[get_property PART [current_project]] ne "xc7z020clg400-2" || [get_property TOP [get_filesets sources_1]] ne "top"} {error "Part/top mismatch"}
    set f [open $project_dir/input_paths.tsv w]
    foreach file [get_files -all] {
        set path [file normalize $file]
        set allowed 0
        foreach base [list $package $project_dir [file normalize $::env(XILINX_VIVADO)]] {
            if {[string first "[string tolower $base]/" [string tolower $path]]==0} {set allowed 1}
        }
        if {!$allowed} {error "External reconstruction input: $path"}
        puts $f "[get_property FILE_TYPE $file]\t$path"
    }
    close $f
    # Retain the original user-XDC order even for the early exported package.
    reorder_files -fileset constrs_1 -front [list $package/constraints/hdmi_out_test.xdc $package/constraints/board.xdc $package/constraints/audio.xdc]
    if {[get_property INCREMENTAL_CHECKPOINT [get_runs synth_1]] ne ""} {error "Unexpected incremental synthesis input"}
    set_param general.maxThreads 1
    launch_runs synth_1 -jobs 1
    wait_on_run synth_1
    if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {error "Clean synthesis failed"}
    puts "PASS: SYSTEM640_SOURCE_ONLY_SYNTHESIS"
    launch_runs impl_1 -to_step write_bitstream -jobs 1
    wait_on_run impl_1
    if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "Clean implementation failed"}
    open_run impl_1
    write_hw_platform -fixed -include_bit -file $project_dir/system640_clean.xsa
    puts "PASS: SYSTEM640_SOURCE_ONLY_IMPLEMENTATION"
    close_project
}
if {[catch {main} message options]} {
    puts stderr $message
    puts stderr [dict get $options -errorinfo]
    exit 1
}
