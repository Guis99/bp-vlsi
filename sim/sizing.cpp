// sizing.cpp — instantiate each predictor and report get_size().
// Target budget: 64 KiB = 65536 bytes = 524288 bits per predictor.
// Tweak the params below and re-run to converge on the target.
//
//   clang++ sizing.cpp -std=c++20 -O2 -o ./bin/sizing && ./bin/sizing
//

#define USE_PHIST_

#include "predictors.h"

#include <array>
#include <cstdio>
#include <string>

#include "predictor_params.h"

static void report(const std::string& label, u64 bits) {
    double bytes = bits / 8.0;
    double kib   = bytes / 1024.0;
    std::printf("%-34s %13llu bits %11.1f B %8.2f KiB   %+7.2f KiB\n",
                label.c_str(), (unsigned long long)bits, bytes, kib,
                (bytes - TARGET_BYTES) / 1024.0);
}

int main() {
    std::printf("%-34s %13s %13s %8s %11s %.1f%5s\n",
                "predictor (params)", "size", "", "", "vs ", NUM_KB, "KiB");
    std::printf("target: %.0f bytes (%.0f bits) per predictor\n\n",
                TARGET_BYTES, TARGET_BYTES * 8);

    {
        BimodalPredictor p(BIM_LOG);
        report("Bimodal(log=" + std::to_string(BIM_LOG) + ")", p.get_size());
    }
    {
        GSharePredictor p(GSH_TBL, GSH_HIST);
        report("GShare(tbl=" + std::to_string(GSH_TBL) + ",h=" + std::to_string(GSH_HIST) + ")", p.get_size());
    }
    {
        TAGEPredictor<TP_H, TP_NC> p(TP_BASE, TP_IDX, TP_TAG, TP_NC, TP_L1, TP_RATIO);
        report("TAGE<H=" + std::to_string(TP_H) + ",NC=" + std::to_string(TP_NC) + ">", p.get_size());
    }
    return 0;
}
