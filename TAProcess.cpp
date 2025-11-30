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
#include <semaphore.h>
#include "SharedMemory.hpp"

using namespace std;

const char* SHM_NAME = "/ta_marking_shm";

// timeout
const int DEADLOCK_TIMEOUT = 2;

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

    cout << "TA Exam Marking System" << endl;
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

    //error message
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

    //init
    sem_init(&shared->rubric_mutex, 1, 1);
    sem_init(&shared->rubric_read_count_mutex, 1, 1);
    shared->rubric_read_count = 0;
    
    for (int i = 0; i < MAX_QUESTIONS; i++) {
        sem_init(&shared->question_mutex[i], 1, 1);
    }
    sem_init(&shared->exam_loading_mutex, 1, 1);

    cout << "Semaphores initialized for synchronization" << endl << endl;

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

    cout << "\nAll TAs have finished marking" << endl;

    sem_destroy(&shared->rubric_mutex);
    sem_destroy(&shared->rubric_read_count_mutex);
    for (int i = 0; i < MAX_QUESTIONS; i++) {
        sem_destroy(&shared->question_mutex[i]);
    }
    sem_destroy(&shared->exam_loading_mutex);

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
        cerr << "Error: no input directory" << endl;
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
        cerr << "Error: no exam files found in Input directory" << endl;
        return false;
    }

    sort(exam_filenames.begin(), exam_filenames.end());

    shared->num_exam_files = min((int)exam_filenames.size(), MAX_EXAMS);
    for (int i = 0; i < shared->num_exam_files; i++) {
        strncpy(shared->exam_files[i], exam_filenames[i].c_str(), MAX_FILENAME - 1);
        shared->exam_files[i][MAX_FILENAME - 1] = '\0';
    }

    cout << "Found " << shared->num_exam_files << " exam files in Input directory" << endl;
    return true;
}

bool loadRubricToSharedMemory(SharedData* shared) {
    ifstream file("rubric.txt");
    if (!file.is_open()) {
        cerr << "Error: could not open rubric.txt" << endl;
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
        return false;
    }

    const char* filename = shared->exam_files[exam_index];
    
    ifstream file(filename);
    //error message
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
        shared->current_exam.question_being_marked[i] = false;
    }
    shared->current_exam_index = exam_index;

    cout << "Accessing exam " << shared->current_exam.student_number << endl;

    //last exam is 9999
    if (shared->current_exam.student_number == 9999) {
        shared->should_terminate = true;
        cout << "Termination exam (9999) reached. Stopping all TAs." << endl;
    }

    return true;
}

void taProcess(int ta_id, SharedData* shared) {
    cout << "TA " << ta_id << " started" << endl;
    
    time_t last_progress = time(NULL);

    while (!shared->should_terminate) {
        time_t current_time = time(NULL);
        if (difftime(current_time, last_progress) > DEADLOCK_TIMEOUT) {
            cout << "*** POTENTIAL DEADLOCK DETECTED by TA " << ta_id 
                << " (no progress for " << DEADLOCK_TIMEOUT << " seconds) ***" << endl;
            break;
        }

        checkAndCorrectRubric(ta_id, shared);

        bool exam_complete = false;
        while (!exam_complete && !shared->should_terminate) {
            bool marked = markQuestion(ta_id, shared);
            
            if (marked) {
                last_progress = time(NULL);
            }
            
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

                    sem_wait(&shared->exam_loading_mutex);
                    
                    if (shared->current_exam_index < shared->num_exam_files - 1) {
                        int next_exam = shared->current_exam_index + 1;
                        if (!loadExamToSharedMemory(shared, next_exam)) {
                            shared->should_terminate = true;
                        }
                        last_progress = time(NULL);
                    } else {
                        shared->should_terminate = true;
                    }
                    
                    sem_post(&shared->exam_loading_mutex);
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

    sem_wait(&shared->rubric_read_count_mutex);
    shared->rubric_read_count++;
    if (shared->rubric_read_count == 1) {

        sem_wait(&shared->rubric_mutex);
    }
    sem_post(&shared->rubric_read_count_mutex);

    cout << "TA " << ta_id << " accessing rubric" << endl;

    for (int i = 0; i < MAX_RUBRIC_LINES; i++) {
        //random delay
        usleep(randomDelay(500000, 1000000));

        //30% chance of correction
        bool needs_correction = (rand() % 100) < 30;

        if (needs_correction) {

            sem_wait(&shared->rubric_read_count_mutex);
            shared->rubric_read_count--;
            if (shared->rubric_read_count == 0) {
                sem_post(&shared->rubric_mutex);
            }
            sem_post(&shared->rubric_read_count_mutex);

            sem_wait(&shared->rubric_mutex);
            
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
            
            sem_post(&shared->rubric_mutex);

            sem_wait(&shared->rubric_read_count_mutex);
            shared->rubric_read_count++;
            if (shared->rubric_read_count == 1) {
                sem_wait(&shared->rubric_mutex);
            }
            sem_post(&shared->rubric_read_count_mutex);
        }
    }

    sem_wait(&shared->rubric_read_count_mutex);
    shared->rubric_read_count--;
    if (shared->rubric_read_count == 0) {
        sem_post(&shared->rubric_mutex);
    }
    sem_post(&shared->rubric_read_count_mutex);
}

bool markQuestion(int ta_id, SharedData* shared) {
    for (int i=0; i < MAX_QUESTIONS; i++) {
        if (sem_trywait(&shared->question_mutex[i]) == 0) {

            if (!shared->current_exam.question_marked[i] && 
                !shared->current_exam.question_being_marked[i]) {
                
                shared->current_exam.question_being_marked[i] = true;
                
                cout << "TA " << ta_id << " marking question " << (i + 1) 
                    << " of exam " << shared->current_exam.student_number << endl;

                sem_post(&shared->question_mutex[i]);
                
                usleep(randomDelay(1000000, 2000000));

                sem_wait(&shared->question_mutex[i]);
                shared->current_exam.question_marked[i] = true;
                shared->current_exam.question_being_marked[i] = false;
                sem_post(&shared->question_mutex[i]);

                cout << "TA " << ta_id << " finished marking question " << (i + 1)
                    << " of exam " << shared->current_exam.student_number << endl;

                return true;
            } else {
                sem_post(&shared->question_mutex[i]);
            }
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