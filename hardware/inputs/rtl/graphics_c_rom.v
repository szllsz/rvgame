module graphics_c_rom #(parameter HOST_LOADER=0)(
 input wire clk,input wire [31:0] address,output wire [31:0] instruction,
 input wire [31:0] data_address,output wire [31:0] read_data,
 input wire load_write,input wire [31:0] load_address,input wire [31:0] load_data);
 (* ram_style="block" *) reg [31:0] program [0:1023];
 reg [9:0] instruction_address;
 reg instruction_valid,instruction_valid_d,data_valid;
 reg [31:0] instruction_word,data_word;
 integer i;
 initial for(i=0;i<1024;i=i+1) program[i]=32'h13;
 wire write_valid=HOST_LOADER && load_write && load_address<4096 && load_address[1:0]==0;
 always @(posedge clk) begin
  instruction_address<=address[11:2];instruction_valid<=address<4096;
  instruction_valid_d<=instruction_valid;
  instruction_word<=program[write_valid?load_address[11:2]:instruction_address];
  if(write_valid) program[load_address[11:2]]<=load_data;
  data_word<=program[data_address[11:2]];data_valid<=data_address<4096;
 end
 assign instruction=instruction_valid_d?instruction_word:32'h13;
 assign read_data=data_valid?data_word:32'd0;
endmodule
