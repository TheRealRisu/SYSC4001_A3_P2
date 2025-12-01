# How to run the file

1. Confirm rubric.txt exists
2. Confirm there are files called exam_####.txt inside of Input folder
3. Confirm there is a exam_9999.txt inside of Input folder (to end the process)


4. In a linux terminal, navigate to this directory and run

g++ -std=c++11 -Wall -pthread TAProcess_101296691_101304731.cpp -o ta_marking -lrt

A file named "ta_marking" should appear

5. In the same directory, run (Replace the "#" with a number, the number must be bigger or equal to 2_

./ta_marking #

6. Code runs and logs the process to terminal.

# PSA Part A and B

Part B is the current implementation of the code seen through the TAProcess_101296691_101304731.cpp and SharedMemory_101296691_101304731.cpp files. This version is the refined version of part A.

Part A is the first implementation of the code. The very first commit, titled part a, holds the original version of part A, before it was refined with semaphores.
