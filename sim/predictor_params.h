#pragma once

// ======================= tweak these to hit 2 KiB =========================
// Shared predictor geometry: consumed by sizing.cpp and by the CBP sweep
// factory (cbp2025/my_cond_branch_predictor.h) so measured size and swept
// configuration never drift apart.
// Bimodal(log2_entries):            entries = 2^log,  2 bits each
constexpr int BIM_LOG   = 12;
// GShare(log2_entries, log2_hist)
constexpr int GSH_TBL   = 12;
constexpr int GSH_HIST  = 10;
// TwoLevel(log2_pc_entries, log2_hist)
constexpr int TL_TBL    = 8;
constexpr int TL_HIST   = 6;
// Perceptron(log2_entries, history_length)
constexpr int PCP_TBL   = 8;
constexpr int PCP_HIST  = 40;
// TAGEPredictor<H, NC>(idx_width, tag_width, num_comp, L1, ratio)  [tag != idx!]
constexpr int TP_H = 300, TP_NC = 8;
constexpr int TP_BASE = 9;
constexpr int TP_IDX = 6, TP_TAG = 6, TP_L1 = 4;
constexpr float TP_RATIO = 1.7f;
// ===========================================================================

constexpr double NUM_KB = 1.0;
constexpr double TARGET_BYTES = NUM_KB * 1024.0;   // 64 KiB
