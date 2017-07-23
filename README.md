# lockfree_hashtable



This is a lock free/wait free hashtable implemented in C. There is an alternative C++ template based 
implementation in the https://github.com/larytet/emcpp
The hashtable's original goal is to replace SystemTap's associative arrays. The hashtable reduces the 
probes latency by 30% and more depending on the scenario.



## Limitations   

#  Key is of integral type
#  A single context is allowed to insert/remove a specific key. Many contexts can insert/remove different keys.
#  GCC is assumed 
 
## Compile

* 'make' should be enough to build the unitest
*  Load and run the SystemTap module: sudo stap -g -v -k --suppress-time-limits -D MAXSKIPPED=0  dup_probe.stp 
*  Shell script echo_test.sh loads a multicore system and generates significant amount of system calls
*  The hashtable is in hashtable.h


## Performance

The hashtable hits 50M API calls per second on a single i7 core at cost of ~20nanos for an API call.