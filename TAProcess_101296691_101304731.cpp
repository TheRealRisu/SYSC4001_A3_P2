#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#include <sys/wait.h>
#include <dirent.h>
#include <vector>
#include <algorithm>
#include "SharedMemory_101296691_101304731.hpp"

using namespace std;

const char* SHM_NAME = "/ta_marking_shm";

void initializeSharedMemory(SharedData* shared);
bool discoverExamFiles(SharedData* shared);
bool loadRubricToSharedMemory(SharedData* shared);
bool loadExamToSharedMemory(SharedData* shared, int exam_index);
void taProcess(int ta_id, SharedData* shared);
void checkAndCorrectRubric(int ta_id, SharedData* shared);
bool markQuestion(int ta_id, SharedData* shared);
void saveRubricToFile(SharedData* shared);
double randomDelay(double min, double max);

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <number_of_TAs>" << endl;
        return 1;
    }

    int num_tas = atoi(argv[1]);
    if (num_tas < 2) {
        cerr << "Number of TAs must be at least 2" << endl;
        return 1;
    }

    cout << "=== TA Exam Marking System ===" << endl;
    cout << "Number of TAs: " << num_tas << endl << endl;

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        return 1;
    }

    if (ftruncate(shm_fd, sizeof(SharedData)) == -1) {
        perror("ftruncate");
        return 1;
    }

    SharedData* shared = (SharedData*)mmap(NULL, sizeof(SharedData),
        PROT_READ | PROT_WRITE,
        MAP_SHARED, shm_fd, 0);
    if (shared == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    initializeSharedMemory(shared);

    if (!discoverExamFiles(shared)) {
        cerr << "Failed to discover exam files" << endl;
        return 1;
    }

    if (!loadRubricToSharedMemory(shared)) {
        cerr << "Failed to load rubric file" << endl;
        return 1;
    }

    if (!loadExamToSharedMemory(shared, 0)) {
        cerr << "Failed to load first exam file" << endl;
        return 1;
    }

    shared->active_tas = num_tas;

    for (int i = 0; i < num_tas; i++) {
        pid_t pid = fork();
        
        if (pid < 0) {
            perror("fork");
            return 1;
        } else if (pid == 0) {
            srand(time(NULL) + getpid());
            taProcess(i + 1, shared);
            exit(0);
        }
    }

    for (int i = 0; i < num_tas; i++) {
        wait(NULL);
    }

    cout << "\n=== All TAs have finished marking ===" << endl;

    munmap(shared, sizeof(SharedData));
    close(shm_fd);
    shm_unlink(SHM_NAME);

    return 0;
}

void initializeSharedMemory(SharedData* shared) {
    memset(shared, 0, sizeof(SharedData));
    shared->should_terminate = false;
    shared->num_exam_files = 0;
    shared->current_exam_index = 0;
}

bool discoverExamFiles(SharedData* shared) {
    DIR* dir;
    struct dirent* entry;
    vector<string> exam_filenames;

    dir = opendir("Input");
    if (dir == NULL) {
        cerr << "Error: Could not open Input directory" << endl;
        return false;
    }

    //find exams
    while ((entry = readdir(dir)) != NULL) {
        string filename = entry->d_name;
        if (filename.find("exam_") == 0 && filename.find(".txt") != string::npos) {
            exam_filenames.push_back("Input/" + filename);
        }
    }
    closedir(dir);

    if (exam_filenames.empty()) {
        cerr << "Error: No exam files found in Input directory (exam_*.txt)" << endl;
        return false;
    }

    sort(exam_filenames.begin(), exam_filenames.end());

    shared->num_exam_files = min((int)exam_filenames.size(), MAX_EXAMS);
    for (int i = 0; i < shared->num_exam_files; i++) {
        strncpy(shared->exam_files[i], exam_filenames[i].c_str(), MAX_FILENAME - 1);
        shared->exam_files[i][MAX_FILENAME - 1] = '\0';
    }

    cout << "Discovered " << shared->num_exam_files << " exam files in Input directory" << endl;
    return true;
}

bool loadRubricToSharedMemory(SharedData* shared) {
    ifstream file("rubric.txt");
    if (!file.is_open()) {
        cerr << "Error: Could not open rubric.txt" << endl;
        return false;
    }

    string line;
    int index = 0;
    
    while (getline(file, line) && index < MAX_RUBRIC_LINES) {
        size_t comma_pos = line.find(',');
        if (comma_pos != string::npos) {
            shared->rubric[index].exercise_num = stoi(line.substr(0, comma_pos));
            
            size_t text_pos = line.find_first_not_of(" \t", comma_pos + 1);
            if (text_pos != string::npos) {
                shared->rubric[index].rubric_text = line[text_pos];
            }
            
            strncpy(shared->rubric[index].full_line, line.c_str(), RUBRIC_LINE_SIZE - 1);
            shared->rubric[index].full_line[RUBRIC_LINE_SIZE - 1] = '\0';
        }
        index++;
    }

    file.close();
    cout << "Rubric loaded into shared memory" << endl;
    return true;
}

bool loadExamToSharedMemory(SharedData* shared, int exam_index) {
    if (exam_index >= shared->num_exam_files) {
        cout << "No more exams to load" << endl;
        return false;
    }

    const char* filename = shared->exam_files[exam_index];
    
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Could not open " << filename << endl;
        return false;
    }

    string line;
    if (getline(file, line)) {
        shared->current_exam.student_number = stoi(line);
    } else {
        file.close();
        return false;
    }

    file.close();

    for (int i = 0; i < MAX_QUESTIONS; i++) {
        shared->current_exam.question_marked[i] = false;
    }
    shared->current_exam_index = exam_index;

    cout << "Accessing exam " << shared->current_exam.student_number << endl;

    if (shared->current_exam.student_number == 9999) {
        shared->should_terminate = true;
        cout << "Termination exam (9999) reached. Stopping all TAs." << endl;
    }

    return true;
}

void taProcess(int ta_id, SharedData* shared) {
    cout << "TA " << ta_id << " started" << endl;

    while (!shared->should_terminate) {
        checkAndCorrectRubric(ta_id, shared);

        bool exam_complete = false;
        while (!exam_complete && !shared->should_terminate) {
            bool marked = markQuestion(ta_id, shared);
            
            if (!marked) {
                bool all_marked = true;
                for (int i = 0; i < MAX_QUESTIONS; i++) {
                    if (!shared->current_exam.question_marked[i]) {
                        all_marked = false;
                        break;
                    }
                }

                if (all_marked) {
                    exam_complete = true;
                    
                    int next_exam = shared->current_exam_index + 1;
                    if (!loadExamToSharedMemory(shared, next_exam)) {
                        shared->should_terminate = true;
                    }
                    break;
                } else {
                    usleep(100000);
                }
            }
        }
    }

    cout << "TA " << ta_id << " finished" << endl;
}

void checkAndCorrectRubric(int ta_id, SharedData* shared) {
    cout << "TA " << ta_id << " accessing rubric" << endl;

    for (int i = 0; i < MAX_RUBRIC_LINES; i++) {

        //correction delay
        usleep(randomDelay(500000, 1000000));

        //30% chance for correction
        bool needs_correction = (rand() % 100) < 30;

        if (needs_correction) {
            char old_text = shared->rubric[i].rubric_text;
            shared->rubric[i].rubric_text = old_text + 1;


            size_t comma_pos = string(shared->rubric[i].full_line).find(',');
            if (comma_pos != string::npos) {
                size_t text_pos = string(shared->rubric[i].full_line)
                                    .find_first_not_of(" \t", comma_pos + 1);
                if (text_pos != string::npos) {
                    shared->rubric[i].full_line[text_pos] = shared->rubric[i].rubric_text;
                }
            }

            cout << "TA " << ta_id << " correcting rubric exercise " 
                 << shared->rubric[i].exercise_num 
                 << " ('" << old_text << "' -> '" 
                 << shared->rubric[i].rubric_text << "')" << endl;

            saveRubricToFile(shared);
        }
    }
}

bool markQuestion(int ta_id, SharedData* shared) {
    for (int i = 0; i < MAX_QUESTIONS; i++) {
        // change in part b
        if (!shared->current_exam.question_marked[i]) {
            
            cout << "TA " << ta_id << " marking question " << (i + 1) 
                 << " of exam " << shared->current_exam.student_number << endl;

            //simulate
            usleep(randomDelay(1000000, 2000000));

            shared->current_exam.question_marked[i] = true;

            cout << "TA " << ta_id << " finished marking question " << (i + 1)
                 << " of exam " << shared->current_exam.student_number << endl;

            return true;
        }
    }

    return false;
}

void saveRubricToFile(SharedData* shared) {
    ofstream file("rubric.txt");
    if (!file.is_open()) {
        cerr << "Error: Could not save rubric to file" << endl;
        return;
    }

    for (int i = 0; i < MAX_RUBRIC_LINES; i++) {
        file << shared->rubric[i].full_line << endl;
    }

    file.close();
}

double randomDelay(double min, double max) {
    double range = max - min;
    double random = ((double)rand() / RAND_MAX) * range + min;
    return random;
}