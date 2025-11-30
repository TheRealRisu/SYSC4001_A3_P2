# How to run the file

1. Confirm rubric.txt exists
2. Confirm there are files called exam_####.txt inside of Input folder
3. Confirm there is a exam_9999.txt inside of Input folder (to end the process)


4. In a linux terminal, navigate to this directory and run

g++ -std=c++11 -Wall -pthread TAProcess.cpp -o ta_marking -lrt

A file named "ta_marking" should appear

5. In the same directory, run (Replace the "#" with a number, the number must be bigger or equal to 2_

./ta_marking #

6. Code runs and logs the process to terminal.
