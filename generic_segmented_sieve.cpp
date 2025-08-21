#include <iostream>
#include <vector>
#include <chrono>
#include <cmath>
#include <algorithm>

template<typename T>
class SegmentedSieve {
private:
    std::vector<T> base_primes;
    T segment_size;
    T current_low;
    T current_high;
    
    void generate_base_primes(T limit) {
        T sqrt_limit = static_cast<T>(std::sqrt(limit)) + 1;
        std::vector<bool> is_prime(sqrt_limit + 1, true);
        is_prime[0] = is_prime[1] = false;
        
        for (T i = 2; i * i <= sqrt_limit; ++i) {
            if (is_prime[i]) {
                for (T j = i * i; j <= sqrt_limit; j += i) {
                    is_prime[j] = false;
                }
            }
        }
        
        base_primes.clear();
        for (T i = 2; i <= sqrt_limit; ++i) {
            if (is_prime[i]) {
                base_primes.push_back(i);
            }
        }
    }
    
    std::vector<T> sieve_segment(T low, T high) {
        std::vector<T> primes_in_segment;
        T range = high - low + 1;
        std::vector<bool> is_prime(range, true);
        
        // Handle the case where low is 0 or 1
        if (low <= 1) {
            for (T i = 0; i < std::min(static_cast<T>(2), range); ++i) {
                is_prime[i] = false;
            }
        }
        
        // Mark multiples of each base prime
        for (T prime : base_primes) {
            if (prime * prime > high) break;
            
            // Find the first multiple of prime >= low
            T start = std::max(prime * prime, (low + prime - 1) / prime * prime);
            
            // Mark all multiples of prime in [low, high]
            for (T j = start; j <= high; j += prime) {
                is_prime[j - low] = false;
            }
        }
        
        // Collect primes from this segment
        for (T i = 0; i < range; ++i) {
            if (is_prime[i]) {
                T candidate = low + i;
                if (candidate >= 2) {
                    primes_in_segment.push_back(candidate);
                }
            }
        }
        
        return primes_in_segment;
    }

public:
    SegmentedSieve(T initial_segment_size = 100000) 
        : segment_size(initial_segment_size), current_low(0), current_high(-1) {
        // Generate base primes up to sqrt of a reasonable large number
        generate_base_primes(10000000); // sqrt of ~10^14
    }
    
    std::vector<T> get_next_segment() {
        current_low = current_high + 1;
        current_high = current_low + segment_size - 1;
        
        // Regenerate base primes if we need more
        T sqrt_high = static_cast<T>(std::sqrt(current_high)) + 1;
        if (!base_primes.empty() && base_primes.back() < sqrt_high) {
            generate_base_primes(current_high);
        }
        
        return sieve_segment(current_low, current_high);
    }
    
    T find_largest_prime_up_to(T limit) {
        T largest_prime = 2;
        current_low = 0;
        current_high = -1;
        
        while (current_high < limit) {
            current_low = current_high + 1;
            current_high = std::min(current_low + segment_size - 1, limit);
            
            // Regenerate base primes if needed
            T sqrt_high = static_cast<T>(std::sqrt(current_high)) + 1;
            if (!base_primes.empty() && base_primes.back() < sqrt_high) {
                generate_base_primes(current_high);
            }
            
            auto segment_primes = sieve_segment(current_low, current_high);
            if (!segment_primes.empty()) {
                largest_prime = segment_primes.back();
            }
        }
        
        return largest_prime;
    }
    
    void set_segment_size(T new_size) {
        segment_size = new_size;
    }
    
    T get_current_range() const {
        return current_high;
    }
    
    size_t get_base_primes_count() const {
        return base_primes.size();
    }
};

int main() {
    const auto start_time = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::seconds(10);
    
    // Use long long for larger range support
    SegmentedSieve<long long> sieve(1000000); // 1M numbers per segment
    long long largest_prime = 2;
    long long search_limit = 1000000;
    
    std::cout << "Running Segmented Sieve of Eratosthenes for 10 seconds..." << std::endl;
    std::cout << "Segment size: " << 1000000 << " numbers" << std::endl;
    
    int iteration = 0;
    while (true) {
        auto current_time = std::chrono::high_resolution_clock::now();
        if (current_time - start_time >= duration) {
            break;
        }
        
        // Find largest prime up to current search limit
        long long found_prime = sieve.find_largest_prime_up_to(search_limit);
        if (found_prime > largest_prime) {
            largest_prime = found_prime;
        }
        
        // Exponentially increase search range
        search_limit *= 2;
        
        // Print progress every few iterations
        if (++iteration % 3 == 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                current_time - start_time).count();
            std::cout << "Time: " << elapsed << "ms, "
                      << "Search limit: " << search_limit/2 << ", "
                      << "Largest prime: " << largest_prime << ", "
                      << "Base primes: " << sieve.get_base_primes_count() << std::endl;
        }
        
        // Adaptive segment size - increase for larger ranges
        if (search_limit > 10000000) {
            sieve.set_segment_size(std::min(static_cast<long long>(10000000), 
                                          search_limit / 100));
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    std::cout << "\nExecution completed!" << std::endl;
    std::cout << "Time elapsed: " << elapsed << " ms" << std::endl;
    std::cout << "Final search limit: " << search_limit/2 << std::endl;
    std::cout << "Largest prime found: " << largest_prime << std::endl;
    std::cout << "Base primes used: " << sieve.get_base_primes_count() << std::endl;
    
    return 0;
}