// tb_folded_csr.sv — self-checking bench for folded_csr.
//
// Run (Verilator v5):
//     verilator --binary --timing -j 0 folded_csr.sv tb_folded_csr.sv
//     ./obj_dir/Vtb_folded_csr
//
// NOTE: exp_seq below is HAND-DERIVED from the C++ recurrence (W=6, CUTOFF=4).
// On the first real run, cross-check against the C++ `csr` golden; if they
// disagree, the C++ is authoritative — fix exp_seq, not the DUT.

module tb_folded_csr;
    localparam int W      = 6;
    localparam int CUTOFF = 4;   // e.g. history length 11: (11-1) % 6 = 4
    localparam int N      = 8;

    logic             clk = 1'b0;
    logic             rst, en, branch, oldest;
    logic [W-1:0]     hash;

    folded_csr #(.W(W), .CUTOFF(CUTOFF)) dut (
        .clk(clk), .rst(rst), .en(en),
        .branch(branch), .oldest(oldest), .hash(hash)
    );

    always #5 clk = ~clk;

    // stimulus
    logic        br_seq  [N] = '{1'b1, 1'b0, 1'b1, 1'b1, 1'b0, 1'b1, 1'b0, 1'b1};
    logic        old_seq [N] = '{1'b0, 1'b0, 1'b0, 1'b1, 1'b0, 1'b1, 1'b0, 1'b0};
    // hand-derived golden hash after each update (starting from hash=0)
    logic [W-1:0] exp_seq [N] = '{6'h01, 6'h02, 6'h05, 6'h2B,
                                  6'h17, 6'h0F, 6'h1E, 6'h3D};

    int errors = 0;

    initial begin
        rst = 1'b1; en = 1'b0; branch = 1'b0; oldest = 1'b0;
        @(posedge clk); #1; rst = 1'b0;      // hash cleared to 0

        for (int i = 0; i < N; i++) begin
            branch = br_seq[i];
            oldest = old_seq[i];
            en     = 1'b1;
            @(posedge clk); #1;              // update latches on this edge
            $display("step %0d: branch=%0b oldest=%0b -> hash=0x%02x (exp 0x%02x) %s",
                     i, branch, oldest, hash, exp_seq[i],
                     (hash === exp_seq[i]) ? "ok" : "MISMATCH");
            if (hash !== exp_seq[i]) errors++;
        end
        en = 1'b0;

        if (errors == 0) $display("PASS: all %0d vectors match", N);
        else             $display("FAIL: %0d mismatch(es)", errors);
        $finish;
    end
endmodule
