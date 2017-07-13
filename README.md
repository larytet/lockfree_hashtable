# lockfree_hashtable

Compile

* 'make' should be enough to build the unitest
*  Load and run the SystemTap module: sudo stap -g -v -k --suppress-time-limits -D MAXSKIPPED=0  dup_probe.stp 
*  Shell script echo_test.sh loads a multicore system and generates significant amount of system calls
*  The hashtable is in hashtable.h
