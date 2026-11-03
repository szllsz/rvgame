# Complete-system timing experiment; stop before bit generation and physical use.
proc main {} {
    global argv
    if {![regexp {^2025\.2(\.[0-9]+)?$} [version -short]] || [llength $argv]!=1} {error "Expected Vivado2025.2 family and isolated trial path"}
    set trial [file normalize [lindex $argv 0]]
    set root [file dirname [file dirname $trial]]
    set package $trial/inputs
    open_project $trial/vivado/system640_clean.xpr
    if {[get_property PART [current_project]] ne "xc7z020clg400-2"} {error "Unexpected FPGA"}
    set bd [get_files -filter {FILE_TYPE == "Block Designs"}]
    open_bd_design $bd
    if {[get_property CONFIG.VIDEO_CDC [get_bd_cells control]]!=1 || [get_property CONFIG.FRAME_HEIGHT [get_bd_cells control]]!=600 || [get_property CONFIG.FRAME_WIDTH [get_bd_cells control]]!=800} {error "PS VIDEO_CDC/frame parameters not applied"}
    if {[get_property CONFIG.FREQ_HZ [get_bd_ports cpu_clk]]!=105000000 || [get_property CONFIG.FREQ_HZ [get_bd_ports pixel_clk]]!=105000000} {error "BD clock frequency mismatch"}
    foreach {pin port} {ps7/S_AXI_HP0_ACLK cpu_clk ps7/S_AXI_HP1_ACLK pixel_clk control/s_axi_aclk cpu_clk} {
        if {[get_bd_nets -of_objects [get_bd_pins $pin]] ne [get_bd_nets -of_objects [get_bd_ports $port]]} {error "Wrong clock: $pin"}
    }
    set_param general.maxThreads 1
    if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {
        launch_runs synth_1 -jobs 1
        wait_on_run synth_1
    }
    if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {error "Independent125 synthesis failed"}
    open_run synth_1
    # Reject async-reset logic reaching BRAM controls before spending a route.
    report_drc -checks [get_drc_checks {REQP-1839 REQP-1840 LUTLP-1}] -file $trial/synth_memory_drc.rpt
    if {[llength [get_drc_violations -quiet]]} {error "Synthesis memory/reset or combinational-loop DRC failed"}
    puts "PASS: SYNTH_BRAM_SYNCHRONOUS_CONTROLS_NO_COMBINATIONAL_LOOPS"
    set dualRF [get_cells -hier -filter {REF_NAME == rv32i_banked_regfile}]
    set secondMEM [get_cells -hier -filter {REF_NAME =~ FD* && NAME =~ *mem_second_result_reg*}]
    set secondWB [get_cells -hier -filter {REF_NAME =~ FD* && NAME =~ *wb_second_data_reg*}]
    if {[llength $dualRF]!=1 || [llength $secondMEM]!=32 || [llength $secondWB]!=32} {error "Mapped dual payload audit failed"}
    puts "PASS: RF_DUAL_ALU_MAPPED rf=1 mem=32 wb=32"
    report_utilization -hierarchical -file $trial/synth_utilization.rpt
    report_cdc -details -file $trial/synth_cdc_unconstrained.rpt
    close_design
    # Keep the qualified work-RAM power exclusion and add only the explicit CDC bounds.
    set hook $trial/independent_opt_pre.tcl
    set f [open $hook w]
    puts $f [list source $package/build/frame640_power.tcl]
    set cdc_hook $package/build/independent_clock125_constraints.tcl
    if {![file exists $cdc_hook]} {set cdc_hook $root/scripts/independent_clock125_constraints.tcl}
    puts $f [list source $cdc_hook]
    close $f
    add_files -fileset utils_1 -norecurse $hook
    set_property STEPS.OPT_DESIGN.TCL.PRE $hook [get_runs impl_1]
    launch_runs impl_1 -to_step route_design -jobs 1
    wait_on_run impl_1
    # Overall progress/status can describe the pending optional post-route step.
    # Verify the completed route checkpoint and actual routed-net counts instead.
    set route_status [get_property STATUS [get_runs impl_1]]
    set routed_checkpoint [get_property DIRECTORY [get_runs impl_1]]/top_routed.dcp
    if {![file exists $routed_checkpoint]} {error "Independent125 routed checkpoint missing: $route_status"}
    # The optional post-route step can leave run metadata "not launched" even
    # though route_design wrote a complete checkpoint; inspect that design.
    open_checkpoint $routed_checkpoint
    set route_report [report_route_status -return_string]
    set routable 0
    set routed 0
    set errors -1
    if {![regexp {# of routable nets[. ]*:[ ]*([0-9]+)} $route_report unused routable] ||
        ![regexp {# of fully routed nets[. ]*:[ ]*([0-9]+)} $route_report unused routed] ||
        ![regexp {# of nets with routing errors[. ]*:[ ]*([0-9]+)} $route_report unused errors] ||
        $routable!=$routed || $routable==0 || $errors!=0} {error "Incomplete routing: $route_report"}
    puts "PASS: INDEPENDENT125_ROUTING_COMPLETE nets=$routed status=$route_status"
    set worst [get_timing_paths -delay_type max -max_paths 1]
    if {[llength $worst]!=1} {error "Missing routed setup path"}
    # Predeclared one postroute pass for this new source.
    phys_opt_design -directive AggressiveExplore
    # Record actual live mailbox payload bits for exact CDC-path reconciliation.
    set sourceBits {};set destinationBits {}
    foreach cell [get_cells -hier -filter {NAME =~ *u_mailbox* && REF_NAME =~ FD*}] {
        set name [get_property NAME $cell]
        if {[regexp {request_payload_reg\[([0-9]+)\]} $name unused bit]} {lappend sourceBits $bit}
        if {[regexp {dst_request_reg\[([0-9]+)\]} $name unused bit]} {lappend destinationBits $bit}
    }
    set taskReport [open $trial/mailbox_request_bits.rpt w]
    puts $taskReport "SOURCE_BITS [lsort -integer -unique $sourceBits]"
    puts $taskReport "DESTINATION_BITS [lsort -integer -unique $destinationBits]"
    close $taskReport
    report_bus_skew -file $trial/independent_bus_skew.rpt
    report_clock_interaction -file $trial/independent_clock_interaction.rpt
    report_exceptions -coverage -file $trial/independent_exception_coverage.rpt
    write_checkpoint $trial/independent_routed.dcp
    # Keep CDC/constraint evidence even when the acceptance script rejects timing.
    # open_checkpoint creates an in-memory project whose DIRECTORY is cwd.
    # Pin the existing acceptance script's output to this isolated trial.
    set accept_file [open $package/build/accept.tcl r]
    set accept_body [read $accept_file]
    close $accept_file
    set output_line {set out [file normalize [get_property DIRECTORY [current_project]]]/acceptance}
    if {[string first $output_line $accept_body]<0} {error "Unexpected acceptance output definition"}
    set accept_body [string map [list $output_line [list set out $trial/acceptance]] $accept_body]
    eval $accept_body
    puts "PASS: INDEPENDENT_CPU125_ROUTE_FINISHED_NO_BIT_NO_BOARD"
    close_project
}
if {[catch {main} message options]} {
    puts stderr $message
    puts stderr [dict get $options -errorinfo]
    exit 1
}
