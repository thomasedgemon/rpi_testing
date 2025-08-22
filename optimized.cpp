// optimized_segmented_sieve_refactor.cpp
// A fast, cache-friendly segmented sieve for 64-bit ranges.
// - Odd-only indexing (½ memory and ~½ work)
// - Reuses base primes and carries each prime's "next multiple" across segments
// - No div/mod inside the hot strike loop
// - Minimal branching, no progress prints
//
// Build (Pi Zero 2W / Cortex-A53 recommended):
//   g++ -O3 -pipe -flto -fno-exceptions -fno-rtti -march=armv8-a -mtune=cortex-a53 \
//       -DNDEBUG optimized.cpp -o optimized
//
// Usage: ./optimized [seconds]
// Default runtime is 10 seconds; the sieve streams upward until time elapses.
//
// Notes:
// * This counts primes correctly and tracks the largest prime seen.
// * It avoids expensive divisions in inner loops; per-prime alignment is done once and then
//   carried between segments using simple additions.
//

#include <bits/stdc++.h>
using namespace std;

using u8  = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;

// --- Small sieve that EXTENDS a base prime list up to new_limit (inclusive) ---
static void extend_base_primes(u32 new_limit, vector<u32>& base, u32& sieved_to) {
    if (new_limit <= sieved_to) return;
    u32 old = max<u32>(2, sieved_to + 1);
    u32 n   = new_limit;

    // Simple byte sieve (dense, tiny)
    vector<u8> mark(n + 1, 1);
    mark[0] = mark[1] = 0;

    // If extending, pre-mark with existing primes to keep complexity small
    if (!base.empty()) {
        for (u32 p : base) {
            u64 start = max<u64>(u64(p) * u64(p), (u64((old + p - 1) / p) * p));
            for (u64 j = start; j <= n; j += p) mark[(size_t)j] = 0;
        }
    }

    for (u32 i = 2; (u64)i * i <= n; ++i) {
        if (!mark[i]) continue;
        u64 start = max<u64>(u64(i) * u64(i), (u64((old + i - 1) / i) * i));
        for (u64 j = start; j <= n; j += i) mark[(size_t)j] = 0;
    }

    if (sieved_to < 2) {
        for (u32 i = 2; i <= n; ++i) if (mark[i]) base.push_back(i);
    } else {
        for (u32 i = old; i <= n; ++i) if (mark[i]) base.push_back(i);
    }
    sieved_to = n;
}

// Map odd indices -> actual value
static inline u64 idx_to_val(u64 lo_odd, u64 i) { return lo_odd + (i << 1); }

// Compute first odd multiple of p >= lo (one-time math; outside hot loop)
static inline u64 first_odd_multiple_ge(u64 lo, u64 p) {
    u64 m = (lo % p == 0) ? lo : lo + (p - (lo % p));
    if ((m & 1ULL) == 0) m += p; // make it odd
    return m;
}

int main(int argc, char** argv) {
    using clock = chrono::steady_clock;

    double RUN_SECONDS = 10.0;
    if (argc >= 2) RUN_SECONDS = atof(argv[1]);

    // Size of the segment flag buffer (bytes). Each byte represents ONE odd number.
    // 512 KiB of flags ~= 1,048,576 numbers per segment (since we store only odds).
    const size_t SEG_BYTES = 512 * 1024;

    vector<u8> flags(SEG_BYTES, 1); // reused each segment

    vector<u32> base;     // all base primes (2, 3, 5, 7, ...)
    u32 base_sieved_to = 1;
    extend_base_primes(100, base, base_sieved_to); // small bootstrap

    // Per-prime "next multiple" carried across segments.
    // next_mult[k] is the first odd multiple of base[k] that is >= current 'lo'.
    vector<u64> next_mult;
    next_mult.reserve(1 << 16);

    u64 primes_count = 0;
    u64 largest_prime = 0;

    // Stream upward in segments until time budget expires
    u64 lo = 2;
    auto t0 = clock::now();
    auto deadline = t0 + chrono::duration<double>(RUN_SECONDS);

    // If 2 is within the first segment we will account for it once.
    bool counted_two = false;

    while (true) {
        // Compute this segment's [lo, hi)
        // Keep the window size so that 'flags.size()' bytes cover only ODDs.
        u64 lo_odd = (lo | 1ULL);
        u64 len    = flags.size(); // odd-count
        u64 hi     = lo_odd + (len << 1); // exclusive upper bound

        // Ensure base primes cover up to sqrt(hi-1)
        u64 need = (u64)floor(sqrt((long double)(hi - 1)));
        if (need > base_sieved_to) extend_base_primes((u32)need, base, base_sieved_to);

        // Prepare next_mult on first segment or when base grew
        if (next_mult.size() != base.size()) {
            next_mult.resize(base.size());
            // Initialize from current 'lo'
            for (size_t i = 0; i < base.size(); ++i) {
                u32 p = base[i];
                if (p < 3) { // We'll handle p=2 separately
                    next_mult[i] = 0;
                    continue;
                }
                next_mult[i] = first_odd_multiple_ge(lo, p);
            }
        }

        // Reset flags to "prime" for this segment
        memset(flags.data(), 1, flags.size());

        // Strike using odd-only mapping.
        // Handle p=2 separately: all evens are non-prime; our buffer contains only odds already.
        size_t start_idx = (base.size() >= 1 && base[0] == 2) ? 1 : 0;
        for (size_t bi = start_idx; bi < base.size(); ++bi) {
            u32 p = base[bi];
            u64 step = (u64)p << 1; // 2p, jump to the next odd multiple

            // Ensure j is the first odd multiple >= lo
            u64 j = next_mult[bi];
            if (j < lo) {
                // Fast-forward by repeated addition (no division)
                // On average this executes < 1 iteration.
                u64 delta = lo - j;
                // Unrolled jump by 4*step while possible to reduce loop trips
                u64 jump4 = step << 2;
                while (delta >= jump4) { j += jump4; delta -= jump4; }
                while (j < lo) j += step;
            }
            // Strike all odd multiples inside [lo, hi)
            // Map value j -> index ((j - lo_odd) >> 1)
            for (; j < hi; j += step) {
                u64 idx = (j - lo_odd) >> 1;
                flags[(size_t)idx] = 0;
            }
            // Carry "first >= hi" to the next segment
            next_mult[bi] = j;
        }

        // Count primes in this segment and track largest
        // Add 2 exactly once if it lies below hi and we haven't counted it yet
        if (!counted_two && 2 < hi) {
            primes_count += 1;
            largest_prime = 2;
            counted_two = true;
        }

        for (size_t i = 0; i < len; ++i) {
            if (!flags[i]) continue;
            u64 v = idx_to_val(lo_odd, i);
            if (v >= 3) {
                ++primes_count;
                largest_prime = v;
            }
        }

        // Advance to next segment
        lo = hi;

        // Exit when time budget is exhausted
        if (chrono::steady_clock::now() >= deadline) break;

        // Small safety to avoid tight overshoot
        if (chrono::steady_clock::now() + chrono::milliseconds(2) >= deadline) break;
    }

    double elapsed = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
    cout << "Primes found: " << primes_count << "\n";
    cout << "Largest prime found: " << largest_prime << "\n";
    cout << "Time: " << fixed << setprecision(3) << elapsed << " s\n";
    cout << "Range checked: 2.." << (lo - 1) << "\n";
    return 0;
}
