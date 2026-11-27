`timescale 1ns/1ps
module gpt_tb;
  reg clk=0, rst=0;
  always #10 clk=~clk;
  reg [7:0] aw=0, ar=0;
  reg [31:0] wd=0;
  reg av=0,wv=0,rv=0;
  wire awready,wready,bvalid,arready,rvalid;
  wire [31:0] rd;
  ZyboGPTWrapper dut(.aclk(clk),.aresetn(rst),
    .s_axi_awaddr(aw),.s_axi_awprot(3'b0),.s_axi_awvalid(av),.s_axi_awready(awready),
    .s_axi_wdata(wd),.s_axi_wvalid(wv),.s_axi_wstrb(4'hf),.s_axi_wready(wready),
    .s_axi_bvalid(bvalid),.s_axi_bready(1'b1),
    .s_axi_araddr(ar),.s_axi_arprot(3'b0),.s_axi_arvalid(rv),.s_axi_arready(arready),
    .s_axi_rvalid(rvalid),.s_axi_rready(1'b1),.s_axi_rdata(rd));
  task wr(input [7:0] addr,input [31:0] data);
    @(negedge clk);aw=addr;wd=data;av=1;wv=1;
    @(posedge clk); if(!awready||!wready) $fatal;
    @(negedge clk);av=0;wv=0;wait(bvalid);@(posedge clk);
  endtask
  task rdreg(input [7:0] addr,output [31:0] data);
    @(negedge clk); ar=addr;rv=1;
    @(posedge clk);if(!arready) $fatal;
    @(negedge clk);rv=0;wait(rvalid);data=rd;@(posedge clk);
  endtask
  reg [7:0] golden[0:15];
  string prompt;
  string vector_file;
  reg [31:0] seed;
  integer pos,token,status;
  initial begin
    if (!$value$plusargs("prompt=%s",prompt)) prompt="ROMEO:";
    if (!$value$plusargs("seed=%h",seed)) seed=32'hdeadbeef;
    if (!$value$plusargs("golden=%s",vector_file)) vector_file="golden.hex";
    $readmemh(vector_file,golden);
    repeat(4) @(negedge clk);rst=1;
    wr(36,seed);wr(32,170);
    for(pos=0;pos<prompt.len()+15;pos=pos+1) begin
      if(pos<prompt.len()) token=prompt[pos];
      wr(8,token);wr(16,pos);wr(0,0);wr(0,1);wr(0,0);
      status=0;
      while(!(status & 2)) begin repeat(50) @(posedge clk);rdreg(4,status);end
      rdreg(12,token);
      if(pos>=prompt.len()-1 && token!==golden[pos-prompt.len()+1]) $fatal(1,"pos %0d RTL %0d ref %0d",pos,token,golden[pos-prompt.len()+1]);
    end
    $display("GOLDEN PASS 16");$finish;
  end
  initial #100000000 $fatal(1,"timeout");
endmodule
