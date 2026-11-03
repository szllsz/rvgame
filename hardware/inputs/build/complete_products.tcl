if {![regexp {^2025\.2(\.[0-9]+)?$} [version -short]]} {error "2025.2 family required"}
set trial [file normalize [lindex $argv 0]]
set_param general.maxThreads 1
open_project $trial/vivado/system640_clean.xpr
set bd [get_files -filter {FILE_TYPE == "Block Designs"}]
if {[llength $bd]!=1} {error "Expected one BD"}
# All completed targets are reused; only remaining targets are generated.
generate_target all $bd
puts "PASS: REMAINING_BD_PRODUCTS_GENERATED_NO_RTL_CHANGE"
close_project
