# Executed after synthesis, before opt_design, only for the isolated split-clock trial.
# Handshaked bundled payloads are bounded; no blanket asynchronous clock groups.
if {![info exists cdc_report_dir]} {set cdc_report_dir [file normalize [get_property DIRECTORY [current_project]]]}
set destination $cdc_report_dir
set f [open $destination/independent_cdc_endpoints.txt w]
foreach {prefix reply_width} {u_soc/g_mmio_cdc.u_bridge/u_mailbox 32 u_ps_video/u_transport 34} {
    foreach {from_leaf to_leaf maximum} [list request_payload_reg dst_request_reg 69 response_payload_reg src_response_reg $reply_width request_toggle_reg {request_sync_reg[0]} 1 ack_toggle_reg {ack_sync_reg[0]} 1] {
        # Scalar toggle and first-stage synchronizer names are exact, including brackets.
        set from_cells [get_cells -quiet -hier -filter "IS_SEQUENTIAL && NAME =~ $prefix/${from_leaf}*"]
        set to_cells [get_cells -quiet -hier -filter "IS_SEQUENTIAL && NAME =~ $prefix/${to_leaf}*"]
        if {$maximum==1} {
            set from_cells [get_cells -quiet -hier -filter "NAME == $prefix/$from_leaf"]
            set to_cells [get_cells -quiet -hier -filter "NAME == $prefix/$to_leaf"]
        }
        if {![llength $from_cells] || ![llength $to_cells]} {
            error "Missing/expanded independent-clock endpoints $prefix $from_leaf $to_leaf: $from_cells -> $to_cells"
        }
        # Synthesis replicates high-fanout payload bits (e.g. dst_request[38]).
        # Include every replica rather than omitting its crossing constraint.
        if {$maximum>1} {
            foreach {cells leaf} [list $from_cells $from_leaf $to_cells $to_leaf] {
                set pattern [format {^%s\[([0-9]+)\](_rep(__[0-9]+)?)?$} $leaf]
                foreach cell $cells {
                    set tail [file tail $cell]
                    if {![regexp $pattern $tail all bit] || $bit>=$maximum} {error "Unrecognized CDC payload register: $cell"}
                }
            }
        } elseif {[llength $from_cells]!=1 || [llength $to_cells]!=1} {error "Scalar CDC endpoints expanded"}
        set from_pins [get_pins -of_objects $from_cells -filter {REF_PIN_NAME == C}]
        set to_pins [get_pins -of_objects $to_cells -filter {REF_PIN_NAME == D}]
        if {[llength $from_pins]!=[llength $from_cells] || [llength $to_pins]!=[llength $to_cells]} {error "CDC registers must have C/D pins"}
        set_max_delay -datapath_only 8.000 -from $from_pins -to $to_pins
        set_bus_skew 8.000 -from $from_pins -to $to_pins
        puts $f "$prefix $from_leaf -> $to_leaf actual=[llength $from_cells],[llength $to_cells] nominal=$maximum max_delay_ns=8.000"
        puts $f "FROM=$from_cells\nTO=$to_cells"
    }
}
close $f
# Common reset asserts asynchronously at local release synchronizers only.
# Local release-to-functional-flop recovery/removal remains timed.
set release_cells [get_cells -hier -regexp {.*(src_release_reg|dst_release_reg|video_release_reg|reset_sync_reg).*}]
set reset_pins [get_pins -quiet -of_objects $release_cells -filter {REF_PIN_NAME == CLR || REF_PIN_NAME == PRE}]
if {![llength $reset_pins]} {error "No asynchronous reset-release endpoints found"}
set_false_path -to $reset_pins
# Generated-clock names may not exist when early top-level XDC is read.
set video_clock [get_clocks -of_objects [get_pins u_video_clock/clk_out1]]
if {[llength $video_clock]!=1 || abs([get_property PERIOD $video_clock]-1000.0/105.0)>0.002} {error "Video clock missing/wrong"}
set_max_delay 20.000 -from $video_clock -to [get_ports {audio_din audio_bclk audio_lrc}]
set_min_delay 0.000 -from $video_clock -to [get_ports {audio_din audio_bclk audio_lrc}]
report_exceptions -file $destination/independent_cdc_exceptions.rpt
puts "PASS: INDEPENDENT_CPU125_ORDERED_CDC_CONSTRAINTS"

# Additional bounded crossings for the isolated four-entry posted MMIO queue.
# Source Gray bits reach first-stage synchronizers within one fastest period.
# Payload stays owned until the returned read pointer; bound its entire first
# destination stage, including replicated decode consumers. No clock groups.
set fifo_prefix u_soc/g_mmio_cdc.u_bridge/g_posted.u_writes
set fifo_report [open $destination/posted_fifo_cdc_endpoints.txt w]
foreach {source_leaf destination_leaf} {write_gray_reg write_gray_meta_reg read_gray_reg read_gray_meta_reg} {
    set sources [get_cells -quiet -hier -filter "IS_SEQUENTIAL && NAME =~ $fifo_prefix/${source_leaf}*"]
    set sinks [get_cells -quiet -hier -filter "IS_SEQUENTIAL && NAME =~ $fifo_prefix/${destination_leaf}*"]
    if {[llength $sources]!=3 || [llength $sinks]!=3} {error "Posted FIFO Gray endpoint mismatch: $sources -> $sinks"}
    set from [get_pins -of_objects $sources -filter {REF_PIN_NAME == C}]
    set to [get_pins -of_objects $sinks -filter {REF_PIN_NAME == D}]
    set_max_delay -datapath_only 8.000 -from $from -to $to
    set_bus_skew 8.000 -from $from -to $to
    puts $fifo_report "GRAY $sources -> $sinks max_delay_ns=8.000 bus_skew_ns=8.000"
}
set payload [get_cells -quiet -hier -filter "IS_SEQUENTIAL && NAME =~ $fifo_prefix/entries_reg*"]
# Synthesis can remove address bits unused by every peripheral. Enumerate every
# retained register, including replicas; require all four owner words to remain.
set owner_words {}
foreach cell $payload {
    if {![regexp {entries_reg\[([0-3])\]\[([0-9]+)\](_rep(__[0-9]+)?)?$} [file tail $cell] all word bit] || $bit>=68} {
        error "Unrecognized posted payload register: $cell"
    }
    lappend owner_words $word
}
if {[lsort -unique $owner_words] ne {0 1 2 3}} {error "Posted FIFO lost an owner word: $payload"}
set output_pins [get_pins -of_objects $payload -filter {REF_PIN_NAME == Q}]
if {[llength $output_pins]!=[llength $payload]} {error "Posted FIFO payload Q pins missing"}
set endpoints [all_fanout -flat -endpoints_only -from $output_pins]
set destination_pins $endpoints
if {![llength $destination_pins]} {error "Posted payload has no destination endpoints"}
foreach pin $destination_pins {
    set cell [get_cells -of_objects $pin]
    if {[llength $cell]!=1 || ![get_property IS_SEQUENTIAL $cell] ||
        [get_property REF_PIN_NAME $pin] in {C CLK CLKARDCLK CLKBWRCLK CLR PRE R S}} {
        error "Posted payload reaches non-data/control endpoint: $pin"
    }
    set clock_pins [get_pins -of_objects $cell -filter {REF_PIN_NAME == C || REF_PIN_NAME == CLK || REF_PIN_NAME == CLKARDCLK || REF_PIN_NAME == CLKBWRCLK}]
    set clocks [get_clocks -of_objects $clock_pins]
    if {[llength $clocks]!=1 || $clocks ne $video_clock} {error "Posted payload consumer is outside video domain: $pin clocks=$clocks"}
}
set from [get_pins -of_objects $payload -filter {REF_PIN_NAME == C}]
set_max_delay -datapath_only 8.000 -from $from -to $destination_pins
set_bus_skew 8.000 -from $from -to $destination_pins
puts $fifo_report "PAYLOAD source_bits=[llength $payload] destination_bits=[llength $destination_pins] max_delay_ns=8.000 bus_skew_ns=8.000"
puts $fifo_report "FROM=$payload\nTO=$destination_pins"
close $fifo_report
puts "PASS: POSTED_MMIO_EXPLICIT_GRAY_AND_PAYLOAD_BOUNDS"

# Append after the existing posted Gray/payload bounds. Require the first video
# endpoints to be only registered head data; no source payload may reach decode.
if {![info exists destination_pins] || ![llength $destination_pins]} {error "Missing posted payload bounds"}
set head_destinations [get_cells -of_objects $destination_pins]
foreach pin $destination_pins {
    set cell [get_cells -of_objects $pin]
    if {![regexp {^u_soc/g_mmio_cdc\.u_bridge/g_posted\.u_writes/head_payload_reg\[([0-9]+)\](_rep(__[0-9]+)?)?$} $cell all bit] ||
        $bit>=68 || [get_property REF_PIN_NAME $pin] ne "D"} {
        error "Posted source bypasses the video-domain head register: $pin"
    }
}
if {[llength $head_destinations]<68} {error "Missing retained posted head bits: $head_destinations"}
set f [open $destination/posted_fifo_head_boundary.txt w]
puts $f "REGISTERED_HEAD_ONLY destination_bits=[llength $destination_pins] max_delay_ns=8.000 bus_skew_ns=8.000"
puts $f $destination_pins
close $f
puts "PASS: POSTED_SOURCE_PAYLOAD_REACHES_ONLY_REGISTERED_VIDEO_HEAD"
