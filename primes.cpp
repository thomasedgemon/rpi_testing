#include <bits/stdc++.h>
using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;

// Optimized small sieve with bit packing
//find all primes up to sqrt(n)
static std::vector<u32> small_primes(u32 n) {
    if (n < 2) return {};
    
    // Use bits instead of bytes
    //64 bit int comprised of zeroes and ones, where one=prime, 0=nonprime
    std::vector<u64> is_prime((n / 64) + 1, ~0ULL);
    
    // Clear 0 and 1
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

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    
    // Segment size optimized for ARM Cortex-A53
    // 32KB L1 cache, 512KB L2 cache
    // 500K numbers = ~63KB with bit packing
    const u64 SEG_SIZE = 500'000ULL;
    const auto RUN_FOR = std::chrono::seconds(10);
    
    const auto t0 = std::chrono::steady_clock::now();
    const auto deadline = t0 + RUN_FOR;
    
    u64 low = 2;  // Start from 2
    u64 total_prime_count = 0;
    
    std::vector<u32> base_primes;
    u32 base_limit = 1;
    
    // Pre-allocate segment bitset
    //creates one bit array that is reused for every segment
    //allocates space for 500k numbers packed into ~7813 64bit ints
    const size_t bits_needed = (SEG_SIZE + 63) / 64;
    std::vector<u64> is_prime_bits(bits_needed);
    
    // Pre-compute wheel mod 2,3,5 for faster sieving
    const u32 wheel_primes[3] = {2, 3, 5};
    
    while (std::chrono::steady_clock::now() < deadline) {
        u64 high = low + SEG_SIZE;
        
        // Update base primes if needed
        u32 need = (u32)std::sqrt((double)high) + 1;
        if (need > base_limit) {
            base_primes = small_primes(need);
            base_limit = need;
        }
        
        // Initialize all bits to 1 (potentially prime)
        std::fill(is_prime_bits.begin(), is_prime_bits.end(), ~0ULL);
        
        // Optimized sieving with unrolling for small primes
        for (u32 p : base_primes) {
            u64 start = ((low + p - 1) / p) * p;
            if (start < (u64)p * p) start = (u64)p * p;
            if (start >= high) continue;
            
            u64 j = start - low; //convert number to a position within the segment
            
            // Heavy unrolling for smallest primes (biggest impact)
            if (p == 2) {
                // Special case for 2 - just clear all even bits
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
                // 8x unrolling for small primes
                while (j + 7*p < SEG_SIZE) {
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64)); //from segment position to bit location
                    j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                    j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                    j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                    j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                    j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                    j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                    j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                    j += p;
                }
                // Handle remainder
                while (j < SEG_SIZE) {
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                    j += p;
                }
            } else {
                // Regular sieving for larger primes
                while (j < SEG_SIZE) {
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                    j += p;
                }
            }
        }
        
        // Handle special cases for small numbers
        if (low <= 1) {
            if (low == 0) is_prime_bits[0] &= ~1ULL;  // Clear 0
            if (low <= 1 && high > 1) is_prime_bits[(1 - low) / 64] &= ~(1ULL << ((1 - low) % 64));  // Clear 1
        }
        
        // Count primes using hardware popcount
        u64 segment_primes = 0;
        u64 nums_in_segment = std::min(SEG_SIZE, high - low);
        u64 full_words = nums_in_segment / 64;
        
        // Count full 64-bit words
        //counts  num of 1 bit in an int in a single cycle rather than
        //looping through 64 bits
        for (size_t i = 0; i < full_words; ++i) {
            segment_primes += __builtin_popcountll(is_prime_bits[i]);
        }
        
        // Handle partial last word
        u64 remainder = nums_in_segment % 64;
        if (remainder > 0) {
            u64 mask = (1ULL << remainder) - 1;
            segment_primes += __builtin_popcountll(is_prime_bits[full_words] & mask);
        }
        
        total_prime_count += segment_primes;
        low = high;
    }
    
    auto duration = std::chrono::steady_clock::now() - t0;
    auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() / 1000.0;
    
    std::cout << "Primes found: " << total_prime_count << "\n";
    std::cout << "Time: " << seconds << " seconds\n";
    std::cout << "Numbers/sec: " << (low - 2) / seconds / 1e6 << " million\n";
    std::cout << "Range checked: 2 to " << (low - 1) << "\n";
    
    return 0;
}