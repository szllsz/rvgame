# Preserve exact work RAM exclusion and cover newly admitted local program BRAM.
set work_rams [get_cells -hier -filter {REF_NAME == RAMB36E1 && NAME =~ *g_c_program.work_ram_reg*}]
if {[llength $work_rams]!=32} {error "Expected32workRAM"}
set program_rams [get_cells -hier -filter {(REF_NAME == RAMB36E1 || REF_NAME == RAMB18E1) && NAME =~ *g_c_program.u_rom/*}]
if {[llength $program_rams]!=2} {error "Expected2programRAM"}
foreach cell $program_rams {
 if {[get_property REF_NAME $cell] ne "RAMB36E1"} {error "ProgramRAMtype"}
}
set_power_opt -exclude_cells [concat $work_rams $program_rams]
set taskReport [open program_bram_mapping.rpt w]
foreach cell $program_rams {puts $taskReport [report_property -all -return_string $cell]}
close $taskReport
puts "FRAME640_POWER: excluded32work_and2programRAM"
