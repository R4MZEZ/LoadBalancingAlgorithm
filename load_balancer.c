#include "load_balancer.h"

size_t nodes_count = 5; // update via network stats
struct node* head;
float overload_bound = 0.95f;
float underload_bound = 0.3f;
uint32_t tick_value = 43000;
size_t migrations = 0;

float ram_usage_history[CPU_HISTORY_LEN];
uint8_t tasks_sizes[1000] = {0};
uint8_t overloaded_exist = 0;
uint8_t off_count = 0;

struct node* create_node(uint32_t id, uint8_t computing_power){
    struct node* node = malloc(sizeof(struct node));
    node->id = id;
    node->cpu_temp = 0;
    node->completion_time = 0;
    node->tasks_count = 0;
    node->head_idx = 0;
    node->tail_idx = 0;
    node->computing_power = computing_power;
    node->state = UNDERLOADED;
    node->prev = NULL;
    node->next = NULL;
    node->timer = 0;
    return node;
}

void send_task(struct task task, struct node* node){
    node->completion_time += pow(task.size,3)*(2-node->computing_power);
    if (node->tasks_count == 0)
        node->tasks[node->tail_idx] = task.id;
    else{
        node->tail_idx = node->tail_idx == TASKS_CAPACITY - 1 ? 0 : ++node->tail_idx;
        node->tasks[node->tail_idx] = task.id;
    }
    node->tasks_count += 1; 
}

size_t delete_last_task(struct node* target){

    size_t task_id = target->tasks[target->tail_idx];
    if (--target->tasks_count != 0)
        target->tail_idx = target->tail_idx == 0 ? TASKS_CAPACITY-1 : --target->tail_idx;
    target->completion_time -= pow(tasks_sizes[task_id], 3);
    return task_id;
}

void calc_new_position(struct node* inserted_node){
    if (inserted_node->state == OFF){
        // do nothing
    }
    else if (inserted_node->tasks_count == TASKS_CAPACITY){
        inserted_node->state = FULL;
    }else if (inserted_node->tasks_count > TASKS_CAPACITY * overload_bound){
        inserted_node->state = OVERLOADED;
    }else if (inserted_node->tasks_count < TASKS_CAPACITY * underload_bound){
        inserted_node->state = UNDERLOADED;
    }else{
        inserted_node->state = BALANCED;
    }

    if ((inserted_node->next == NULL || inserted_node->completion_time <= inserted_node->next->completion_time) &&
        (inserted_node->prev == NULL || inserted_node->completion_time >= inserted_node->prev->completion_time)){
            return; // already on right place
        }
    if (inserted_node->prev != NULL)
        inserted_node->prev->next = inserted_node->next;
    if (inserted_node->next != NULL){
        inserted_node->next->prev = inserted_node->prev;
        if (inserted_node->prev == NULL){ // head
            head = inserted_node->next;
        }
    }
    struct node* next_node = head;
    while (next_node != NULL && inserted_node->completion_time >= next_node->completion_time){
        if (next_node->next == NULL){
            inserted_node->next = NULL;
            inserted_node->prev = next_node;
            next_node->next = inserted_node;
            return;
        }
        next_node = next_node->next;
    }

    inserted_node->next = next_node;
    inserted_node->prev = next_node->prev;
    if (next_node->prev != NULL)
        next_node->prev->next = inserted_node;
    else
        head = inserted_node;
    next_node->prev = inserted_node;
}

enum operation_code assign_new_task(struct node* target, struct task new_task){
    if (target->tasks_count == TASKS_CAPACITY)
        return TASK_LOST;
    else{
        send_task(new_task, target);
        calc_new_position(target);
        return OK;
    }
    
}

void recalculate_borders(uint8_t sensetivity){
    float sum = 0;
    for (size_t i = 0; i < CPU_HISTORY_LEN; i++) {
        sum += ram_usage_history[i];
    }

    float mean = sum / CPU_HISTORY_LEN;
    float values = 0;
 
    for (size_t i = 0; i < CPU_HISTORY_LEN; i++) {
        values += pow(ram_usage_history[i] - mean, 2);
    }
    float variance = values / CPU_HISTORY_LEN;
    float standardDeviation = sqrt(variance);

    overload_bound = mean + sensetivity * standardDeviation;
    float tmp = mean - sensetivity * standardDeviation;
    underload_bound = tmp > 0.3 ? 0.3 : tmp;
}

void swap_task(struct node* src, struct node* dst){
    size_t migrated_task_id = delete_last_task(src);
    calc_new_position(src);
    struct task migrated_task = {migrated_task_id, tasks_sizes[migrated_task_id], NULL, NULL, 0};
    send_task(migrated_task, dst);
    calc_new_position(dst);
    migrations++;
}

enum operation_code migrate_from_overloaded(struct node* src_node){
    struct node* underload_target = head;
    struct node* balanced_target = NULL;
    struct node* overloaded_target = NULL;
    while (underload_target != NULL && underload_target->state != UNDERLOADED){
        if (underload_target->state == BALANCED)
            balanced_target = underload_target;
        else if (src_node != underload_target && underload_target->state == OVERLOADED)
            overloaded_target = underload_target;
        underload_target = underload_target->next;
    }

    struct node* target;

    if (underload_target == NULL){
        if (balanced_target == NULL){
            if (overloaded_target == NULL){
                return FAILED;
            }else{
                target = overloaded_target;
            }
        }else{
            target = balanced_target;
        }
    }else{
        target = underload_target;
    }
    
    swap_task(src_node, target);
    return OK;
}

enum operation_code migrate_from_underloaded(struct node* src_node){
    struct node* balanced_target = head;
    struct node* underload_target = NULL;
    uint8_t underloaded_count = 0;

    while (balanced_target != NULL && (balanced_target->state != BALANCED ||
            balanced_target->tasks_count+src_node->tasks_count >= TASKS_CAPACITY*overload_bound)){
        if (balanced_target != src_node && balanced_target->state == UNDERLOADED){
            underload_target = balanced_target;
            underloaded_count++;
        }
        balanced_target = balanced_target->next;
    }

    struct node* target;

    if (balanced_target == NULL){
        if (underloaded_count <= 2){ //otherwise they will collapse into one balanced and request to turn on again
            return FAILED; 
        }
        target = underload_target;
    }else{
        target = balanced_target;
    }
    while (src_node->tasks_count > 0)
        swap_task(src_node, target);

    src_node->state = OFF;
    off_count++;
    return OK;
}

void tick_timers(){
    struct node* iter = head;
    while (iter != NULL){
        if (iter->tasks_count > 0){
            iter->timer += tick_value;
            if (iter->timer >= pow(tasks_sizes[iter->tasks[iter->head_idx]], 3)){
                // printf("TASK %zu, \t%"PRIu64 " COMPLETED\n", next->tasks[next->head_idx], tasks_times[next->tasks[next->head_idx]]);
                iter->completion_time -= pow(tasks_sizes[iter->tasks[iter->head_idx]],3);
                if (--iter->tasks_count != 0)
                    iter->head_idx = iter->head_idx == TASKS_CAPACITY-1 ? 0 : iter->head_idx + 1;
                iter->timer = 0;
            }
        }
        printf("ID: %" PRIu32 ",\t COUNT: %zu,\t STATE: %d,\t COMP_TIME: %" PRIu64"\n",iter->id, iter->tasks_count, iter->state, iter->completion_time);
        if ((iter->tail_idx == iter->head_idx && iter->tasks_count !=0) ||
            (iter->tail_idx  > iter->head_idx && (iter->tail_idx - iter->head_idx + 1 != iter->tasks_count)) ||
            (iter->tail_idx  < iter->head_idx && (TASKS_CAPACITY + 1 - iter->head_idx + iter->tail_idx != iter->tasks_count))){
                printf("SIZE: %zu, HEAD: %zu, TAIL %zu\n" ,iter->tasks_count, iter->head_idx, iter->tail_idx);
            }

        iter = iter->next;
    }
}

void check_overloaded(){
    struct node* iterator = head;
    while (iterator != NULL){
        if (iterator->state == OVERLOADED || iterator->state == FULL){
            overloaded_exist = 1;
            migrate_from_overloaded(iterator);
            return;
        }
        iterator = iterator->next;
    }
    overloaded_exist = 0;
}

void check_underloaded(){
    struct node* iterator = head;
    struct node* first_underloaded = NULL;
    uint8_t underload_count = 0;
    

    while (iterator != NULL){
        if (iterator->state == UNDERLOADED){
            if (++underload_count == 1)
                first_underloaded = iterator;
            if (underload_count >= 2){
                migrate_from_underloaded(first_underloaded);
                return;
            }
        }
        iterator = iterator->next;
    }
}

void check_turn_on_condition(){
    struct node* iterator = head;
    uint8_t on_count = 0;
    struct node* turn_on_candidate = NULL;

    while (iterator != NULL){
        if (iterator->state == OFF){
            turn_on_candidate = iterator;
        }else if (iterator->state >= BALANCED){
            on_count++;
        }
        iterator = iterator->next;
    }

    if (nodes_count - off_count - on_count == 0){ // no reserved-underloaded
        turn_on_candidate->state = UNDERLOADED;
        off_count--;
    }
}

int main(){
    struct node* new_node = create_node(0, 1);
    head = new_node;
    size_t lost_tasks = 0;

    for (size_t i = 1; i < nodes_count; i++)
    {
        new_node->next = create_node(i, 1);
        new_node->next->prev = new_node;
        new_node = new_node->next;
    }

    for (size_t i = 0; i < 1000; i++)
    {
        // if (i % 55 == 0)
        //     Sleep(1);
        uint8_t size = rand()%100+2;
        struct task new_task = {i, size, NULL, NULL, 1};
        tasks_sizes[i] = size;

        if (i % 5 == 0){
            check_overloaded();
            if (!overloaded_exist)
                check_underloaded(); 
        }
        

        struct node* target = head;
        while (target->state == OFF){
            target = target->next;
        }

        enum operation_code code = assign_new_task(target, new_task);
        if (code == TASK_LOST)
            lost_tasks++;

        printf("ITER %zu\n", i);
        tick_timers();

        struct node* iterator = head;
        while (iterator != NULL){
            calc_new_position(iterator);
            iterator = iterator->next;
        }

        if (off_count > 0){
            check_turn_on_condition();
        }

        printf("\n");
    }

    printf("TASKS LOST: %zu\n", lost_tasks);
    printf("MIGRATIONS: %zu\n", migrations);
    
    
    return 0;
}