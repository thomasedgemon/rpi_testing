#include <bits/stdc++.h>  // Include all standard library headers (common in competitive programming)
#include <thread>         // For multi-threading
#include <mutex>          // For thread synchronization
#include <atomic>         // For atomic operations

// Type aliases for cleaner code and explicit bit sizes
using u8 = uint8_t;   // 8-bit unsigned integer (0 to 255)
using u32 = uint32_t; // 32-bit unsigned integer (0 to ~4 billion)
using u64 = uint64_t; // 64-bit unsigned integer (0 to ~18 quintillion)

/**
 * SMALL SIEVE FUNCTION
 * This implements the classic "Sieve of Eratosthenes" algorithm
 * Purpose: Find ALL prime numbers up to a given limit 'n'
 * Used to generate "base primes" needed for the segmented sieve
 */
static std::vector<u32> small_primes(u32 n) {
    if (n < 2) return {}; // No primes below 2
    
    // BIT PACKING OPTIMIZATION:
    // Instead of using 1 byte per number (wasteful), we use 1 bit per number
    // Each u64 (64-bit integer) can represent 64 numbers
    // So we need (n/64 + 1) integers to represent all numbers up to n
    std::vector<u64> is_prime((n / 64) + 1, ~0ULL);
    // ~0ULL means "all bits set to 1" (initially assume all numbers are prime)
    
    // CLEAR NON-PRIMES:
    // 0 and 1 are not prime, so clear their bits
    is_prime[0] &= ~3ULL; // ~3ULL = ...11111100 (clears bits 0 and 1)
    
    // CLASSIC SIEVE ALGORITHM:
    // For each potential prime p, mark all its multiples as non-prime
    for (u32 p = 2; p * p <= n; ++p) {
        // Check if p is still marked as prime
        // p/64 gives which u64 contains p's bit
        // p%64 gives which bit position within that u64
        if (is_prime[p / 64] & (1ULL << (p % 64))) {
            // p is prime, so mark all multiples starting from p*p
            // (smaller multiples already marked by smaller primes)
            for (u32 j = p * p; j <= n; j += p) {
                // Clear the bit for number j
                is_prime[j / 64] &= ~(1ULL << (j % 64));
            }
        }
    }
    
    // COLLECT RESULTS:
    // Convert bit array back to list of prime numbers
    std::vector<u32> ps;
    if (n > 10) {
        // OPTIMIZATION: Pre-reserve space using prime number theorem
        // Approximately n/ln(n) primes up to n
        ps.reserve(n / (std::log(n) - 1));
    }
    
    // Scan through all numbers and collect those still marked as prime
    for (u32 i = 2; i <= n; ++i) {
        if (is_prime[i / 64] & (1ULL << (i % 64))) {
            ps.push_back(i);
        }
    }
    return ps;
}

/**
 * WORKER THREAD FUNCTION
 * Each thread processes its own segments independently
 */
void worker_thread(int thread_id, 
                  const u64 SEG_SIZE,
                  const std::chrono::steady_clock::time_point& deadline,
                  std::atomic<u64>& global_low,
                  std::atomic<u64>& total_prime_count,
                  std::mutex& base_primes_mutex,
                  std::vector<u32>& shared_base_primes,
                  std::atomic<u32>& shared_base_limit) {
    
    // THREAD-LOCAL STATE:
    u64 thread_prime_count = 0;
    std::vector<u32> local_base_primes;
    u32 local_base_limit = 0;
    
    // MEMORY OPTIMIZATION:
    // Pre-allocate the bit array once per thread and reuse it
    const size_t bits_needed = (SEG_SIZE + 63) / 64;
    std::vector<u64> is_prime_bits(bits_needed);
    
    while (std::chrono::steady_clock::now() < deadline) {
        // ATOMIC SEGMENT ALLOCATION:
        // Each thread atomically claims the next available segment
        u64 low = global_low.fetch_add(SEG_SIZE);
        u64 high = low + SEG_SIZE;
        
        // UPDATE BASE PRIMES IF NEEDED:
        u32 need = (u32)std::sqrt((double)high) + 1;
        if (need > local_base_limit) {
            // Check if shared base primes are sufficient
            if (need > shared_base_limit.load()) {
                // Generate new base primes (only one thread does this)
                std::lock_guard<std::mutex> lock(base_primes_mutex);
                if (need > shared_base_limit.load()) {
                    shared_base_primes = small_primes(need);
                    shared_base_limit.store(need);
                }
            }
            
            // Copy shared base primes to local cache
            {
                std::lock_guard<std::mutex> lock(base_primes_mutex);
                local_base_primes = shared_base_primes;
                local_base_limit = shared_base_limit.load();
            }
        }
        
        // INITIALIZE SEGMENT:
        std::fill(is_prime_bits.begin(), is_prime_bits.end(), ~0ULL);
        
        // SIEVE THE SEGMENT:
        for (u32 p : local_base_primes) {
            // FIND FIRST MULTIPLE:
            u64 start = ((low + p - 1) / p) * p;
            if (start < (u64)p * p) start = (u64)p * p;
            if (start >= high) continue;
            
            u64 j = start - low;
            
            // OPTIMIZED SIEVING WITH LOOP UNROLLING:
            if (p == 2) {
                // SPECIAL CASE FOR 2: Mark all even numbers
                if (low % 2 == 0) {
                    for (u64 i = 0; i < SEG_SIZE; i += 2) {
                        is_prime_bits[i / 64] &= ~(1ULL << (i % 64));
                    }
                } else {
                    for (u64 i = 1; i < SEG_SIZE; i += 2) {
                        is_prime_bits[i / 64] &= ~(1ULL << (i % 64));
                    }
                }
            } else if (p < 64) {
                // LOOP UNROLLING FOR SMALL PRIMES:
                while (j + 7*p < SEG_SIZE) {
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64)); j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64)); j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64)); j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64)); j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64)); j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64)); j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64)); j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64)); j += p;
                }
                while (j < SEG_SIZE) {
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                    j += p;
                }
            } else {
                // REGULAR SIEVING FOR LARGER PRIMES:
                while (j < SEG_SIZE) {
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                    j += p;
                }
            }
        }
        
        // HANDLE SPECIAL CASES:
        if (low <= 1) {
            if (low == 0) {
                is_prime_bits[0] &= ~1ULL;
            }
            if (low <= 1 && high > 1) {
                is_prime_bits[(1 - low) / 64] &= ~(1ULL << ((1 - low) % 64));
            }
        }
        
        // COUNT PRIMES IN THIS SEGMENT:
        u64 segment_primes = 0;
        u64 nums_in_segment = std::min(SEG_SIZE, high - low);
        u64 full_words = nums_in_segment / 64;
        
        // COUNT COMPLETE 64-BIT WORDS:
        for (size_t i = 0; i < full_words; ++i) {
            segment_primes += __builtin_popcountll(is_prime_bits[i]);
        }
        
        // HANDLE PARTIAL LAST WORD:
        u64 remainder = nums_in_segment % 64;
        if (remainder > 0) {
            u64 mask = (1ULL << remainder) - 1;
            segment_primes += __builtin_popcountll(is_prime_bits[full_words] & mask);
        }
        
        // UPDATE THREAD-LOCAL COUNT:
        thread_prime_count += segment_primes;
    }
    
    // ADD THREAD RESULTS TO GLOBAL TOTAL:
    total_prime_count.fetch_add(thread_prime_count);
}

int main() {
    // PERFORMANCE OPTIMIZATIONS:
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    
    // PARALLEL SIEVE PARAMETERS:
    const int NUM_THREADS = 2;  // Use exactly 2 cores
    const u64 SEG_SIZE = 32'000ULL;
    const auto RUN_FOR = std::chrono::seconds(10);
    
    // TIMING SETUP:
    const auto t0 = std::chrono::steady_clock::now();
    const auto deadline = t0 + RUN_FOR;
    
    // SHARED STATE BETWEEN THREADS:
    std::atomic<u64> global_low(2);           // Next segment to process
    std::atomic<u64> total_prime_count(0);    // Total primes found across all threads
    
    // BASE PRIMES SHARING:
    std::vector<u32> shared_base_primes;
    std::atomic<u32> shared_base_limit(1);
    std::mutex base_primes_mutex;
    
    // CREATE AND LAUNCH WORKER THREADS:
    std::vector<std::thread> workers;
    workers.reserve(NUM_THREADS);
    
    for (int i = 0; i < NUM_THREADS; ++i) {
        workers.emplace_back(worker_thread, 
                           i,
                           SEG_SIZE,
                           std::cref(deadline),
                           std::ref(global_low),
                           std::ref(total_prime_count),
                           std::ref(base_primes_mutex),
                           std::ref(shared_base_primes),
                           std::ref(shared_base_limit));
    }
    
    // WAIT FOR ALL THREADS TO COMPLETE:
    for (auto& worker : workers) {
        worker.join();
    }
    
    // CALCULATE AND DISPLAY RESULTS:
    auto duration = std::chrono::steady_clock::now() - t0;
    auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() / 1000.0;
    
    u64 final_range = global_low.load();
    
    std::cout << "Primes found: " << total_prime_count.load() << "\n";
    std::cout << "Time: " << seconds << " seconds\n";
    std::cout << "Numbers/sec: " << (final_range - 2) / seconds / 1e6 << " million\n";
    std::cout << "Range checked: 2 to " << (final_range - 1) << "\n";
    std::cout << "Threads used: " << NUM_THREADS << "\n";
    
    return 0;
}