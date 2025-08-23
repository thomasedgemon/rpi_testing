so i have been in a professional full stack software development role for a little over a year now.
and lately, as I've become more comfortable in the languages I work with, I have begun to turn my attention
to optimizing and making more performant the things Ive already built. this let me down the rabbit hole, including some 
youtube videos, molly rocket, sheaffication of g, etc. 

so i professionally work mostly with python because it makes the most sense for my usecases, and
you know as well as i do that its about as abstracted away from the OS and hardware as a language can be.
i've only recently begun to dip my toes in to the Golang waters, because it seems to me that, if you
can work in any language of your choice, as least one of them should be a lower level compiled language,
if only to better understand how the code interacts with hardware, memory management, etc etc. this seems
like a must for any self-respecting dev who wants to deeply understand things. but i digress. 

rather than trying to tune a script on my very powewrful work computer, or my relatively fast home computer, 
or one of my one liter cluster computers, but i thought it was more interesting to enforce performance 
and optimization with the hardware itself. trial by c plus plus fire. 

so i bought a raspberry pi zero, which i think is the smallest sbc they make that accepts a flash drive.
ill explain that in a minute. the zero has a single core 1ghz cpu and 512mb of ram and less than a mb of L1 cache. 

so the main question i wanted to answer was how much performance i could get out of an extremely small, low-powered computer, 
with a secondary consideration of storing all the primes it found. if you've watched my previous videos, you
know i have a thing for primes with respect to performance benchmarking. blame my math degree.

while i waited it for jeffrey bezos to deliver it, i cracked open my ancient t480 thinkpad,
and had chatgpt write a cpp script on a single core to find primes. i left it in my office, said good night, and
let it run all night. 

but i am an impatient man. so when i got home, i had gpt write a python script with the same algorithm on 
three cores of a 2.4GHz i5-7500. after 30 minutes, it found 666 million primes (or about 22 million per minute) and took up 7.5 gigs 
of an uncompressed text file with one prime per line.

knowing very little about number theory, especially with respect to primes and their theorems,
i kind of assumed primes became less dense rather quickly, but this is apparently not the case.
the internet says prime density shrinks at about 1 over natural log of n, where n is the average value
in the range youre looking at. so that explains the linearity of primes found per unit time. chatgpt says 
that based on the find rate of my aforementioned python script on three cores, it would have to run over 
a year before any kind of curvature would be noticeable. as for the filesize per unit time, linearity
can be explained by the fact that, while prime numbers slowly become less common, each new prime
found is larger than the previous, which makes for kind of an elegant cancellation. nice!

all of that to say, storing all the primes it might find is definitely out of the question. 
even if it works at one-tenth the search speed of the python script, thats 1400 MB per hour, or 
35 gigs a day. so thats out the window. 

so first up is obvious considerations for optimization. headless seems like an obvious choice, but how will
we know if the script is still running? while writing to a terminal which is displayed on an external screen
doesnt require much of the cpu, its still much more intensive than simply blinking an led once per minute or so.

as an aside, i thought about running this off of a battery to avoid potential power loss, but apparently 
running this for a week straight would require a 500 dollar powerbank. so thats out the window, too.
i settled for plugging it into the apc i already have that my router and modem are plugged into.

using cpp is also pretty much a given. i could use a stripped down version of python, or perhaps even Go,
but python would basically be an immediate gunshot to the knee during a footrace. we want to squeeze
as much out of the cpu as we can. BUT, we arent going to overclock, because thats cheating.

beyond those, tradeoffs start happening. do i want bursts of speed with no record-keeping, or do i
want to store primes? do i want this to be able run indefinitely, if possible? 

to start, we need an algorithm. there are a few quite performant exact ones for finding
all primes up to n, but a segmented sieve is probably the best bet. so lets go with that.

let me explain how a segmented sieve works. for a set of numbers 2 to n, you start at two. two is prime, so you add
that to your list of primes, and then eliminate all of its multiples because you know they cannot also be prime. the next
number is three. three is prime, so add it to the prime list, and remove its multiples too. the next number is five, since four was a 
multiple of two. add it to the prime list, and remove its multiples. you continue this until the list
is exhausted. with each iteration, you are checking against fewer and fewer numbers, which makes this 
an incredibly fast exact algo for large sets of numbers. now, where the segmentation comes in: 

but back to our indefinite run question. let me ruin the party just a little. no, it cannot run forever.
and heres why: with any exact algorithm, you must test at minimum some subset of the numbers less than the
number youre testing. even if, in the slowest possible configuration, you were to test one number at a time,
n and the number youre testing will be too large themselves to hold in ram for the cpu to process. at the extreme end of the
scale, there arent enough atoms in the universe to build enough ram, no matter how efficient
or memory dense it could possibly be, to store them. there is no free lunch. so, while in this context i could 
probably devise some scheme that would let it run for weeks
or months, technically it cannot run forever. so with that, lets just focus on squeezing speed out of this bad boy. 

dessert first: 
generic sieve found 54 million primes in 10s on a four core i7-7500 (2.4ghz) and 32gb ram with no optimizations whatsoever.
segmented sieve highly optimized segmented sieve found 74 million primes in 10s on the raspberry pi zero 2w.

i started out with a half dozen varieties of a sieve: unsegmented, segmented, incorporating hardware limitations, etc, 
but given vibecode and cpp noob status, it was too much to troubleshoot, so i ended up just going with the one which 
seemed the most reliable and fastest. 

i'll talk abput all of the optimizations i did in a moment, but lets skip a little ahead and talk about data validation.
since i dont know cpp nearly well enough to inspect the code directly, my low iq solution was to make another version of the
scipt which searched until it found 250k primes, saved them all to a text file, stopped searching, and turned around and checked each
of those numbers for primality. and every single number passed, so it stands to reason that all 74 million would as well. the algorithm
cannot become more unreliable with time. at worst, it can somehow drop a prime. all of that to say i'm confident in the number,
although at the outset, knowing next to nothing, i think 10 million primes would have impressed me.

but back to the algorithm. 

we already talked about what segmentation is. for this, we made the segments small enough to fit in L1 cache, which requires 
much fewer cpu cycles to access than L2 or ram. 

we skipped all even numbers. pretty straightforward.

multithreading: splitting up a "job" across threads while minimizing as much as possible the overhead multithreading
introduces. all threads are also working from the same set of primes, so no prime is computed more than once.

bit packing: in each segment, we map a number to a bit, which means that for several sub-processes,
we can work with a bit rather than a byte, before finally going back to the integer itself. 

popcount: in the aforementioned mapping from integers to bits, we can count how many are prime
in a single instruction. this is a feature at the hardware level. 

with respect to the compile command flags themselves:

we compile specifically for this type of cpu

we enable SIMD processes (single instruction, multiple data) like in our mapping from integers 
to bits. we can clear all bits at once, rather than one by one. 

-03 flag: aggressively optimizes the code for vectorization and predictive branching. 











