#ifndef SHARED_MEMORY_HPP
#define SHARED_MEMORY_HPP

#include <string>
#include <semaphore.h>

#define MAX_RUBRIC_LINES 5
#define MAX_QUESTIONS 5
#define RUBRIC_LINE_SIZE 100
#define MAX_EXAMS 100
#define MAX_FILENAME 50

struct RubricLine {
    int exercise_num;
    char rubric_text;
    char full_line[RUBRIC_LINE_SIZE];
};

struct Exam {
    int student_number;
    bool question_marked[MAX_QUESTIONS];
    bool question_being_marked[MAX_QUESTIONS];
};

struct SharedData {
    RubricLine rubric[MAX_RUBRIC_LINES];
    Exam current_exam;
    int active_tas;
    bool should_terminate;
    
    char exam_files[MAX_EXAMS][MAX_FILENAME];
    int num_exam_files;
    int current_exam_index;
    
    // Semaphores
    sem_t rubric_mutex;
    sem_t rubric_read_count_mutex;
    int rubric_read_count;
    
    sem_t question_mutex[MAX_QUESTIONS];
    sem_t exam_loading_mutex;
};

#endif