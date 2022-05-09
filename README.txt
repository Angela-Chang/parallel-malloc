Compile instructions (this will build the test binary).

gcc -pthread -D [MODE] -D NUMTHREADS=[n] *.c

Where mode is one of {TEST_NAIVE, TEST_ARENA_ONLY, TEST_ARENA_CACHED} and n is the number of threads


VIDEO PRESENTATION

https://youtu.be/4aoed-JTriA