proc main {} {
    global argv
    if {![regexp {^2025\.2(\.[0-9]+)?$} [version -short]] || [llength $argv]!=2} {error "Expected own DCP and output"}
    open_checkpoint [lindex $argv 0]
    set out [lindex $argv 1]
    set f [open $out/topology.txt w]
    set heads [get_cells -hier -filter {IS_SEQUENTIAL && NAME =~ u_soc/g_mmio_cdc.u_bridge/g_posted.u_writes/head_payload_reg*}]
    set entries [get_cells -hier -filter {IS_SEQUENTIAL && NAME =~ u_soc/g_mmio_cdc.u_bridge/g_posted.u_writes/entries_reg*}]
    if {[llength $heads]!=68 || [llength $entries]!=272} {error "Actual FIFO dimensions changed"}
    puts $f "COUNTS entries=[llength $entries] heads=[llength $heads]"
    foreach head $heads {
        if {![regexp {head_payload_reg\[([0-9]+)\]$} $head unused bit]} {error "Unexpected head $head"}
        set dp [get_pins -of_objects $head -filter {REF_PIN_NAME == D}]
        set starts [all_fanin -flat -startpoints_only -only_cells $dp]
        puts $f "FANIN bit=$bit cells=$starts"
        set words {}
        foreach pin $starts {
            if {[regexp {entries_reg\[([0-3])\]\[([0-9]+)\]$} $pin unused word sourcebit]} {
                if {$sourcebit!=$bit} {error "Cross-bit FIFO input $pin to $dp"}
                lappend words $word
                set cell $pin
                set cp [get_pins -of_objects $cell -filter {REF_PIN_NAME == C}]
                set timing [get_timing_paths -delay_type max -from $cp -to $dp -max_paths 1]
                if {[llength $timing]!=1} {error "Missing owned FIFO timed path $pin to $dp"}
                set requirement [get_property REQUIREMENT $timing]
                set slack [get_property SLACK $timing]
                if {abs($requirement-8.0)>0.001 || $slack<0} {error "FIFO 8ns bound failed $pin $requirement $slack"}
                puts $f "PATH bit=$bit word=$word requirement=$requirement slack=$slack"
            }
        }
        if {[lsort -integer -unique $words] ne {0 1 2 3}} {error "Missing FIFO entry fanin for bit $bit: $words"}
    }
    close $f
    set f [open $out/mailbox_bits.tsv w]
    foreach prefix {u_soc/g_mmio_cdc.u_bridge/u_mailbox u_ps_video/u_transport} {
        foreach leaf {request_payload_reg dst_request_reg response_payload_reg src_response_reg} {
            foreach cell [get_cells -hier -filter "IS_SEQUENTIAL && NAME =~ $prefix/${leaf}*"] {
                if {![regexp {\[([0-9]+)\](_rep(__[0-9]+)?)?$} $cell unused bit]} {error "Unexpected CDC payload cell: $cell"}
                puts $f "$prefix\t$leaf\t$bit\t$cell"
            }
        }
    }
    close $f
    puts "PASS: ALL272_FIFO_ENTRY_PATHS_TO68_HEAD_BITS_8NS"
    close_project
}
if {[catch {main} message options]} {puts stderr $message;puts stderr [dict get $options -errorinfo];exit 1}
