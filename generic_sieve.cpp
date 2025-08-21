#include <iostream>
#include <vector>
#include <chrono>
#include <cmath>

template<typename T>
class SieveOfEratosthenes {
private:
    std::vector<bool> is_prime;
    T current_limit;
    T last_checked;

public:
    SieveOfEratosthenes(T initial_limit = 1000) 
        : current_limit(initial_limit), last_checked(1) {
        is_prime.resize(current_limit + 1, true);
        is_prime[0] = is_prime[1] = false;
    }

    void extend_sieve(T new_limit) {
        if (new_limit <= current_limit) return;
        
        T old_limit = current_limit;
        current_limit = new_limit;
        is_prime.resize(current_limit + 1, true);
        
        // Sieve the new range
        T sqrt_limit = static_cast<T>(std::sqrt(current_limit));
        
        for (T i = 2; i <= sqrt_limit; ++i) {
            if (is_prime[i]) {
                // Start marking from the first multiple in the new range
                T start = std::max(i * i, (old_limit / i) * i);
                if (start < old_limit) start += i;
                
                for (T j = start; j <= current_limit; j += i) {
                    is_prime[j] = false;
                }
            }
        }
    }

    T find_next_prime() {
        while (last_checked < current_limit) {
            last_checked++;
            if (is_prime[last_checked]) {
                return last_checked;
            }
        }
        return 0; // No more primes in current range
    }

    T get_largest_prime_in_range() {
        for (T i = current_limit; i >= 2; --i) {
            if (is_prime[i]) {
                return i;
            }
        }
        return 2;
    }

    bool is_prime_number(T n) {
        if (n > current_limit) {
            extend_sieve(n * 2); // Extend sieve beyond n
        }
        return n >= 2 && is_prime[n];
    }

    T get_current_limit() const { return current_limit; }
};

int main() {
    const auto start_time = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::seconds(10);
    
    // Use long long to handle larger primes
    SieveOfEratosthenes<long long> sieve(10000);
    long long largest_prime = 2;
    long long current_limit = 10000;
    
    std::cout << "Running Sieve of Eratosthenes for 10 seconds..." << std::endl;
    
    while (true) {
        auto current_time = std::chrono::high_resolution_clock::now();
        if (current_time - start_time >= duration) {
            break;
        }
        
        // Extend the sieve range
        current_limit *= 2;
        sieve.extend_sieve(current_limit);
        
        // Find the largest prime in the current range
        largest_prime = sieve.get_largest_prime_in_range();
        
        // Optional: print progress every few iterations
        static int iteration = 0;
        if (++iteration % 5 == 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                current_time - start_time).count();
            std::cout << "Time: " << elapsed << "ms, Range: " << current_limit 
                      << ", Largest prime so far: " << largest_prime << std::endl;
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    std::cout << "\nExecution completed!" << std::endl;
    std::cout << "Time elapsed: " << elapsed << " ms" << std::endl;
    std::cout << "Final search range: " << current_limit << std::endl;
    std::cout << "Largest prime found: " << largest_prime << std::endl;
    
    return 0;
}