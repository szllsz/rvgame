set_property PACKAGE_PIN W18 [get_ports audio_din]
set_property PACKAGE_PIN P14 [get_ports audio_bclk]
set_property PACKAGE_PIN Y16 [get_ports audio_lrc]
set_property IOSTANDARD LVCMOS33 [get_ports {audio_din audio_bclk audio_lrc}]
set_property DRIVE 8 [get_ports {audio_din audio_bclk audio_lrc}]
set_property SLEW SLOW [get_ports {audio_din audio_bclk audio_lrc}]
# Registered outputs update data/LRC only on BCLK falling edges (~318 ns setup).
# Audio timing is bound after generated-clock derivation in independent_clock125_constraints.tcl.
# Audio timing is bound after generated-clock derivation in independent_clock125_constraints.tcl.
