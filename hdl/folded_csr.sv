// folded_csr.sv — incremental folded-history CSR, one bank's hash.
//
// Bit-exact port of sim/primitives.h  csr::shift_and_fold(branch, oldest):
//     x    = hash ^ (oldest << CUTOFF);     // remove departing history bit
//     rot  = (x << 1) | (x >> (W-1));       // rotate-left-by-1 within W bits
//     hash = (rot ^ branch) & ((1<<W)-1);   // inject new bit at bit 0, keep W bits
//
// One instance per (bank, hash-kind): W=6 for index & primary-tag CSRs,
// W=5 for the secondary-tag CSR. CUTOFF = (history_length - 1) % W.
//
// Verilator (v5):  verilator --binary --timing -j 0 folded_csr.sv tb_folded_csr.sv
//                  ./obj_dir/Vtb_folded_csr

module folded_csr #(
    parameter int W      = 6,   // CSR width in bits
    parameter int CUTOFF = 4    // (history_length - 1) % W
) (
    input  logic           clk,
    input  logic           rst,     // synchronous, active-high: hash <- 0
    input  logic           en,      // 1 = apply one branch update this cycle
    input  logic           branch,  // resolved direction, TAKEN = 1
    input  logic           oldest,  // GHR[hist_len-1], sampled BEFORE the GHR shift
    output logic [W-1:0]   hash
);
    logic [W-1:0] x;          // hash with the departing bit XOR'd out
    logic [W:0]   rot;        // W+1 bits: room for the pre-mask overflow at bit W
    logic [W-1:0] next_hash;

    always_comb begin
        x         = hash ^ (W'(oldest) << CUTOFF);
        // rotate-left-by-1 within W bits: top bit (x[W-1]) wraps into bit 0
        rot       = ({1'b0, x} << 1) | {{W{1'b0}}, x[W-1]};
        // inject the new branch bit at position 0, then keep the low W bits
        next_hash = rot[W-1:0] ^ {{(W-1){1'b0}}, branch};
    end

    always_ff @(posedge clk) begin
        if (rst)      hash <= '0;
        else if (en)  hash <= next_hash;
    end
endmodule
