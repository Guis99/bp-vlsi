#pragma once

// ======================= tweak these to hit 64 KiB =========================
// Shared predictor geometry: consumed by sizing.cpp and by the CBP sweep
// factory (cbp2025/my_cond_branch_predictor.h) so measured size and swept
// configuration never drift apart.
// Bimodal(log2_entries):            entries = 2^log,  2 bits each
constexpr int BIM_LOG   = 18;
// GShare(log2_entries, log2_hist)
constexpr int GSH_TBL   = 18;
constexpr int GSH_HIST  = 14;
// TwoLevel(log2_pc_entries, log2_hist)
constexpr int TL_TBL    = 10;
constexpr int TL_HIST   = 8;
// Perceptron(log2_entries, history_length)
constexpr int PCP_TBL   = 10;
constexpr int PCP_HIST  = 60;
// TAGEPredictor<H, NC>(idx_width, tag_width, num_comp, L1, ratio)  [tag != idx!]
constexpr int TP_H = 2000, TP_NC = 15;
constexpr int TP_IDX = 11, TP_TAG = 11, TP_L1 = 4;
constexpr float TP_RATIO = 1.51f;
// TAGEImproved<N_L, N_U, NC>(idx1, idx2, tag1(short), tag2(long), firstLong, minHist, maxHist, noSkip)
//   mirrors my_cond_branch_predictor.h
constexpr int TI_N_L = 9, TI_N_U = 17, TI_NC = 15;
constexpr int TI_BASE_IW = 13, TI_TAGE_IW = 10, TI_SHORT_TW = 9, TI_LONG_TW = 12;
constexpr int TI_FIRST_LONG = 9, TI_MIN_HIST = 4, TI_MAX_HIST = 2000;
// ===========================================================================

constexpr double TARGET_BYTES = 64.0 * 1024.0;   // 64 KiB
