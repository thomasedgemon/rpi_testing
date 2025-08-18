#include <bits/stdc++.h>
#include <thread>
#include <atomic>
#include <condition_variable>
using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;

// Optimized small sieve with bit packing
static std::vector<u32> small_primes(u32 n) {
    if (n < 2) return {};
    
    std::vector<u64> is_prime((n / 64) + 1, ~0ULL);
    is_prime[0] &= ~3ULL;
    
    for (u32 p = 2; p * p <= n; ++p) {
        if (is_prime[p / 64] & (1ULL << (p % 64))) {
            for (u32 j = p * p; j <= n; j += p) {
                is_prime[j / 64] &= ~(1ULL << (j % 64));
            }
        }
    }
    
    std::vector<u32> ps;
    if (n > 10) {
        ps.reserve(n / (std::log(n) - 1));
    }
    
    for (u32 i = 2; i <= n; ++i) {
        if (is_prime[i / 64] & (1ULL << (i % 64))) {
            ps.push_back(i);
        }
    }
    return ps;
}

struct WorkSegment {
    u64 low;
    u64 high;
    WorkSegment(u64 l, u64 h) : low(l), high(h) {}
};

class WorkQueue {
private:
    std::queue<WorkSegment> segments;
    std::mutex mtx;
    std::condition_variable cv;
    bool finished = false;
    
public:
    void add_segment(u64 low, u64 high) {
        std::lock_guard<std::mutex> lock(mtx);
        segments.emplace(low, high);
        cv.notify_one();
    }
    
    bool get_segment(WorkSegment& seg) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this] { return !segments.empty() || finished; });
        
        if (segments.empty()) return false;
        
        seg = segments.front();
        segments.pop();
        return true;
    }
    
    void finish() {
        std::lock_guard<std::mutex> lock(mtx);
        finished = true;
        cv.notify_all();
    }
    
    size_t queue_size() {
        std::lock_guard<std::mutex> lock(mtx);
        return segments.size();
    }
};

// Optimized sieve function for a single segment
u64 sieve_segment(u64 low, u64 high, const std::vector<u32>& base_primes) {
    const u64 SEG_SIZE = high - low;
    const size_t bits_needed = (SEG_SIZE + 63) / 64;
    
    // Thread-local segment buffer
    std::vector<u64> is_prime_bits(bits_needed, ~0ULL);
    
    // Optimized sieving with cache-friendly access patterns
    for (u32 p : base_primes) {
        u64 start = ((low + p - 1) / p) * p;
        if (start < (u64)p * p) start = (u64)p * p;
        if (start >= high) continue;
        
        u64 j = start - low;
        
        if (p == 2) {
            // Vectorized even number elimination
            u64 start_bit = (low % 2 == 0) ? 0 : 1;
            u64 word_idx = start_bit / 64;
            u64 bit_idx = start_bit % 64;
            
            // Process word by word for better cache performance
            while (word_idx < bits_needed) {
                u64 mask = 0x5555555555555555ULL; // Every other bit
                if (bit_idx % 2 == 1) mask = 0xAAAAAAAAAAAAAAAAULL;
                is_prime_bits[word_idx] &= ~mask;
                word_idx++;
                bit_idx = 0;
            }
        } else if (p < 32) {
            // Heavy unrolling for small primes (bigger impact on performance)
            while (j + 15*p < SEG_SIZE) {
                // 16x unrolling
                for (int unroll = 0; unroll < 16; ++unroll) {
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                    j += p;
                }
            }
            while (j < SEG_SIZE) {
                is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                j += p;
            }
        } else if (p < 256) {
            // 4x unrolling for medium primes
            while (j + 3*p < SEG_SIZE) {
                is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                j += p;
                is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                j += p;
                is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                j += p;
                is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                j += p;
            }
            while (j < SEG_SIZE) {
                is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                j += p;
            }
        } else {
            // Standard sieving for large primes
            while (j < SEG_SIZE) {
                is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                j += p;
            }
        }
    }
    
    // Handle special cases
    if (low <= 1) {
        if (low == 0) is_prime_bits[0] &= ~1ULL;
        if (low <= 1 && high > 1) is_prime_bits[(1 - low) / 64] &= ~(1ULL << ((1 - low) % 64));
    }
    
    // Count primes using hardware popcount
    u64 segment_primes = 0;
    u64 nums_in_segment = std::min(SEG_SIZE, high - low);
    u64 full_words = nums_in_segment / 64;
    
    for (size_t i = 0; i < full_words; ++i) {
        segment_primes += __builtin_popcountll(is_prime_bits[i]);
    }
    
    u64 remainder = nums_in_segment % 64;
    if (remainder > 0) {
        u64 mask = (1ULL << remainder) - 1;
        segment_primes += __builtin_popcountll(is_prime_bits[full_words] & mask);
    }
    
    return segment_primes;
}

// Worker thread function
void worker_thread(WorkQueue& queue, const std::vector<u32>& base_primes, 
                   std::atomic<u64>& total_count) {
    WorkSegment seg(0, 0);
    while (queue.get_segment(seg)) {
        u64 primes_in_segment = sieve_segment(seg.low, seg.high, base_primes);
        total_count.fetch_add(primes_in_segment, std::memory_order_relaxed);
    }
}

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    
    // Optimized segment size for ARM Cortex-A53
    // Smaller segments for better load balancing across 4 cores
    const u64 SEG_SIZE = 200'000ULL;  // Reduced for better parallelization
    const auto RUN_FOR = std::chrono::seconds(10);
    const int NUM_THREADS = 4;
    
    const auto t0 = std::chrono::steady_clock::now();
    const auto deadline = t0 + RUN_FOR;
    
    std::atomic<u64> total_prime_count{0};
    WorkQueue work_queue;
    
    // Pre-compute base primes for a reasonable range
    // We'll compute enough to handle segments up to a large number
    const u64 MAX_EXPECTED_RANGE = 1'000'000'000ULL; // 1 billion
    u32 base_limit = (u32)std::sqrt((double)MAX_EXPECTED_RANGE) + 1;
    
    std::cout << "Computing base primes up to " << base_limit << "...\n";
    auto base_primes = small_primes(base_limit);
    std::cout << "Found " << base_primes.size() << " base primes\n";
    
    // Start worker threads
    std::vector<std::thread> workers;
    workers.reserve(NUM_THREADS);
    
    for (int i = 0; i < NUM_THREADS; ++i) {
        workers.emplace_back(worker_thread, std::ref(work_queue), 
                           std::cref(base_primes), std::ref(total_prime_count));
    }
    
    // Producer thread - generate work segments
    u64 current_low = 2;
    u64 max_range_processed = 2;
    
    while (std::chrono::steady_clock::now() < deadline) {
        // Keep the work queue filled but not overfilled
        while (work_queue.queue_size() < NUM_THREADS * 3 && 
               std::chrono::steady_clock::now() < deadline) {
            u64 current_high = current_low + SEG_SIZE;
            work_queue.add_segment(current_low, current_high);
            current_low = current_high;
            max_range_processed = current_high;
        }
        
        // Brief pause to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Signal completion and wait for workers to finish
    work_queue.finish();
    
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    auto duration = std::chrono::steady_clock::now() - t0;
    auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() / 1000.0;
    
    std::cout << "Primes found: " << total_prime_count.load() << "\n";
    std::cout << "Time: " << seconds << " seconds\n";
    std::cout << "Numbers/sec: " << (max_range_processed - 2) / seconds / 1e6 << " million\n";
    std::cout << "Range checked: 2 to " << (max_range_processed - 1) << "\n";
    std::cout << "Using " << NUM_THREADS << " threads\n";
    std::cout << "Primes/second: " << total_prime_count.load() / seconds << "\n";
    
    return 0;
}