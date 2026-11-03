`timescale 1ns/1ps
module tb_report_commit;
reg clk=0;always #5 clk=~clk;
reg reset_n=0,enable=1,begin_frame=0,band_valid=0,band_id=0,frame_edge=0,abort_frame=0;
reg copy_done=0,copy_busy=0,bus_busy=0,copy_fault=0;
wire copy_start,copy_band,copy_target_bank,front,display_valid,need_band,active,ready_to_present,presented,failed;
wire [31:0] completed_frames;
native_frame_commit dut(.*);
integer fd=0;reg recording=0;
always @(posedge clk or negedge clk) begin
 #1;
 if(recording)$fdisplay(fd,"%0f,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d",
  $realtime,clk,begin_frame,band_valid,band_id,copy_start,copy_busy,bus_busy,ready_to_present,frame_edge,front,completed_frames,dut.state,presented);
end
task submit_band(input reg id,input integer busy_cycles);
 begin
  if(!need_band || copy_band!=id)$fatal(1,"Unexpected requested band");
  @(negedge clk);band_valid=1;band_id=id;
  @(negedge clk);band_valid=0;
  if(!copy_start)$fatal(1,"No copy request");
  copy_busy=1;bus_busy=1;
  repeat(busy_cycles)@(negedge clk);
  copy_busy=0;bus_busy=0;copy_done=1;
  @(negedge clk);copy_done=0;
 end
endtask
task begin_transaction;
 begin
  @(negedge clk);begin_frame=1;
  @(negedge clk);begin_frame=0;
  if(!need_band || copy_band!=0)$fatal(1,"BEGIN did not request upper band");
 end
endtask
initial begin
 repeat(2)@(negedge clk);reset_n=1;
 // Warm up one complete frame so the recorded transaction preserves an
 // already valid front bank. The copy engine is an explicit stimulus model.
 begin_transaction();submit_band(0,1);submit_band(1,1);
 if(!ready_to_present)$fatal(1,"First frame not ready");
 @(negedge clk);frame_edge=1;
 @(negedge clk);frame_edge=0;
 if(front!=1 || !display_valid || completed_frames!=1)$fatal(1,"Warmup commit failed");
 fd=$fopen("trace.csv","w");if(!fd)$fatal(1,"Trace file not opened");
 $fdisplay(fd,"time_ns,clk,begin_frame,band_valid,band_id,copy_start,copy_busy,bus_busy,ready_to_present,frame_edge,front,completed_frames,state,presented");
 recording=1;
 begin_transaction();repeat(2)@(negedge clk);submit_band(0,3);
 if(front!=1 || completed_frames!=1 || ready_to_present)$fatal(1,"Upper band exposed as frame");
 repeat(2)@(negedge clk);submit_band(1,4);
 if(!ready_to_present || front!=1 || completed_frames!=1)$fatal(1,"READY changed front early");
 repeat(3)@(negedge clk);
 if(front!=1 || completed_frames!=1)$fatal(1,"Frame published without edge");
 frame_edge=1;
 @(negedge clk);frame_edge=0;
 if(front!=0 || completed_frames!=2 || failed)$fatal(1,"Atomic frame swap failed");
 repeat(2)@(negedge clk);recording=0;$fclose(fd);
 $display("PASS REPORT_NATIVE_COMMIT upper_not_presented=1 edge_gated_swap=1 completed_frames=2");$finish;
end
initial begin #10000;$fatal(1,"Controller illustration timed out");end
endmodule
