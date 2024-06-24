#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <malloc.h>
#include <windows.h>
#include <stdlib.h>
#include <inttypes.h>

#define TASKS_CAPACITY 50 // calculate from memory
#define CPU_HISTORY_LEN 1000

enum load_state{
    OFF = 0,
    UNDERLOADED,
    BALANCED,
    OVERLOADED,
    FULL
};

enum operation_code{
    OK = 0,
    TASK_LOST,
    FAILED,
};

struct node{
    uint32_t id; // may be IP or hostname?

    float cpu_temp; // in degree Celsius

    uint64_t completion_time; // in milliseconds
    uint32_t tasks[TASKS_CAPACITY]; // IDs of tasks in queue
    size_t tasks_count;
    size_t head_idx;
    size_t tail_idx;

    uint8_t computing_power;
    enum load_state state;
    uint64_t timer;
    struct node* prev;
    struct node* next;
};

struct task{
    uint32_t id;
    uint8_t size;
    int** matrix_a;
    int** matrix_b;
    uint8_t priority; // should raise up every tick
};