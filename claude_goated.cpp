#include <iostream>
#include <vector>
#include <chrono>
#include <cmath>
#include <thread>
#include <atomic>
#include <mutex>

// Use smaller integer types where possible to save memory
using u32 = uint32_t;
using u64 = uint64_t;

/**
 * MEMORY-EFFICIENT BASE PRIME GENERATOR
 * Generates primes on-demand rather than storing large arrays
 */
class StreamingPrimeGenerator {
private:
    std::vector<u32> cached_primes;
    u32 cache_limit;
    
public:
    StreamingPrimeGenerator() : cache_limit(0) {}
    
    // Generate primes up to n, only if we don't already have them
    void ensure_primes_up_to(u32 n) {
        if (n <= cache_limit) return;
        
        // Use simple sieve for small ranges only
        std::vector<bool> is_prime(n + 1, true);
        is_prime[0] = is_prime[1] = false;
        
        for (u32 p = 2; p * p <= n; ++p) {
            if (is_prime[p]) {
                for (u32 j = p * p; j <= n; j += p) {
                    is_prime[j] = false;
                }
            }
        }
        
        // Only store primes we didn't have before
        for (u32 i = cache_limit + 1; i <= n; ++i) {
            if (is_prime[i]) {
                cached_primes.push_back(i);
            }
        }
        cache_limit = n;
    }
    
    const std::vector<u32>& get_primes() const { return cached_primes; }
    
    // Memory management: clear cache if it gets too large
    void trim_cache_if_needed() {
        // Keep cache under 1MB (roughly 250K primes)
        if (cached_primes.size() > 250000) {
            cached_primes.clear();
            cache_limit = 0;
        }
    }
};

/**
 * LIGHTWEIGHT WORKER THREAD FOR PI 2W
 * Minimal memory footprint, simple operations
 */
void pi_worker_thread(int thread_id,
                     const u64 SEG_SIZE,
                     const std::chrono::steady_clock::time_point& deadline,
                     std::atomic<u64>& global_low,
                     std::atomic<u64>& total_prime_count,
                     std::mutex& prime_gen_mutex,
                     StreamingPrimeGenerator& prime_gen) {
    
    u64 thread_prime_count = 0;
    std::vector<u32> local_primes;
    u32 local_prime_limit = 0;
    
    // TINY memory footprint: only 2KB for bit array
    const size_t bits_needed = (SEG_SIZE + 63) / 64;
    std::vector<u64> is_prime_bits(bits_needed);
    
    while (std::chrono::steady_clock::now() < deadline) {
        // Atomic segment allocation
        u64 low = global_low.fetch_add(SEG_SIZE);
        u64 high = low + SEG_SIZE;
        
        // Conservative memory limit: don't go beyond 50M range
        if (low > 50000000ULL) break;
        
        // Update base primes if needed
        u32 need = (u32)std::sqrt((double)high) + 1;
        if (need > local_prime_limit) {
            std::lock_guard<std::mutex> lock(prime_gen_mutex);
            prime_gen.ensure_primes_up_to(need);
            local_primes = prime_gen.get_primes();
            local_prime_limit = need;
            
            // Memory management
            prime_gen.trim_cache_if_needed();
        }
        
        // Initialize segment - simple fill
        for (size_t i = 0; i < is_prime_bits.size(); ++i) {
            is_prime_bits[i] = ~0ULL;
        }
        
        // Simple sieving - optimized for ARM Cortex-A7
        for (u32 p : local_primes) {
            if (p > need) break;
            
            u64 start = ((low + p - 1) / p) * p;
            if (start < (u64)p * p) start = (u64)p * p;
            if (start >= high) continue;
            
            // Simple loop - no complex unrolling for ARM
            for (u64 j = start; j < high; j += p) {
                u64 pos = j - low;
                if (pos < SEG_SIZE) {
                    is_prime_bits[pos / 64] &= ~(1ULL << (pos % 64));
                }
            }
        }
        
        // Handle special cases
        if (low <= 1) {
            if (low == 0) is_prime_bits[0] &= ~1ULL;
            if (low <= 1 && high > 1) {
                u64 pos = 1 - low;
                is_prime_bits[pos / 64] &= ~(1ULL << (pos % 64));
            }
        }
        
        // Count primes - simple bit counting (no hardware popcount dependency)
        u64 segment_primes = 0;
        u64 nums_in_segment = std::min(SEG_SIZE, high - low);
        
        for (u64 i = 0; i < nums_in_segment; ++i) {
            if (is_prime_bits[i / 64] & (1ULL << (i % 64))) {
                segment_primes++;
            }
        }
        
        thread_prime_count += segment_primes;
    }
    
    total_prime_count.fetch_add(thread_prime_count);
}

int main() {
    // RASPBERRY PI 2W OPTIMIZED SETTINGS
    const int NUM_THREADS = 2;        // Conservative threading
    const u64 SEG_SIZE = 16'000ULL;   // Tiny segments for memory efficiency
    const auto RUN_FOR = std::chrono::seconds(10);
    
    std::cout << "Raspberry Pi 2W Optimized Prime Sieve\n";
    std::cout << "Threads: " << NUM_THREADS << ", Segment size: " << SEG_SIZE << "\n";
    
    const auto t0 = std::chrono::steady_clock::now();
    const auto deadline = t0 + RUN_FOR;
    
    // Shared state with minimal memory footprint
    std::atomic<u64> global_low(2);
    std::atomic<u64> total_prime_count(0);
    
    StreamingPrimeGenerator prime_gen;
    std::mutex prime_gen_mutex;
    
    // Launch lightweight worker threads
    std::vector<std::thread> workers;
    workers.reserve(NUM_THREADS);
    
    for (int i = 0; i < NUM_THREADS; ++i) {
        workers.emplace_back(pi_worker_thread,
                           i, SEG_SIZE, std::cref(deadline),
                           std::ref(global_low), std::ref(total_prime_count),
                           std::ref(prime_gen_mutex), std::ref(prime_gen));
    }
    
    // Monitor progress (optional - can be removed to save cycles)
    std::thread monitor([&]() {
        while (std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            std::cout << "Progress: " << elapsed << "ms, range: " 
                      << global_low.load() << ", primes: " 
                      << total_prime_count.load() << "\n";
        }
    });
    
    // Wait for completion
    for (auto& worker : workers) {
        worker.join();
    }
    monitor.join();
    
    // Results
    auto duration = std::chrono::steady_clock::now() - t0;
    auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() / 1000.0;
    u64 final_range = global_low.load();
    
    std::cout << "\nResults:\n";
    std::cout << "Primes found: " << total_prime_count.load() << "\n";
    std::cout << "Time: " << seconds << " seconds\n";
    std::cout << "Range: 2 to " << (final_range - 1) << "\n";
    std::cout << "Rate: " << (final_range - 2) / seconds / 1000.0 << " thousand/sec\n";
    
    return 0;
}