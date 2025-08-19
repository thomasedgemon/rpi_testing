#include <bits/stdc++.h>  // Include all standard library headers (common in competitive programming)

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

int main() {
    // PERFORMANCE OPTIMIZATIONS:
    std::ios::sync_with_stdio(false); // Faster I/O by uncoupling C++ streams from C streams
    std::cin.tie(nullptr);            // Don't flush output buffer before input
    
    // SEGMENTED SIEVE PARAMETERS:
    // Segment size chosen to fit in CPU cache for optimal performance
    // ARM Cortex-A53: 32KB L1 cache, 512KB L2 cache
    // 500K numbers with bit packing = ~63KB, fits nicely in cache
    const u64 SEG_SIZE = 500'000ULL;
    const auto RUN_FOR = std::chrono::seconds(10); // Run for exactly 10 seconds
    
    // TIMING SETUP:
    const auto t0 = std::chrono::steady_clock::now(); // Start time
    const auto deadline = t0 + RUN_FOR;              // When to stop
    
    // ALGORITHM STATE VARIABLES:
    u64 low = 2;                 // Start of current segment (begin with 2, first prime)
    u64 total_prime_count = 0;   // Running count of all primes found
    
    // BASE PRIMES MANAGEMENT:
    // We need primes up to sqrt(current_range) to sieve each segment
    std::vector<u32> base_primes; // Will store small primes for sieving
    u32 base_limit = 1;          // Track how far our base primes go
    
    // MEMORY OPTIMIZATION:
    // Pre-allocate the bit array once and reuse it for every segment
    // This avoids repeated memory allocation/deallocation
    const size_t bits_needed = (SEG_SIZE + 63) / 64; // Round up division
    std::vector<u64> is_prime_bits(bits_needed);
    
    // WHEEL OPTIMIZATION (mentioned but not fully used in this code):
    // Skip multiples of small primes to reduce work
    const u32 wheel_primes[3] = {2, 3, 5};
    
    // MAIN SEGMENTED SIEVE LOOP:
    // Process segments until time runs out
    while (std::chrono::steady_clock::now() < deadline) {
        u64 high = low + SEG_SIZE; // End of current segment (exclusive)
        
        // UPDATE BASE PRIMES IF NEEDED:
        // We need all primes up to sqrt(high) to properly sieve this segment
        u32 need = (u32)std::sqrt((double)high) + 1;
        if (need > base_limit) {
            // Generate fresh set of base primes
            base_primes = small_primes(need);
            base_limit = need;
        }
        
        // INITIALIZE SEGMENT:
        // Set all bits to 1 (assume all numbers in segment are prime initially)
        std::fill(is_prime_bits.begin(), is_prime_bits.end(), ~0ULL);
        
        // SIEVE THE SEGMENT:
        // For each base prime, mark its multiples in this segment
        for (u32 p : base_primes) {
            // FIND FIRST MULTIPLE:
            // Find smallest multiple of p that's >= low
            u64 start = ((low + p - 1) / p) * p; // Ceiling division trick
            
            // OPTIMIZATION: Start from p*p if it's larger
            // (smaller multiples already handled by smaller primes)
            if (start < (u64)p * p) start = (u64)p * p;
            
            // Skip if this prime's first multiple is beyond our segment
            if (start >= high) continue;
            
            // Convert absolute position to segment-relative position
            u64 j = start - low;
            
            // OPTIMIZED SIEVING WITH LOOP UNROLLING:
            // Different strategies based on prime size
            
            if (p == 2) {
                // SPECIAL CASE FOR 2: Mark all even numbers
                if (low % 2 == 0) {
                    // If segment starts on even number, mark evens starting at 0
                    for (u64 i = 0; i < SEG_SIZE; i += 2) {
                        is_prime_bits[i / 64] &= ~(1ULL << (i % 64));
                    }
                } else {
                    // If segment starts on odd number, mark evens starting at 1
                    for (u64 i = 1; i < SEG_SIZE; i += 2) {
                        is_prime_bits[i / 64] &= ~(1ULL << (i % 64));
                    }
                }
            } else if (p < 64) {
                // LOOP UNROLLING FOR SMALL PRIMES:
                // Process 8 multiples per loop iteration to reduce overhead
                while (j + 7*p < SEG_SIZE) {
                    // Mark 8 consecutive multiples of p
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64)); j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64)); j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64)); j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64)); j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64)); j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64)); j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64)); j += p;
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64)); j += p;
                }
                // Handle remaining multiples that didn't fit in groups of 8
                while (j < SEG_SIZE) {
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                    j += p;
                }
            } else {
                // REGULAR SIEVING FOR LARGER PRIMES:
                // No unrolling needed since these primes have fewer multiples
                while (j < SEG_SIZE) {
                    is_prime_bits[j / 64] &= ~(1ULL << (j % 64));
                    j += p;
                }
            }
        }
        
        // HANDLE SPECIAL CASES:
        // Make sure 0 and 1 are properly marked as non-prime if they're in range
        if (low <= 1) {
            if (low == 0) {
                is_prime_bits[0] &= ~1ULL;  // Clear bit 0 (number 0)
            }
            if (low <= 1 && high > 1) {
                // Clear bit for number 1
                is_prime_bits[(1 - low) / 64] &= ~(1ULL << ((1 - low) % 64));
            }
        }
        
        // COUNT PRIMES IN THIS SEGMENT:
        // Use hardware popcount for ultra-fast bit counting
        u64 segment_primes = 0;
        u64 nums_in_segment = std::min(SEG_SIZE, high - low);
        u64 full_words = nums_in_segment / 64; // How many complete u64s to check
        
        // COUNT COMPLETE 64-BIT WORDS:
        // __builtin_popcountll counts set bits in a u64 in one CPU instruction
        for (size_t i = 0; i < full_words; ++i) {
            segment_primes += __builtin_popcountll(is_prime_bits[i]);
        }
        
        // HANDLE PARTIAL LAST WORD:
        // The last u64 might not be completely filled
        u64 remainder = nums_in_segment % 64;
        if (remainder > 0) {
            // Create mask to only count the bits we care about
            u64 mask = (1ULL << remainder) - 1; // e.g., for remainder=3: 00000111
            segment_primes += __builtin_popcountll(is_prime_bits[full_words] & mask);
        }
        
        // UPDATE TOTALS AND ADVANCE:
        total_prime_count += segment_primes;
        low = high; // Move to next segment
    }
    
    // CALCULATE AND DISPLAY RESULTS:
    auto duration = std::chrono::steady_clock::now() - t0;
    auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() / 1000.0;
    
    std::cout << "Primes found: " << total_prime_count << "\n";
    std::cout << "Time: " << seconds << " seconds\n";
    std::cout << "Numbers/sec: " << (low - 2) / seconds / 1e6 << " million\n";
    std::cout << "Range checked: 2 to " << (low - 1) << "\n";
    
    return 0;
}