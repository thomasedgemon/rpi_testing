// optimized_multi_core_pi.cpp
// Optimized for Raspberry Pi Zero 2W (Cortex-A53, 512MB RAM, ~32KB L1 cache)
// - Bit-packed flags (8x memory reduction)
// - Smaller segments for L1 cache fit
// - Loop unrolling in hot paths
// - Simple odd-only sieve (no complex wheel)
// - Portable code (compiles on any system)
//
// Build for Pi Zero 2W:
//   g++ -O3 -pipe -flto -fno-exceptions -fno-rtti -mcpu=cortex-a53 -ftree-vectorize -funroll-loops -DNDEBUG -pthread optimized_multi_core_pi.cpp -o optimized_mc_pi
//
// Build for other systems:
//   g++ -O3 -pipe -flto -fno-exceptions -fno-rtti -march=native -funroll-loops -DNDEBUG -pthread optimized_multi_core_pi.cpp -o optimized_mc_pi
//
// Usage:
//   ./optimized_mc_pi [seconds=10] [threads=3]

#include <bits/stdc++.h>
#include <atomic>
#include <thread>
#include <mutex>

using namespace std;

using u8  = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;

// -------------------- Bit manipulation --------------------
static inline void clear_bit(u64* arr, size_t bit) {
    arr[bit >> 6] &= ~(1ULL << (bit & 63));
}

static inline bool test_bit(const u64* arr, size_t bit) {
    return (arr[bit >> 6] >> (bit & 63)) & 1;
}

// Fast bit counting - portable version
static u64 popcount_array(const u64* arr, size_t n_u64) {
    u64 total = 0;
    
    // Unroll by 4 for better performance
    size_t i = 0;
    for (; i + 3 < n_u64; i += 4) {
        total += __builtin_popcountll(arr[i]);
        total += __builtin_popcountll(arr[i + 1]);
        total += __builtin_popcountll(arr[i + 2]);
        total += __builtin_popcountll(arr[i + 3]);
    }
    
    // Handle remainder
    for (; i < n_u64; ++i) {
        total += __builtin_popcountll(arr[i]);
    }
    
    return total;
}

// -------------------- Shared base primes --------------------
struct BasePrimes {
    vector<u32> primes;
    u32 sieved_to = 1;
    mutex mtx;

    void ensure(u32 new_need) {
        if (new_need <= sieved_to) return;
        lock_guard<mutex> lk(mtx);
        if (new_need <= sieved_to) return;

        u32 target = max<u32>(new_need, sieved_to ? min<u32>(numeric_limits<u32>::max(), sieved_to * 2u) : new_need);

        // Simple sieve for base primes
        vector<u8> mark(target + 1, 1);
        if (target >= 0) mark[0] = 0;
        if (target >= 1) mark[1] = 0;

        for (u32 p : primes) {
            u64 start = (u64)p * (u64)p;
            if (start > target) break;
            for (u64 j = start; j <= target; j += p) mark[(size_t)j] = 0;
        }

        for (u32 i = 2; (u64)i * i <= target; ++i) {
            if (!mark[i]) continue;
            u64 start = (u64)i * (u64)i;
            for (u64 j = start; j <= target; j += i) mark[(size_t)j] = 0;
        }

        primes.clear();
        for (u32 i = 2; i <= target; ++i) {
            if (mark[i]) primes.push_back(i);
        }

        sieved_to = target;
    }
};

// -------------------- Work allocator --------------------
struct WorkAllocator {
    // Optimized for L1 cache (32KB) - bit-packed allows more coverage
    static constexpr size_t SEG_BYTES  = 16 * 1024;      // 16KB total storage
    static constexpr size_t SEG_U64S   = SEG_BYTES / 8;  // 2048 u64 elements
    static constexpr u64    SEG_BITS   = SEG_BYTES * 8;  // 131072 bit positions
    static constexpr u64    SEG_ODDS   = SEG_BITS;       // One bit per odd number
    static constexpr u64    SEG_SPAN   = SEG_ODDS * 2;   // Range covered (262144)
    static constexpr int    CHUNK_SEGS = 32;             // Segments per chunk
    static constexpr u64    CHUNK_SPAN = SEG_SPAN * CHUNK_SEGS;

    std::atomic<uint32_t> next_chunk{0};

    u64 get_chunk() {
        uint32_t id = next_chunk.fetch_add(1, std::memory_order_relaxed);
        return 3 + (u64)id * CHUNK_SPAN; // Start at 3
    }
};

// -------------------- Thread worker --------------------
struct ThreadResult {
    u64 primes_count = 0;
    u64 largest_prime = 0;
    u64 segments_processed = 0;
    u64 bytes_touched = 0;
    u64 max_hi_processed = 0;
};

static void worker(double seconds,
                   BasePrimes* base_shared,
                   WorkAllocator* alloc,
                   ThreadResult* out)
{
    using clock = chrono::steady_clock;
    auto t0 = clock::now();
    auto deadline = t0 + chrono::duration<double>(seconds);

    // Bit-packed flags - one bit per odd number
    vector<u64> flags(WorkAllocator::SEG_U64S);
    vector<u64> next_mult;
    next_mult.reserve(1<<16);

    u64 local_count = 0;
    u64 local_largest = 0;
    u64 local_segments = 0;
    u64 local_bytes = 0;
    u64 local_max_hi = 0;
    
    // Count 2 once
    static bool counted_two = false;
    static mutex two_mutex;
    {
        lock_guard<mutex> lk(two_mutex);
        if (!counted_two) {
            local_count = 1; // Just 2
            local_largest = 2;
            counted_two = true;
        }
    }

    while (clock::now() < deadline) {
        u64 chunk_lo = alloc->get_chunk();
        
        for (int seg = 0; seg < WorkAllocator::CHUNK_SEGS; ++seg) {
            u64 lo = chunk_lo + seg * WorkAllocator::SEG_SPAN;
            u64 hi = lo + WorkAllocator::SEG_SPAN;
            
            // Make lo odd
            u64 lo_odd = lo | 1ULL;

            // Ensure base primes cover sqrt(hi-1)
            u64 need64 = (u64)floor(sqrt((long double)(hi - 1)));
            u32 need = (need64 > numeric_limits<u32>::max()) ? numeric_limits<u32>::max() : (u32)need64;
            base_shared->ensure(need);

            // Initialize next_mult for new primes
            if (next_mult.size() != base_shared->primes.size()) {
                size_t old = next_mult.size();
                next_mult.resize(base_shared->primes.size(), 0);
                for (size_t i = old; i < base_shared->primes.size(); ++i) {
                    u32 p = base_shared->primes[i];
                    if (p == 2) {
                        next_mult[i] = 0; // Skip 2, all evens are composite
                        continue;
                    }
                    // Find first odd multiple of p >= lo_odd
                    u64 m = ((lo_odd + p - 1) / p) * p;
                    if ((m & 1) == 0) m += p; // Make it odd
                    next_mult[i] = m;
                }
            }

            // Set all bits to 1 (all prime initially)
            memset(flags.data(), 0xFF, WorkAllocator::SEG_U64S * sizeof(u64));

            // Sieve - skip even prime (2)
            size_t start_idx = 0;
            if (!base_shared->primes.empty() && base_shared->primes[0] == 2) {
                start_idx = 1;
            }

            for (size_t bi = start_idx; bi < base_shared->primes.size(); ++bi) {
                u32 p = base_shared->primes[bi];
                u64 step = p * 2; // Step by 2p to stay odd
                
                // Get starting position for this segment
                u64 j = next_mult[bi];
                if (j < lo_odd) {
                    // Recalculate if we're behind
                    u64 m = ((lo_odd + p - 1) / p) * p;
                    if ((m & 1) == 0) m += p;
                    j = m;
                }
                
                // Mark composites - unrolled by 4
                u64 end = lo_odd + WorkAllocator::SEG_SPAN;
                while (j + 3*step < end) {
                    u64 idx0 = (j - lo_odd) >> 1;
                    u64 idx1 = (j + step - lo_odd) >> 1;
                    u64 idx2 = (j + 2*step - lo_odd) >> 1;
                    u64 idx3 = (j + 3*step - lo_odd) >> 1;
                    
                    if (idx0 < WorkAllocator::SEG_BITS) clear_bit(flags.data(), idx0);
                    if (idx1 < WorkAllocator::SEG_BITS) clear_bit(flags.data(), idx1);
                    if (idx2 < WorkAllocator::SEG_BITS) clear_bit(flags.data(), idx2);
                    if (idx3 < WorkAllocator::SEG_BITS) clear_bit(flags.data(), idx3);
                    
                    j += 4 * step;
                }
                
                // Handle remainder
                while (j < end) {
                    u64 idx = (j - lo_odd) >> 1;
                    if (idx < WorkAllocator::SEG_BITS) {
                        clear_bit(flags.data(), idx);
                    }
                    j += step;
                }
                
                next_mult[bi] = j; // Save for next segment
            }

            // Count primes in this segment
            u64 seg_count = popcount_array(flags.data(), WorkAllocator::SEG_U64S);
            local_count += seg_count;

            // ---- metrics for reality checks ----
            ++local_segments;
            local_bytes += WorkAllocator::SEG_BYTES;
            if (hi > local_max_hi) local_max_hi = hi;

            // Find largest prime in segment (scan backwards)
            for (int64_t i = WorkAllocator::SEG_BITS - 1; i >= 0; --i) {
                if (test_bit(flags.data(), i)) {
                    u64 p = lo_odd + (i << 1);
                    if (p > local_largest) {
                        local_largest = p;
                    }
                    break; // Found largest in this segment
                }
            }
            
            // Check deadline
            if (clock::now() >= deadline) break;
        }

        if (clock::now() >= deadline) break;
    }

    out->primes_count = local_count;
    out->largest_prime = local_largest;
    out->segments_processed = local_segments;
    out->bytes_touched = local_bytes;
    out->max_hi_processed = local_max_hi;
}

// -------------------- main --------------------
int main(int argc, char** argv) {
    double RUN_SECONDS = 10.0;
    if (argc >= 2) RUN_SECONDS = atof(argv[1]);

    // Default to 3 threads for Pi Zero 2W (better memory bandwidth utilization)
    unsigned threads = 3;
    unsigned hw_threads = std::thread::hardware_concurrency();
    if (hw_threads > 0 && hw_threads < 3) threads = hw_threads;
    
    if (argc >= 3) {
        int t = atoi(argv[2]);
        if (t > 0) threads = (unsigned)t;
    }

    BasePrimes base_shared;
    base_shared.ensure(100);

    WorkAllocator alloc;

    vector<thread> pool;
    vector<ThreadResult> results(threads);

    auto start_time = chrono::steady_clock::now();
    
    for (unsigned i = 0; i < threads; ++i) {
        pool.emplace_back(worker, RUN_SECONDS, &base_shared, &alloc, &results[i]);
    }
    for (auto& th : pool) th.join();
    
    auto end_time = chrono::steady_clock::now();
    double actual_seconds = chrono::duration<double>(end_time - start_time).count();

    // Reduce results
    u64 total = 0;
    u64 maxp = 0;
    for (const auto& r : results) {
        total += r.primes_count;
        if (r.largest_prime > maxp) maxp = r.largest_prime;
    }
    // Aggregate instrumentation
    u64 total_segments = 0;
    u64 total_bytes = 0;
    u64 final_N_processed = 0;
    for (const auto& r : results) {
        total_segments += r.segments_processed;
        total_bytes += r.bytes_touched;
        if (r.max_hi_processed > final_N_processed) final_N_processed = r.max_hi_processed;
    }

    cout << "Threads: " << threads << "\n";
    cout << "Primes found: " << total << "\n";
    cout << "Largest prime found: " << maxp << "\n";
    cout << "Final N processed: " << final_N_processed << "\n";
    cout << "Segments processed: " << total_segments << "\n";
    cout << "Approx bytes touched: " << total_bytes << "\n";
    cout << "Time: " << fixed << setprecision(3) << actual_seconds << " s\n";
    
    return 0;
}