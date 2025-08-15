
#!/usr/bin/env python3
import math
import os
import time
import multiprocessing as mp
from typing import List

#three cores on i5-7400 w/ 32gb ram


# === CONFIG ===
# Set this to your flash drive path.
#   Linux:  OUTPUT_PATH = "/media/youruser/FLASH/primes.txt"
#   macOS:  OUTPUT_PATH = "/Volumes/FLASH/primes.txt"
#   Windows: OUTPUT_PATH = r"E:\primes.txt"
OUTPUT_PATH = "/media/tom/FLASH/primes.txt"

# Performance knobs
SEGMENT_SIZE = 50_000_000   # numbers per segment
N_PROCESSES = 3             # use 3 cores
REPORT_EVERY_SEC = 60       # print progress once per minute
BATCH_WRITE_SIZE = 1_000    # buffer this many primes before writing
FSYNC_INTERVAL_SEC = 30     # fsync at most every 30s (reduces I/O overhead)

# ---------- Core sieve utilities ----------
def simple_sieve(limit: int) -> List[int]:
    """Return all primes <= limit using a classic sieve (single-threaded)."""
    if limit < 2:
        return []
    sieve = bytearray(b"\x01") * (limit + 1)
    sieve[0:2] = b"\x00\x00"
    root = int(math.isqrt(limit))
    for p in range(2, root + 1):
        if sieve[p]:
            step_start = p * p
            sieve[step_start:limit + 1:p] = b"\x00" * (((limit - step_start) // p) + 1)
    return [i for i, is_prime in enumerate(sieve) if is_prime]

def sieve_segment(low: int, high: int, base_primes: List[int]) -> List[int]:
    """
    Segmented sieve on [low, high), using base_primes (all primes <= sqrt(high-1)).
    Returns the primes in the segment.
    """
    n = high - low
    if n <= 0:
        return []
    seg = bytearray(b"\x01") * n

    # Handle numbers < 2
    if low <= 0:
        for x in range(min(2 - low, n)):
            seg[x] = 0
    elif low == 1:
        seg[0] = 0

    for p in base_primes:
        p2 = p * p
        if p2 >= high:
            start = ((low + p - 1) // p) * p
        else:
            start = max(p2, ((low + p - 1) // p) * p)
        for m in range(start, high, p):
            seg[m - low] = 0

    return [low + i for i, b in enumerate(seg) if b]

# ---------- Runner ----------
def main():
    os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
    f = open(OUTPUT_PATH, "a", encoding="utf-8", buffering=1)

    # Progress & buffering
    total_found = 0
    out_buffer: List[int] = []
    last_report = time.time()
    last_total_minute = 0
    last_fsync = time.time()

    # Base primes cache
    base_primes_cache: List[int] = []
    base_limit_cached = 1

    next_low = 2
    pool = mp.Pool(processes=N_PROCESSES)

    def flush_batches(force_all: bool = False):
        """Write buffered primes in batches; fsync at most every FSYNC_INTERVAL_SEC or on force."""
        nonlocal out_buffer, last_fsync
        wrote_any = False
        while len(out_buffer) >= BATCH_WRITE_SIZE or (force_all and out_buffer):
            batch = out_buffer[:BATCH_WRITE_SIZE]
            del out_buffer[:BATCH_WRITE_SIZE]
            f.write("\n".join(map(str, batch)) + "\n")
            f.flush()
            wrote_any = True
        if wrote_any and (force_all or (time.time() - last_fsync >= FSYNC_INTERVAL_SEC)):
            try:
                os.fsync(f.fileno())
            except OSError:
                pass
            last_fsync = time.time()

    print(f"Writing primes to: {OUTPUT_PATH}")
    print(f"Using {N_PROCESSES} processes, segment size {SEGMENT_SIZE:,}.")
    print("Press Ctrl+C to stop; progress will be saved.\n")

    try:
        tasks: List[mp.pool.ApplyResult] = []

        while True:
            # Keep the queue filled
            while len(tasks) < N_PROCESSES:
                low = next_low
                high = low + SEGMENT_SIZE
                needed_base = int(math.isqrt(high - 1)) if high > 1 else 1
                if needed_base > base_limit_cached:
                    base_primes_cache = simple_sieve(needed_base)
                    base_limit_cached = needed_base
                tasks.append(pool.apply_async(sieve_segment, (low, high, base_primes_cache)))
                next_low = high

            # Collect finished tasks
            ready_idx = [i for i, t in enumerate(tasks) if t.ready()]
            if not ready_idx:
                time.sleep(0.01)
            else:
                for i in sorted(ready_idx, reverse=True):
                    primes = tasks[i].get()
                    tasks.pop(i)
                    total_found += len(primes)
                    out_buffer.extend(primes)
                flush_batches(force_all=False)

            # Periodic progress
            now = time.time()
            if now - last_report >= REPORT_EVERY_SEC:
                delta = total_found - last_total_minute
                timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
                print(f"[{timestamp}] Primes found so far: {total_found:,}  (+{delta:,} in last {REPORT_EVERY_SEC}s)")
                last_total_minute = total_found
                last_report = now

    except KeyboardInterrupt:
        print("\nStopping… finalizing writes.")
    finally:
        try:
            # Drain any finished tasks
            for t in list(tasks):
                if t.ready():
                    primes = t.get()
                    total_found += len(primes)
                    out_buffer.extend(primes)
            flush_batches(force_all=True)
        finally:
            pool.terminate()
            pool.join()
            f.close()
            print(f"Total primes written: {total_found:,}")
            print(f"Saved to: {OUTPUT_PATH}")

if __name__ == "__main__":
    mp.freeze_support()
    main()
