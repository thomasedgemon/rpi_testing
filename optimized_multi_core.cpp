// optimized_multi_core.cpp
// Fast, cache-friendly multi-core segmented sieve (odd-only).
// - Odd-only segment flags (1 byte per odd).
// - Threads grab CHUNKS of contiguous segments so they can carry "next multiple"
//   across segments (no per-segment ceil/div in the hot loop).
// - Shared base primes extended under a mutex with geometric growth.
// - No progress printing; only final stats.
//
// Build:
//   g++ -O3 -pipe -flto -fno-exceptions -fno-rtti -march=native -DNDEBUG -pthread optimized_multi_core.cpp -o optimized_mc
// (on Pi Zero 2W: replace -march=native with -march=armv8-a -mtune=cortex-a53)
//
// Usage:
//   ./optimized_mc [seconds=10] [threads=hardware_concurrency]

//g++ -O3 -std=c++17 -pthread optimized_multi_core.cpp -o optimized_multi_core

#include <bits/stdc++.h>
#include <atomic>
#include <thread>
#include <mutex>

using namespace std;

using u8  = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;

// -------------------- Utilities --------------------
static inline u64 idx_to_val(u64 lo_odd, u64 i) { return lo_odd + (i << 1); }

static inline u64 first_odd_multiple_ge(u64 lo, u64 p) {
    u64 m = (lo % p == 0) ? lo : lo + (p - (lo % p));
    if ((m & 1ULL) == 0) m += p;
    return m;
}

// -------------------- Shared base primes --------------------
struct BasePrimes {
    vector<u32> primes;
    u32 sieved_to = 1;
    mutex mtx;

    // Extend up to at least new_need, growing geometrically to reduce calls.
    void ensure(u32 new_need) {
        if (new_need <= sieved_to) return;
        lock_guard<mutex> lk(mtx);
        if (new_need <= sieved_to) return;

        u32 target = max<u32>(new_need, sieved_to ? min<u32>(numeric_limits<u32>::max(), sieved_to * 2u) : new_need);

        // Dense simple sieve up to 'target'.
        vector<u8> mark(target + 1, 1);
        if (target >= 0) mark[0] = 0;
        if (target >= 1) mark[1] = 0;

        // If we already have primes, mark using them first to accelerate.
        for (u32 p : primes) {
            u64 start = (u64)p * (u64)p;
            if (start > target) break;
            for (u64 j = start; j <= target; j += p) mark[(size_t)j] = 0;
        }

        // Continue vanilla sieve; start from 2 or last known region.
        for (u32 i = 2; (u64)i * i <= target; ++i) {
            if (!mark[i]) continue;
            u64 start = (u64)i * (u64)i;
            for (u64 j = start; j <= target; j += i) mark[(size_t)j] = 0;
        }

        // Rebuild primes fresh up to target (simpler & still cheap at this scale).
        primes.clear();
        for (u32 i = 2; i <= target; ++i) if (mark[i]) primes.push_back(i);

        sieved_to = target;
    }
};

// -------------------- Work allocator (global) --------------------
struct WorkAllocator {
    static constexpr size_t SEG_BYTES  = 512 * 1024;   // 512 KiB flags
    static constexpr u64    SEG_ODDS   = SEG_BYTES;     // 1 byte per odd
    static constexpr u64    SEG_SPAN   = SEG_ODDS * 2;  // value-domain width
    static constexpr int    CHUNK_SEGS = 16;
    static constexpr u64    CHUNK_SPAN = SEG_SPAN * CHUNK_SEGS;

    // 32-bit on armhf avoids libatomic
    std::atomic<uint32_t> next_chunk{0};

    // Each chunk id maps to a disjoint range starting at:
    // lo = 2 + (u64)chunk_id * CHUNK_SPAN
    u64 get_chunk() {
        uint32_t id = next_chunk.fetch_add(1, std::memory_order_relaxed);
        return 2 + (u64)id * CHUNK_SPAN;
    }
};


// -------------------- Thread worker --------------------
struct ThreadResult {
    u64 primes_count = 0;
    u64 largest_prime = 0;
    u64 range_end = 1; // last value processed
};

static void worker(double seconds,
                   BasePrimes* base_shared,
                   WorkAllocator* alloc,
                   ThreadResult* out)
{
    using clock = chrono::steady_clock;
    auto t0 = clock::now();
    auto deadline = t0 + chrono::duration<double>(seconds);

    vector<u8> flags(WorkAllocator::SEG_BYTES, 1);
    vector<u64> next_mult; next_mult.reserve(1<<16); // per-thread, reused across segments

    u64 local_count = 0;
    u64 local_largest = 0;
    bool counted_two_in_this_chunk = false;

    while (clock::now() < deadline) {
        // Acquire a contiguous chunk.
        u64 lo = alloc->get_chunk();
        // Reset per-chunk "counted 2" so only the very first chunk that crosses 2 counts it.
        counted_two_in_this_chunk = false;

        // Initialize next_mult size to current base size later (after ensure).
        // Process CHUNK_SEGS contiguous segments.
        for (int seg = 0; seg < WorkAllocator::CHUNK_SEGS; ++seg) {
            u64 lo_odd = (lo | 1ULL);
            u64 hi = lo_odd + WorkAllocator::SEG_SPAN; // exclusive

            // Ensure base primes cover sqrt(hi-1)
            u64 need64 = (u64)floor(sqrt((long double)(hi - 1)));
            u32 need = (need64 > std::numeric_limits<u32>::max()) ? std::numeric_limits<u32>::max() : (u32)need64;
            base_shared->ensure(need);

            // If base grew, resize/init next_mult for new primes
            if (next_mult.size() != base_shared->primes.size()) {
                size_t old = next_mult.size();
                next_mult.resize(base_shared->primes.size(), 0);
                for (size_t i = old; i < base_shared->primes.size(); ++i) {
                    u32 p = base_shared->primes[i];
                    if (p < 3) { next_mult[i] = 0; continue; }
                    next_mult[i] = first_odd_multiple_ge(lo, p);
                }
            }

            // Reset flags to "prime" for this segment
            memset(flags.data(), 1, flags.size());

            // Strike (odd-only). Handle p=2 implicitly by our mapping (evens not represented).
            size_t start_idx = 0;
            if (!base_shared->primes.empty() && base_shared->primes[0] == 2) start_idx = 1;

            for (size_t bi = start_idx; bi < base_shared->primes.size(); ++bi) {
                u32 p = base_shared->primes[bi];
                u64 step = (u64)p << 1; // 2p

                u64 j = next_mult[bi];
                if (j < lo) {
                    u64 delta = lo - j;
                    u64 jump4 = step << 2;
                    while (delta >= jump4) { j += jump4; delta -= jump4; }
                    while (j < lo) j += step;
                }
                for (; j < hi; j += step) {
                    u64 idx = (j - lo_odd) >> 1;
                    flags[(size_t)idx] = 0;
                }
                next_mult[bi] = j; // carry into next contiguous segment
            }

            // Count primes in this segment
            if (!counted_two_in_this_chunk && 2 < hi && lo <= 2) {
                local_count += 1;
                local_largest = 2;
                counted_two_in_this_chunk = true;
            }
            const u64 len = WorkAllocator::SEG_ODDS;
            for (u64 i = 0; i < len; ++i) {
                if (!flags[(size_t)i]) continue;
                u64 v = idx_to_val(lo_odd, i);
                if (v >= 3) {
                    ++local_count;
                    local_largest = v;
                }
            }

            lo = hi; // next contiguous segment inside this chunk

            // Soft stop inside chunk if time’s up
            if (clock::now() >= deadline) break;
        }

        out->range_end = max(out->range_end, (u64)(flags.size()*2)); // not exact; updated below anyway
        if (clock::now() >= deadline) break;
    }

    out->primes_count = local_count;
    out->largest_prime = local_largest;
}

// -------------------- main --------------------
int main(int argc, char** argv) {
    double RUN_SECONDS = 10.0;
    if (argc >= 2) RUN_SECONDS = atof(argv[1]);

    unsigned threads = std::thread::hardware_concurrency();
    if (threads == 0) threads = 2;
    if (argc >= 3) {
        int t = atoi(argv[2]);
        if (t > 0) threads = (unsigned)t;
    }

    BasePrimes base_shared;
    // Bootstrap a small base once (cheap).
    base_shared.ensure(100);

    WorkAllocator alloc;

    vector<thread> pool;
    vector<ThreadResult> results(threads);

    for (unsigned i = 0; i < threads; ++i) {
        pool.emplace_back(worker, RUN_SECONDS, &base_shared, &alloc, &results[i]);
    }
    for (auto& th : pool) th.join();

    // Reduce
    u64 total = 0;
    u64 maxp  = 0;
    for (const auto& r : results) {
        total += r.primes_count;
        if (r.largest_prime > maxp) maxp = r.largest_prime;
    }

    cout << "Threads: " << threads << "\n";
    cout << "Primes found: " << total << "\n";
    cout << "Largest prime found: " << maxp << "\n";
    cout << "Time: " << fixed << setprecision(3) << RUN_SECONDS << " s\n";
    return 0;
}
