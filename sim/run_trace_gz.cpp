// run_trace_gz.cpp — self-contained driver.
//
// Loads a CBP2025 .gz trace, filters it to just its branches, and runs one
// Bimodal, one GShare, and one TAGE predictor over the conditional branches,
// reporting accuracy / mispredicts / MPKI for each.
//
// Self-contained: the only dependency beyond this directory's own headers is
// zlib (-lz). The trace is decoded inline (see GzBranchReader) straight from
// the gzip stream — no external trace-reader or gzstream needed.
//
// Build (standalone; does not need the CMake targets):
//     g++ -std=c++20 -O2 run_trace_gz.cpp -lz -o build/run_trace_gz
//
// Run:
//     build/run_trace_gz <trace.gz> [--limit N]
//       --limit N   stop after reading N instructions (0 = whole trace)
//
// Predictor geometry comes from predictor_params.h, so this driver stays in
// lock-step with sizing.cpp (same sizes).

#include "predictors.h"
#include "predictor_params.h"

#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <array>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// CBP2025 trace wire format (little-endian, variable-length, one record per
// committed instruction; no header). Field order per record:
//   PC(8) type(1)
//   if load/store:  EA(8) size(1) baseUpd(1)   (+ regOffset(1) if store)
//   if branch:      taken(1)                    (+ target(8) if taken)
//   numIn(1)  inRegs(1 each)
//   numOut(1) outRegs(1 each)
//   outValues: 8 bytes per INT reg, 16 bytes per SIMD reg (id 32..63)
// Conditional branch = type 3; that is the only class a direction predictor
// sees. This matches cbp2025/lib/trace_reader.h exactly.
// ---------------------------------------------------------------------------
enum : uint8_t {
    CLS_ALU = 0, CLS_LOAD = 1, CLS_STORE = 2, CLS_CONDBR = 3,
    CLS_UNCOND_DIRECT = 4, CLS_UNCOND_INDIRECT = 5, CLS_FP = 6, CLS_SLOWALU = 7,
    CLS_UNDEF = 8, CLS_CALL_DIRECT = 9, CLS_CALL_INDIRECT = 10, CLS_RETURN = 11,
};

static inline bool is_branch_class(uint8_t c) {
    return c == CLS_CONDBR || c == CLS_UNCOND_DIRECT || c == CLS_UNCOND_INDIRECT ||
           c == CLS_CALL_DIRECT || c == CLS_CALL_INDIRECT || c == CLS_RETURN;
}
// INT regs: 0..31, 64 (flags), 65 (zero). SIMD/FP regs: 32..63 (value is 16 B).
static inline bool reg_is_int(uint8_t r) { return r < 32 || r == 64 || r == 65; }

// Buffered byte source over a gzip stream (chunked gzread, served byte-wise).
class GzReader {
    gzFile f_;
    std::vector<uint8_t> buf_;
    size_t pos_ = 0, len_ = 0;
    bool refill() {
        int n = gzread(f_, buf_.data(), (unsigned)buf_.size());
        if (n <= 0) { len_ = pos_ = 0; return false; }
        len_ = (size_t)n; pos_ = 0; return true;
    }
public:
    explicit GzReader(const char* path) : buf_(1 << 20) {
        f_ = gzopen(path, "rb");
        if (!f_) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(1); }
    }
    ~GzReader() { if (f_) gzclose(f_); }
    // Copy n bytes; return false on a clean EOF before any byte of this call.
    bool get(void* dst, size_t n) {
        uint8_t* out = (uint8_t*)dst; size_t got = 0;
        while (got < n) {
            if (pos_ == len_ && !refill()) return false;  // EOF (clean or truncated)
            size_t take = std::min(n - got, len_ - pos_);
            if (dst) std::memcpy(out + got, buf_.data() + pos_, take);
            pos_ += take; got += take;
        }
        return true;
    }
    bool skip(size_t n) { return get(nullptr, n); }
};

// One decoded branch event the predictors care about.
struct BranchEvent { uint64_t pc; bool taken; bool conditional; };

// Pull the next *branch* out of the trace, skipping non-branch instructions.
// Returns false at end of trace. `ninst_out` counts every instruction skipped
// or consumed (for MPKI's denominator).
class GzBranchReader {
    GzReader r_;
public:
    uint64_t ninst = 0;
    explicit GzBranchReader(const char* path) : r_(path) {}

    bool next_branch(BranchEvent& ev) {
        for (;;) {
            uint64_t pc;
            if (!r_.get(&pc, 8)) return false;           // clean EOF at a record boundary
            uint8_t type;
            if (!r_.get(&type, 1)) return false;
            ninst++;

            bool is_cond = false, taken = false, is_br = is_branch_class(type);
            if (type == CLS_LOAD || type == CLS_STORE) {
                r_.skip(8 + 1 + 1);                       // EA, size, baseUpd
                if (type == CLS_STORE) r_.skip(1);        // regOffset
            } else if (is_br) {
                uint8_t tk; r_.get(&tk, 1); taken = tk != 0;
                if (taken) r_.skip(8);                    // target
                is_cond = (type == CLS_CONDBR);
            }

            uint8_t num_in; r_.get(&num_in, 1);
            if (num_in) r_.skip(num_in);                  // input reg names

            uint8_t num_out; r_.get(&num_out, 1);
            uint8_t out_regs[256];
            if (num_out) r_.get(out_regs, num_out);       // output reg names
            for (uint8_t i = 0; i < num_out; i++)         // output reg values
                r_.skip(reg_is_int(out_regs[i]) ? 8 : 16);

            if (is_br) {
                ev = {pc, taken, is_cond};
                return true;
            }
            // else: non-branch instruction, keep scanning
        }
    }
};

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <trace.gz> [--limit N]\n", argv[0]);
        return 2;
    }
    const char* path = argv[1];
    uint64_t limit = 0;
    for (int i = 2; i < argc; i++) {
        if (!std::strcmp(argv[i], "--limit") && i + 1 < argc)
            limit = std::strtoull(argv[++i], nullptr, 10);
        else { std::fprintf(stderr, "unknown arg: %s\n", argv[i]); return 2; }
    }

    // One of each predictor, sized from predictor_params.h (same as sizing.cpp).
    std::printf("constructing predictors (geometry from predictor_params.h)...\n");
    std::vector<std::unique_ptr<BranchPredictorBase>> preds;
    preds.emplace_back(std::make_unique<BimodalPredictor>(BIM_LOG));
    preds.emplace_back(std::make_unique<GSharePredictor>(GSH_TBL, GSH_HIST));
    preds.emplace_back(std::make_unique<TAGEPredictor<TP_H, TP_NC>>(
                           TP_BASE, TP_IDX, TP_TAG, TP_NC, TP_L1, TP_RATIO));

    std::vector<uint64_t> misp(preds.size(), 0);
    uint64_t ncond = 0, nuncond = 0;

    GzBranchReader reader(path);
    BranchEvent ev;
    while (reader.next_branch(ev)) {
        if (ev.conditional) {
            // predict-then-update, in program order, conditional branches only
            for (size_t k = 0; k < preds.size(); k++) {
                bool p = preds[k]->predict(ev.pc);
                if (p != ev.taken) misp[k]++;
                preds[k]->update(ev.pc, ev.taken ? BranchResult::TAKEN : BranchResult::NOT_TAKEN);
            }
            ncond++;
        } else {
            nuncond++;   // unconditional branches: always taken, nothing to predict
        }
        if (limit && reader.ninst >= limit) break;
    }

    const uint64_t ninst = reader.ninst;
    std::printf("\ntrace            : %s\n", path);
    std::printf("instructions     : %llu\n", (unsigned long long)ninst);
    std::printf("conditional br.  : %llu   (fed to predictors)\n", (unsigned long long)ncond);
    std::printf("unconditional br.: %llu   (skipped: no direction to predict)\n",
                (unsigned long long)nuncond);
    std::printf("\n%-10s %12s %10s %9s %9s %9s\n",
                "predictor", "size(B)", "misp", "mispr%", "acc%", "MPKI");
    for (size_t k = 0; k < preds.size(); k++) {
        double mr   = ncond ? 100.0 * misp[k] / ncond : 0.0;
        double mpki = ninst ? 1000.0 * misp[k] / ninst : 0.0;
        std::printf("%-10s %12llu %10llu %8.4f%% %8.4f%% %9.4f\n",
                    preds[k]->predictor_name().c_str(),
                    (unsigned long long)(preds[k]->get_size() / 8),
                    (unsigned long long)misp[k], mr, 100.0 - mr, mpki);
    }
    return 0;
}
