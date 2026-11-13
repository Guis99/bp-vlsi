#pragma once
// ===========================================================================
// Predictor data collection, gated by the COLLECT_DATA compile flag.
//
// DISTINCT from TAGE_STATS: TAGE_STATS keeps self-contained counters printed at
// terminate(); COLLECT_DATA smuggles time-series data OUT of the predictors to
// a file. ALL collection state and file I/O lives here in one global object,
// OUTSIDE the predictor logic. Predictors hold no collection state -- they only
// touch this through the COLLECT_* macros, which are no-ops unless COLLECT_DATA
// is defined (so a normal build is byte-for-byte unaffected).
//
// Output: a windowed time-series CSV.
//   COLLECT_OUT        output path             (default "collect.csv")
//   COLLECT_WINDOW     branches per emitted row (default 1000000)
//   COLLECT_FP_SAMPLE  1-in-K sampling for the tag false-match check (default 1)
//
// Feature #1 (Phase A): allocation-outcome + u-value distribution snapshot.
// Feature #7 (Phase B): tag false-match rate at the provider, via a wide,
//   tag-independent per-entry fingerprint (hash of pc + real history[0:hl]).
//   On allocation the fingerprint is stored; when a tagged bank later PROVIDES
//   (tag matched), the current fingerprint is compared to the stored one -- a
//   mismatch means the tag matched a *different* context (a true false match).
// Feature #3 (aliasing): destructive-interference rate in the TAGLESS
//   predictors (bimodal/gshare/twolevel). Each SatCounter carries a macro-gated
//   full-PC "owner" field (see primitives.h) set to the last branch that
//   trained it; on the next training access we compare owner vs incoming PC.
//   A different real PC == an aliased (shared) entry. alias_rate =
//   alias_conflict/alias_access, denominator = all training accesses (a cold
//   entry with no prior owner is an access but not a conflict). Exact for
//   bimodal (PC-indexed); for gshare/twolevel (context-indexed) it measures
//   branch-level aliasing and under-counts same-PC/different-history collisions.
// ===========================================================================

#ifdef COLLECT_DATA
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

inline void collect_atexit();   // fwd (registered by lazy_init)

// splitmix64 finalizer -- strong mix so fingerprint collisions are ~2^-64
// (2^53x rarer than the 11-bit tag), and independent of the predictor's fold.
static inline uint64_t collect_mix(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// Default branches-per-window. Override at compile time with
// -DCOLLECT_DEFAULT_WINDOW=N, or per-run via the COLLECT_WINDOW env var.
#ifndef COLLECT_DEFAULT_WINDOW
#define COLLECT_DEFAULT_WINDOW 1000000
#endif

struct Collector {
    std::FILE* out       = nullptr;
    bool       inited    = false;
    uint64_t   window    = COLLECT_DEFAULT_WINDOW;   // branches per emitted row
    uint64_t   win_index = 0;
    uint64_t   win_branches   = 0;
    uint64_t   total_branches = 0;
    // Phase A: per-window allocation outcome
    uint64_t   alloc_attempt = 0, alloc_success = 0;
    // Phase A: per-window u-distribution snapshot (u clamped to 0..3)
    uint64_t   u_hist[4] = {0, 0, 0, 0};
    uint64_t   u_total   = 0;
    // Phase B: per-entry fingerprint shadow (mirrors bank storage), + counters
    std::vector<std::vector<uint64_t>> fp_shadow;
    bool       fp_registered = false;
    uint64_t   fp_sample = 1;          // 1-in-K sampling of the provider check
    uint64_t   fp_calls  = 0;
    // Phase B: tag comparison at the provider (a "tag alias" is a tag that
    // matched a *different* context). access = tag comparisons, collision = false matches.
    uint64_t   tag_alias_access = 0, tag_alias_collision = 0;   // per-window
    // Feature #3: tagless-predictor aliasing (per-window)
    uint64_t   alias_access = 0, alias_conflict = 0;
    // Feature #3c / aliasing funnel (per-window). Reuses fp_shadow. Cleanly
    // nested: idx_collision_access (all allocated probes) >= idx_collision
    // (a different (pc,hist) context occupies this index) >= idx_tag_collision
    // (that different context ALSO matches the tag -> "both collide").
    uint64_t   idx_collision_access = 0, idx_collision = 0, idx_tag_collision = 0;
    // Per-window alt-on-new: times the alt prediction was used, and of those,
    // times it was correct.
    uint64_t   alt_attempt = 0, alt_success = 0;
    // Feature #4: provider distribution (per-window), binned by the PROVIDER's
    // history length so TAGE and TAGEImproved compare on a fair axis (their
    // logical bank counts differ). Exactly one increment per branch. Bin 0 =
    // the history-less base predictor; bins 1..10 are log2-ish hist ranges.
    // Bins: 0 = history-less base, then {1-10, 11-30, 31-100, 101-300, 300+}.
    static constexpr int PROV_BINS = 6;
    uint64_t   prov_bin[PROV_BINS] = {0};       // providers this window, per bin
    uint64_t   prov_miss_bin[PROV_BINS] = {0};  // of those, mispredicts, per bin
    // of those, times the provider disagreed with the alt AND was correct --
    // the unique correct contribution of the provider's (longer) history.
    uint64_t   prov_altwin_bin[PROV_BINS] = {0};
    static int prov_bin_of(uint64_t hl) {
        if (hl == 0)    return 0;
        if (hl <= 10)   return 1;
        if (hl <= 30)   return 2;
        if (hl <= 100)  return 3;
        if (hl <= 300)  return 4;
        return 5;
    }

    void lazy_init() {
        if (inited) return;
        inited = true;
        const char* path = std::getenv("COLLECT_OUT");
        out = std::fopen(path ? path : "collect.csv", "w");
        if (const char* w = std::getenv("COLLECT_WINDOW")) {
            uint64_t v = std::strtoull(w, nullptr, 10);
            if (v) window = v;
        }
        if (const char* k = std::getenv("COLLECT_FP_SAMPLE")) {
            uint64_t v = std::strtoull(k, nullptr, 10);
            if (v) fp_sample = v;
        }
        if (out) {
            std::fprintf(out, "window,branches,alloc_attempt,alloc_success,"
                              "alloc_success_rate,u0,u1,u2,u3,"
                              "tag_alias_access,tag_alias_collision,tag_alias_rate,"
                              "alias_access,alias_conflict,alias_rate,"
                              "idx_collision_access,idx_collision,idx_collision_rate,"
                              "idx_tag_collision,alt_attempt,alt_success,"
                              "prov_h0,prov_1_10,prov_11_30,prov_31_100,prov_101_300,prov_300p,"
                              "provmr_h0,provmr_1_10,provmr_11_30,provmr_31_100,provmr_101_300,provmr_300p,"
                              "provben_h0,provben_1_10,provben_11_30,provben_31_100,provben_101_300,provben_300p\n");
            std::fflush(out);
        }
        std::atexit(&collect_atexit);
    }

    void record_alloc(bool ok) {
        lazy_init();
        ++alloc_attempt;
        if (ok) ++alloc_success;
    }

    bool tick() {
        lazy_init();
        ++total_branches;
        return (++win_branches >= window);
    }

    void record_u(int u) {
        int b = (u < 0) ? 0 : (u > 3 ? 3 : u);
        ++u_hist[b];
        ++u_total;
    }

    // Feature #4: called once per branch with the provider's history length
    // (0 == the history-less base predictor) and whether the served prediction
    // missed (attributed to the provider bin, matching _stat_prov_miss).
    void record_provider(uint64_t hl, bool miss, bool altwin) {
        lazy_init();
        int b = prov_bin_of(hl);
        ++prov_bin[b];
        if (miss)   ++prov_miss_bin[b];
        if (altwin) ++prov_altwin_bin[b];   // provider != alt AND provider correct
    }

    // ---- Feature #3: tagless aliasing ----
    // owner == the PC that last trained this counter (~0ull if never trained).
    void record_alias(uint64_t owner, uint64_t pc) {
        lazy_init();
        ++alias_access;
        if (owner != ~0ull && owner != pc) ++alias_conflict;
    }

    // ---- Phase B: fingerprint shadow ----
    void fp_register(const std::vector<size_t>& sizes) {
        if (fp_registered) return;
        fp_registered = true;
        for (size_t s : sizes) fp_shadow.emplace_back(s, 0ull);
    }
    void fp_store(int bank, size_t idx, uint64_t fp) {
        if (bank >= 0 && bank < (int)fp_shadow.size() && idx < fp_shadow[bank].size())
            fp_shadow[bank][idx] = fp;
    }
    // Called when a tagged bank provides (tag matched). Counts it as a false
    // match if the current context fingerprint differs from the one stored at
    // allocation.
    void fp_check_provider(int bank, size_t idx, uint64_t fp) {
        lazy_init();
        if (fp_sample > 1 && (++fp_calls % fp_sample)) return;
        ++tag_alias_access;
        if (bank >= 0 && bank < (int)fp_shadow.size() && idx < fp_shadow[bank].size()) {
            if (fp_shadow[bank][idx] != fp) ++tag_alias_collision;
        }
    }

    // ---- Feature #3c: index collision (independent of the tag) ----
    // Called on every probe of an ALLOCATED tagged-bank entry. If the current
    // context fingerprint differs from the one that allocated the entry (stored
    // in fp_shadow), a *different* context occupies this index slot -- an index
    // collision. tag_alias_collision (Phase B) is the subset that ALSO matched
    // the tag, so tag_alias_collision/idx_collision quantifies the tag's filtering. `fp` and
    // `allocated` are evaluated by the caller only in a COLLECT build (the macro
    // discards them otherwise), so a normal build never pays for either.
    void idx_check(int bank, size_t idx, bool allocated, uint64_t fp, bool tag_match) {
        if (!allocated) return;
        lazy_init();
        if (bank < 0 || bank >= (int)fp_shadow.size() || idx >= fp_shadow[bank].size()) return;
        ++idx_collision_access;                         // probe of an allocated entry
        if (fp_shadow[bank][idx] != fp) {               // different context at this index
            ++idx_collision;
            if (tag_match) ++idx_tag_collision;         // ...whose tag also matches -> both collide
        }
    }

    // Per-window alt-on-new: `used` = the alt prediction was served instead of
    // the provider; `correct` = that alt prediction matched the outcome.
    void record_alt(bool used, bool correct) {
        if (!used) return;
        ++alt_attempt;
        if (correct) ++alt_success;
    }

    void emit() {
        if (out) {
            double arate = alloc_attempt ? (double)alloc_success / (double)alloc_attempt : 0.0;
            double d     = u_total ? (double)u_total : 1.0;
            double frate = tag_alias_access ? (double)tag_alias_collision / (double)tag_alias_access : 0.0;
            double alrate = alias_access ? (double)alias_conflict / (double)alias_access : 0.0;
            double ixrate = idx_collision_access ? (double)idx_collision / (double)idx_collision_access : 0.0;
            std::fprintf(out, "%llu,%llu,%llu,%llu,%.4f,%.4f,%.4f,%.4f,%.4f,%llu,%llu,%.5f,%llu,%llu,%.5f,%llu,%llu,%.5f,%llu,%llu,%llu",
                         (unsigned long long)win_index,
                         (unsigned long long)win_branches,
                         (unsigned long long)alloc_attempt,
                         (unsigned long long)alloc_success,
                         arate,
                         u_hist[0] / d, u_hist[1] / d, u_hist[2] / d, u_hist[3] / d,
                         (unsigned long long)tag_alias_access,
                         (unsigned long long)tag_alias_collision,
                         frate,
                         (unsigned long long)alias_access,
                         (unsigned long long)alias_conflict,
                         alrate,
                         (unsigned long long)idx_collision_access,
                         (unsigned long long)idx_collision,
                         ixrate,
                         (unsigned long long)idx_tag_collision,
                         (unsigned long long)alt_attempt,
                         (unsigned long long)alt_success);
            // Feature #4: provider distribution as fractions (stack to ~1) ...
            uint64_t prov_total = 0;
            for (int b = 0; b < PROV_BINS; b++) prov_total += prov_bin[b];
            double pd = prov_total ? (double)prov_total : 1.0;
            for (int b = 0; b < PROV_BINS; b++)
                std::fprintf(out, ",%.4f", (double)prov_bin[b] / pd);
            // ... then each bin's miss rate (miss/providers; 0 when unused).
            for (int b = 0; b < PROV_BINS; b++)
                std::fprintf(out, ",%.4f",
                             prov_bin[b] ? (double)prov_miss_bin[b] / (double)prov_bin[b] : 0.0);
            // ... then each bin's unique-correct-override share (altwin / all branches).
            for (int b = 0; b < PROV_BINS; b++)
                std::fprintf(out, ",%.4f", (double)prov_altwin_bin[b] / pd);
            std::fputc('\n', out);
            std::fflush(out);
        }
        ++win_index;
        win_branches  = 0;
        alloc_attempt = alloc_success = 0;
        u_hist[0] = u_hist[1] = u_hist[2] = u_hist[3] = 0;
        u_total   = 0;
        tag_alias_access = tag_alias_collision = 0;
        alias_access = alias_conflict = 0;
        idx_collision_access = idx_collision = idx_tag_collision = 0;
        alt_attempt = alt_success = 0;
        for (int b = 0; b < PROV_BINS; b++) prov_bin[b] = prov_miss_bin[b] = prov_altwin_bin[b] = 0;
    }

    void finish() {
        if (out) { std::fflush(out); std::fclose(out); out = nullptr; }
    }
};

inline Collector g_collector;
inline void collect_atexit() { g_collector.finish(); }

#define COLLECT_ALLOC(ok) (g_collector.record_alloc((ok)))
#define COLLECT_SNAPSHOT(banks)                                          \
    do {                                                                 \
        if (g_collector.tick()) {                                        \
            for (auto& _bk : (banks))                                    \
                for (auto& _e : _bk) g_collector.record_u((_e).get_u()); \
            g_collector.emit();                                          \
        }                                                                \
    } while (0)
#define COLLECT_FP_REGISTER(...)        (g_collector.fp_register(__VA_ARGS__))
#define COLLECT_FP_STORE(bank, idx, fp) (g_collector.fp_store((bank), (idx), (fp)))
#define COLLECT_FP_CHECK(bank, idx, fp) (g_collector.fp_check_provider((bank), (idx), (fp)))
// Feature #3c: index-collision probe. `allocated` and `fp` are evaluated only
// here (COLLECT build); the no-op form below discards them, so a normal build
// never calls is_allocated()/_collect_fp() at the probe site.
#define COLLECT_IDX_CHECK(bank, idx, allocated, fp, tag_match) \
    (g_collector.idx_check((bank), (idx), (allocated), (fp), (tag_match)))
// Feature #4: record the provider's history length (0 == base) and whether the
// served prediction missed, for this branch.
#define COLLECT_PROVIDER(hl, miss, altwin) (g_collector.record_provider((uint64_t)(hl), (bool)(miss), (bool)(altwin)))
// Per-window alt-on-new: `used` = alt served instead of provider; `correct` = it matched.
#define COLLECT_ALT(used, correct) (g_collector.record_alt((bool)(used), (bool)(correct)))
// Feature #3: advance the window from a predictor with no banks (tagless).
// TAGE variants advance it via COLLECT_SNAPSHOT instead; call exactly one per branch.
#define COLLECT_TICK()                                                   \
    do { if (g_collector.tick()) g_collector.emit(); } while (0)
// Feature #3: compare the counter's stored owner-PC against the incoming PC,
// tally, then take ownership. Call at the training (update) access site.
#define COLLECT_ALIAS(counter, pc)                                       \
    do {                                                                 \
        g_collector.record_alias((counter).collect_owner(), (uint64_t)(pc)); \
        (counter).collect_set_owner((uint64_t)(pc));                     \
    } while (0)
// Context-aware variant: fingerprint the full (pc, history) context so gshare's
// same-PC/different-history collisions are counted. PC-only (COLLECT_ALIAS) is
// exact only when the history bits are a subset of the index bits (hist <= table
// log); above that the fingerprint is needed. mix is bijective, so for bimodal
// (hist==0) this reduces to an exact per-PC owner.
#define COLLECT_ALIAS_CTX(counter, pc, hist)                             \
    do {                                                                 \
        uint64_t _fp = collect_mix((uint64_t)(pc)) ^ collect_mix((uint64_t)(hist) + 1); \
        g_collector.record_alias((counter).collect_owner(), _fp);        \
        (counter).collect_set_owner(_fp);                                \
    } while (0)

#else  // COLLECT_DATA disabled -> compiles to nothing
#define COLLECT_ALLOC(ok)                ((void)0)
#define COLLECT_SNAPSHOT(banks)          ((void)0)
#define COLLECT_FP_REGISTER(...)         ((void)0)
#define COLLECT_FP_STORE(bank, idx, fp)  ((void)0)
#define COLLECT_FP_CHECK(bank, idx, fp)  ((void)0)
#define COLLECT_IDX_CHECK(bank, idx, allocated, fp, tag_match)  ((void)0)
#define COLLECT_PROVIDER(hl, miss, altwin)  ((void)0)
#define COLLECT_ALT(used, correct)       ((void)0)
#define COLLECT_TICK()                   ((void)0)
#define COLLECT_ALIAS(counter, pc)       ((void)0)
#define COLLECT_ALIAS_CTX(counter, pc, hist)  ((void)0)
#endif
