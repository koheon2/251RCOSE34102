#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <limits.h>

#define MAX_TIME_UNIT 5000
#define MAX_IO_REPEAT 3
#define MAX_QUEUE_SIZE 100
#define TIME_QUANTUM 3
#define NUM_QUEUE_LEVELS 3
#define MAX_ALG 20

enum {NON_PREEMPTIVE, PREEMPTIVE};
enum {FCFS, SJF, PRIORITY, RR, AGING_PRIORITY, HRRN, LOTTERY, STRIDE, MLFQ};

int max_process_num;

typedef struct {
    int IOstart;  
    int IOduration;  
} io_info;

typedef struct {
    int pid;                
    int priority;         
    int arrivalTime;      
    int CPUburst;      
    int CPUremainingTime;  
    io_info IOburst[MAX_IO_REPEAT];    
    int IOCount; 
    int nextIOstart;
    int IOremainingTime;  
    int waitingTime;
    int turnaroundTime;
    int tickets; 
    int stride;
    int stridePass;
    int currentQueueLevel;
    int queueWaitTime;
} process;
typedef process* processPointer;

processPointer job_queue[MAX_QUEUE_SIZE];
processPointer job_queue_clone[MAX_QUEUE_SIZE];
processPointer ready_queue[MAX_QUEUE_SIZE];
processPointer waiting_queue[MAX_QUEUE_SIZE];
processPointer terminated_queue[MAX_QUEUE_SIZE];
processPointer running_process;
int job_size = 0, job_clone_size = 0, ready_size = 0, waiting_size = 0, terminated_size = 0;

processPointer mlf_queue[NUM_QUEUE_LEVELS][MAX_QUEUE_SIZE];
int mlf_size[NUM_QUEUE_LEVELS];
int mlf_time_quantums[NUM_QUEUE_LEVELS];

typedef struct {
    int time_slice;
    int pid;   
} GanttEvent;
GanttEvent gantt_chart[MAX_TIME_UNIT];
int gantt_count = 0;

typedef struct {
    int alg;                
    int Ispreemptive;         
    int startTime;          
    int endTime;            
    double avg_waitingTime;    
    double avg_turnaroundTime; 
    double CPU_utilization; 
} evaluation;
evaluation evals[MAX_ALG];
int evals_count = 0;




void init_queue(processPointer* queue, int* queueSize) {
    *queueSize = 0;
    for (int i = 0; i < MAX_QUEUE_SIZE; i++) {
        queue[i] = NULL;
    }
} 

void del_queue(processPointer* queue) {
    for (int i = 0; i < MAX_QUEUE_SIZE; i++) {
        if (queue[i] == NULL) continue;
        free(queue[i]);
        queue[i] = NULL;
    }
} 

void enqueue(processPointer* queue, int* queueSize, processPointer proc) {
    if (*queueSize < MAX_QUEUE_SIZE) {
        queue[*queueSize] = proc;
        (*queueSize)++;
    }
}

processPointer dequeue(processPointer* queue, int* queueSize, processPointer proc) {
    processPointer temp = NULL;
    int index = -1;
    if (*queueSize > 0 && proc != NULL) {
        for (int i = 0; i < *queueSize; i++) {
            if (queue[i] == proc) {
                temp = queue[i];
                index = i;
                break;
            }
        }
        if (index == -1) {
            return NULL; 
        }
        for (int i = index; i < *queueSize - 1; i++) {
            queue[i] = queue[i + 1];
        }
        queue[*queueSize - 1] = NULL;
        (*queueSize)--;
    }
    return temp;
}

processPointer find(processPointer* queue, int* queueSize, char* criteria) {
    processPointer selected_process = NULL;
    if (*queueSize > 0) {
        if (strcmp(criteria, "min_remainCPUburst") == 0) {
            for (int i = 0; i < *queueSize; i++) {
                if (selected_process == NULL || queue[i]->CPUremainingTime < selected_process->CPUremainingTime) {
                    selected_process = queue[i];
                }
            }
        } else if (strcmp(criteria, "high_priority") == 0) {
            for (int i = 0; i < *queueSize; i++) {
                if (selected_process == NULL || queue[i]->priority < selected_process->priority) {
                    selected_process = queue[i];
                }
            }
        } else if (strcmp(criteria, "min_arrival") == 0) {
            for (int i = 0; i < *queueSize; i++) {
                if (selected_process == NULL || queue[i]->arrivalTime < selected_process->arrivalTime) {
                    selected_process = queue[i];
                }
            }
        } else if (strcmp(criteria, "max_response_ratio") == 0) {
            double current_ratio = 0.0;
            double selected_ratio = 0.0;
            for (int i = 0; i < *queueSize; i++) {
                current_ratio = (queue[i]->waitingTime + queue[i]->CPUburst) / (double)queue[i]->CPUburst;
                
                if (selected_process == NULL || current_ratio > selected_ratio) {
                    selected_process = queue[i];
                    selected_ratio = current_ratio;
                }
            }
        } else if (strcmp(criteria, "lottery_winner") == 0) {
            int total_tickets = 0;
            for (int i = 0; i < *queueSize; i++) {
                total_tickets += queue[i]->tickets;
            }
            int winning_number = rand() % total_tickets;
            int current_sum = 0;
            for (int i = 0; i < *queueSize; i++) {
                current_sum += queue[i]->tickets;
                if (winning_number < current_sum) {
                    selected_process = queue[i];
                    break;
                }
            }
        } else if (strcmp(criteria, "min_stride_pass") == 0) {
            for (int i = 0; i < *queueSize; i++) {
                if (selected_process == NULL || queue[i]->stridePass < selected_process->stridePass) {
                    selected_process = queue[i];
                }
            }
        }
            
    }
    return selected_process;
}

void clone_queue(processPointer* src, processPointer* dst, int* srcSize, int* dstSize) {
    del_queue(dst); 
    for (int i = 0; i < *srcSize; i++) {
        dst[i] = (processPointer)malloc(sizeof(process));
        memcpy(dst[i], src[i], sizeof(process));
        (*dstSize)++;
    }
}

void generate_IOburst(io_info* IOburst, int CPUburst) {
    int temp = CPUburst;
    for (int i = 0; i < MAX_IO_REPEAT; i++) {
        int coinFlip = rand() % 2; 
        if (coinFlip == 0 || temp <= 1) {
            IOburst[i].IOstart = -1; 
            IOburst[i].IOduration = 0;
        } else {
            int start_time = rand() % 10 + 1;
            IOburst[i].IOstart = (start_time < temp) ? start_time : ((rand() % (temp - 1)) + 1); // 1~10 or 1~temp-1
            IOburst[i].IOduration = rand() % 5 + 1; // 1~5
            temp -= IOburst[i].IOstart;
        }
    }
}

void create_process() {
    for (int i = 0; i < max_process_num; i++) {
        processPointer new_process = (processPointer)malloc(sizeof(process));
        new_process->pid = max_process_num - i; 
        new_process->priority = rand() % 10 + 1; // 1~10
        new_process->arrivalTime = rand() % max_process_num; // 0~max_process_num-1
        new_process->CPUburst = rand() % 25 + 1; // 1~25
        new_process->CPUremainingTime = new_process->CPUburst;
        generate_IOburst(new_process->IOburst, new_process->CPUburst);
        new_process->IOCount = 0;
        new_process->nextIOstart = new_process->IOburst[0].IOstart;
        new_process->IOremainingTime = new_process->IOburst[0].IOduration;
        new_process->waitingTime = 0;
        new_process->turnaroundTime = 0;
        new_process->tickets = 11 - new_process->priority; 
        new_process->stride = 100 / new_process->tickets;
        new_process->stridePass = 0;
        new_process->currentQueueLevel = 0;
        new_process->queueWaitTime = 0;
        enqueue(job_queue, &job_size, new_process);
    }
}





void result() {
    printf("\n\n======Process Statistics (Terminated Order)======\n");
    printf("------------------------------------------------------------------------------------------------\n");
    printf(" PID | Arrival | CPUBurst | Priority | tickets | stride | WaitingTime | TurnaroundTime | IOburst \n");
    printf("------------------------------------------------------------------------------------------------\n");
    for (int i = 0; i < terminated_size; i++) {
        processPointer p = terminated_queue[i];
        printf(" %2d |   %3d    |   %3d    |   %3d    |   %3d   |   %3d   |    %4d     |     %4d      |",
        p->pid, p->arrivalTime, p->CPUburst, p->priority, p->tickets, p->stride, p->waitingTime, p->turnaroundTime);
        for (int j = 0; j < MAX_IO_REPEAT; j++) {
            if (p->IOburst[j].IOstart == -1) {
                break;
            } 
            printf("%d: [S:%d, D:%d] ", j + 1, p->IOburst[j].IOstart, p->IOburst[j].IOduration);
        }
        printf("\n");
    }
    printf("------------------------------------------------------------------------------------------------\n");
}

void gantt_record(int time, int pid) {
    if (gantt_count != 0 && gantt_chart[gantt_count - 1].pid == pid) {
        return;
    }
    gantt_chart[gantt_count].time_slice = time;
    gantt_chart[gantt_count].pid = pid;
    gantt_count++;
}

void draw_gantt(int terminateTime) {
    printf("\n===============Gantt Chart=================\n");
    printf("(%d)| P%d", gantt_chart[0].time_slice, gantt_chart[0].pid);
    for (int i = 1; i < gantt_count; i++) {
        if (gantt_chart[i].pid == 0) {
            printf(" (%d)| Idle", gantt_chart[i].time_slice);
        } else {
            printf(" (%d)| P%d", gantt_chart[i].time_slice, gantt_chart[i].pid);
        }
    }
    printf(" (%d)|\n\n\n\n", terminateTime);
}

void create_eval(int alg, int preemptive, int start, int end, int idleTime) {
    evals[evals_count].alg = alg;
    evals[evals_count].Ispreemptive = preemptive;
    evals[evals_count].startTime = start;
    evals[evals_count].endTime = end;
    evals[evals_count].avg_waitingTime = 0;
    evals[evals_count].avg_turnaroundTime = 0;
    for (int i = 0; i < terminated_size; i++) {
        evals[evals_count].avg_waitingTime += terminated_queue[i]->waitingTime;
        evals[evals_count].avg_turnaroundTime += terminated_queue[i]->turnaroundTime;
    }
    evals[evals_count].avg_waitingTime /= terminated_size;
    evals[evals_count].avg_turnaroundTime /= terminated_size;
    evals[evals_count].CPU_utilization = (double)(end - start - idleTime) / (end - start);
    evals_count++;
}

char* get_algorithm_name(int alg, int preemptive) {
    switch(alg) {
        case FCFS: return "FCFS";
        case SJF: return preemptive ? "Preemptive SJF" : "Non-preemptive SJF";
        case PRIORITY: return preemptive ? "Preemptive Priority" : "Non-preemptive Priority";
        case RR: return "Round Robin";
        case AGING_PRIORITY: return preemptive ? "preemptive Aging Priority" : "Non-Preemptive Aging Priority";
        case HRRN: return "HRRN";
        case LOTTERY: return preemptive ? "Preemptive Lottery" : "Non-preemptive Lottery";
        case STRIDE: return preemptive ? "Preemptive Stride" : "Non-preemptive Stride";
        case MLFQ: return "Multi-Level Feedback Queue";
        default: return "<Unknown Algorithm>";
    }
}

void evaluate(){
    printf("\n========= All Algorithm Comparison =========\n");
    printf("-----------------------------------------------------------------------------------------------\n");
    printf("|            Algorithm             | Avg Waiting Time | Avg Turnaround Time | CPU Utilization  |\n");
    printf("-----------------------------------------------------------------------------------------------\n");

    for (int i = 0; i < evals_count; i++) {
        printf("| %-31s  | %16.2f | %19.2f | %15.2f%% |\n",
                get_algorithm_name(evals[i].alg, evals[i].Ispreemptive),
                evals[i].avg_waitingTime,
                evals[i].avg_turnaroundTime,
                evals[i].CPU_utilization * 100);        
    }
    printf("----------------------------------------------------------------------------------------------\n");
}

void scheduler(int alg, int preemptive, int *remaining_TQ) {
    processPointer temp = NULL;
    if (alg == AGING_PRIORITY) {
        alg = PRIORITY; 
    }
    switch(alg) {
        case FCFS:
            if (running_process == NULL) {
                running_process = dequeue(ready_queue, &ready_size, ready_queue[0]);
                if (running_process != NULL) {
                    printf("|P%d (ready -> running)|", running_process->pid);
                }
            }
            break;

        case SJF:
            temp = find(ready_queue, &ready_size, "min_remainCPUburst");
            if (temp == NULL) break;
            
            if (running_process != NULL) {
                if (preemptive && running_process->CPUremainingTime > temp->CPUremainingTime) {
                    enqueue(ready_queue, &ready_size, running_process);
                    printf("|P%d (preempted by P%d)|", running_process->pid, temp->pid);
                    running_process = temp;
                    dequeue(ready_queue, &ready_size, temp);
                } 
            } else {
                running_process = temp;
                printf("|P%d (ready -> running)|",running_process->pid);
                dequeue(ready_queue, &ready_size, temp);
            }
            break;

        case RR: 
            if (running_process != NULL) {
                if (*remaining_TQ != 0) break;
                enqueue(ready_queue, &ready_size, running_process);
                printf("|P%d (time quantum expired)|", running_process->pid);
            }
            running_process = dequeue(ready_queue, &ready_size, ready_queue[0]);
            *remaining_TQ = TIME_QUANTUM;
            if (running_process != NULL) {
                printf("|P%d (ready -> running)|",running_process->pid);
            }
            break;

        case PRIORITY:
            temp = find(ready_queue, &ready_size, "high_priority");
            if (temp == NULL) break;

            if (running_process != NULL) {
                if (preemptive && running_process->priority > temp->priority) {
                    printf("|P%d (preempted by P%d)|", running_process->pid, temp->pid);
                    enqueue(ready_queue, &ready_size, running_process);
                    running_process = temp;
                    dequeue(ready_queue, &ready_size, temp);
                } 
            } else {
                running_process = temp;
                printf("|P%d (ready -> running)|",running_process->pid);
                dequeue(ready_queue, &ready_size, temp);
            }
            break;
        
        case HRRN:
            temp = find(ready_queue, &ready_size, "max_response_ratio");
            if (temp == NULL) break;
            
            if (running_process == NULL) {
                running_process = temp;
                printf("|P%d (ready -> running)|",running_process->pid);
                dequeue(ready_queue, &ready_size, temp);
            }
            break;

        case LOTTERY:
            temp = find(ready_queue, &ready_size, "lottery_winner");
            if (temp == NULL) break;
            
            if (running_process != NULL) {
                if (!preemptive || *remaining_TQ != 0) break;
                printf("|P%d (time quantum expired)|", running_process->pid);
                enqueue(ready_queue, &ready_size, running_process);
            }
            running_process = temp;
            dequeue(ready_queue, &ready_size, temp);
            *remaining_TQ = TIME_QUANTUM;
            if (running_process != NULL) {
                printf("|P%d (ready -> running)|",running_process->pid);
            }
            break;
        case STRIDE:
            temp = find(ready_queue, &ready_size, "min_stride_pass");
            if (temp == NULL) break;

            if (running_process != NULL) {
                if (!preemptive || *remaining_TQ != 0) break;
                printf("|P%d (time quantum expired)|", running_process->pid);
                enqueue(ready_queue, &ready_size, running_process);
            }
            running_process = temp;
            dequeue(ready_queue, &ready_size, temp);
            *remaining_TQ = TIME_QUANTUM;
            if (running_process != NULL) {
                printf("|P%d (ready -> running)|", running_process->pid);
                running_process->stridePass += running_process->stride;
            }
            break;

        case MLFQ:
            if (running_process != NULL) {
                if (*remaining_TQ != 0) {
                    int higher_process = 0;
                    for (int i = 0; i < running_process->currentQueueLevel; i++) {
                        if (mlf_size[i] > 0) {
                            higher_process = 1;
                            temp = mlf_queue[i][0];
                            break;
                        }
                    }
                    if (higher_process) {
                        printf("|P%d (preempted by P%d)|", running_process->pid, temp->pid);
                        running_process->queueWaitTime = 0;
                        enqueue(mlf_queue[running_process->currentQueueLevel], &mlf_size[running_process->currentQueueLevel], running_process);
                        running_process = NULL;
                    }
                } else {
                    if (running_process->currentQueueLevel < NUM_QUEUE_LEVELS - 1) {
                        running_process->currentQueueLevel++;
                        running_process->queueWaitTime = 0;
                    }
                    printf("|P%d (time quantum expired, demoted to queue[%d])|", running_process->pid, running_process->currentQueueLevel);
                    enqueue(mlf_queue[running_process->currentQueueLevel], &mlf_size[running_process->currentQueueLevel], running_process);
                    running_process = NULL;
                }
            }
            if (running_process == NULL){
                for (int level = 0; level < NUM_QUEUE_LEVELS; level++) {
                    if (mlf_size[level] > 0) {
                        running_process = dequeue(mlf_queue[level], &mlf_size[level], mlf_queue[level][0]);
                        running_process->currentQueueLevel = level;
                        printf("|P%d (queue[%d] -> running)|", running_process->pid, level);
                        *remaining_TQ = mlf_time_quantums[level];
                        break;
                    }
                }
            }
            break;

        default:
            printf("<Unknown Algorithm>");
    }
}

void simulate(int alg, int preemptive) {
	switch(alg) {
        case FCFS:
            printf("<FCFS Algorithm>\n");
            break;
        case SJF:
        	printf("<%s SJF Algorithm>\n", preemptive ? "Preemptive" : "Non-preemptive");
        	break;
        case RR:
        	printf("<Round Robin Algorithm (time quantum: %d)>\n",TIME_QUANTUM);
        	break;
        case PRIORITY:
        	printf("<%s Priority Algorithm>\n", preemptive ? "Preemptive" : "Non-preemptive");
        	break;
        case AGING_PRIORITY:
        	printf("<%s Aging Priority Algorithm>\n", preemptive ? "Preemptive" : "Non-preemptive");
        	break;
        case HRRN:
        	printf("<HRRN Algorithm>\n");
        	break;
        case LOTTERY:
            printf("<%s Lottery Algorithm>\n", preemptive ? "Preemptive" : "Non-preemptive");
            break;
        case STRIDE:
            printf("<%s Stride Algorithm>\n", preemptive ? "Preemptive" : "Non-preemptive");
            break;
        default:
            printf("<Unknown Algorithm>\n"); 
            break;
    }

    init_queue(job_queue, &job_size);
    init_queue(ready_queue, &ready_size);
    init_queue(waiting_queue, &waiting_size);
    init_queue(terminated_queue, &terminated_size);
    running_process = NULL;
    clone_queue(job_queue_clone, job_queue, &job_clone_size, &job_size);
    gantt_count = 0;
    int current_time = 0;
    int idle_time = 0;
    int start = -1;
    int remaining_TQ = TIME_QUANTUM;

    while (current_time < MAX_TIME_UNIT) {

        printf("\n[TIME: %d] ", current_time);
        if (running_process != NULL) {
            printf("[Running: P%d]",running_process->pid);
        } else {
            printf("[Running: Idle]");
        }
        for (int i = 0; i < ready_size; i++) {
            ready_queue[i]->waitingTime++;
            ready_queue[i]->queueWaitTime++;
            if (alg == AGING_PRIORITY && ready_queue[i]->queueWaitTime == max_process_num * 5) {
                ready_queue[i]->queueWaitTime = 0;
                ready_queue[i]->priority = (ready_queue[i]->priority > 1 ? ready_queue[i]->priority / 2 : 1);
                printf("|P%d (priority elevated to %d)|", ready_queue[i]->pid, ready_queue[i]->priority);
            } 
        }

        for (int i = job_size - 1; i >= 0; i--){
            if (job_queue[i] == NULL) continue;
            if (job_queue[i]->arrivalTime == current_time) {
                printf("|P%d(arrival)|", job_queue[i]->pid);
                enqueue(ready_queue, &ready_size, job_queue[i]);
                dequeue(job_queue, &job_size, job_queue[i]);
                if (start == -1) {
                    start = current_time;
                }
            }
        }

        for (int i = waiting_size - 1; i >= 0; i--) {
            if (waiting_queue[i] == NULL) continue;
            waiting_queue[i]->IOremainingTime--;
            if (waiting_queue[i]->IOremainingTime == 0) {
                if (waiting_queue[i]->IOCount < MAX_IO_REPEAT) {
                    waiting_queue[i]->nextIOstart = waiting_queue[i]->IOburst[waiting_queue[i]->IOCount].IOstart;
                    waiting_queue[i]->IOremainingTime = waiting_queue[i]->IOburst[waiting_queue[i]->IOCount].IOduration;
                }
                printf("|P%d (waiting -> ready)|", waiting_queue[i]->pid);
                waiting_queue[i]->queueWaitTime = 0;
                enqueue(ready_queue, &ready_size, waiting_queue[i]);
                dequeue(waiting_queue, &waiting_size, waiting_queue[i]);
            }
        }

        if (running_process != NULL) {
            remaining_TQ--;
            running_process->CPUremainingTime--;
            running_process->nextIOstart--;
            if (running_process->CPUremainingTime == 0) {
                enqueue(terminated_queue, &terminated_size, running_process);
                printf("|P%d (terminated)|", running_process->pid);
                running_process->turnaroundTime = current_time - running_process->arrivalTime;
                running_process->waitingTime = running_process->turnaroundTime - running_process->CPUburst;
                for (int i = 0; i < running_process->IOCount; i++) {
                    running_process->waitingTime -= running_process->IOburst[i].IOduration;
                }
                running_process = NULL;
                if (terminated_size == job_clone_size) {
                    printf("All processes terminated\n");
                    break;
                }
            } else if (running_process->nextIOstart == 0) {
                printf("|P%d (running -> waiting)|", running_process->pid);
                enqueue(waiting_queue, &waiting_size, running_process);
                running_process->IOCount++;
                running_process = NULL;
            }
        }

        scheduler(alg, preemptive, &remaining_TQ);
        if (start != -1){
            gantt_record(current_time, running_process ? running_process->pid : 0);
            if (running_process == NULL) idle_time++;
        }

        current_time++;
    }
    create_eval(alg, preemptive, start, current_time, idle_time);
    result();
    draw_gantt(current_time);
}


void simulate_mlfq(int alg, int preemptive) {
    printf("<Multi-Level Feedback Queue Algorithm>\n");

    for (int level = 0; level < NUM_QUEUE_LEVELS - 1; level++) {
        mlf_time_quantums[level] = (level + 1) * TIME_QUANTUM; 
    }
    mlf_time_quantums[NUM_QUEUE_LEVELS - 1] = INT_MAX; 
    
    for (int level = 0; level < NUM_QUEUE_LEVELS; level++) {
        init_queue(mlf_queue[level], &mlf_size[level]);
    }
    init_queue(ready_queue, &ready_size);
    init_queue(job_queue, &job_size);
    init_queue(waiting_queue, &waiting_size);
    init_queue(terminated_queue, &terminated_size);
    running_process = NULL;
    clone_queue(job_queue_clone, job_queue, &job_clone_size, &job_size);
    gantt_count = 0;
    int current_time = 0;
    int idle_time = 0;
    int start = -1;
    int remaining_TQ = TIME_QUANTUM;
    
    while (current_time < MAX_TIME_UNIT) {
        printf("\n[TIME: %d] ", current_time);
        if (running_process != NULL) {
            printf("[Running: P%d]", running_process->pid);
        } else {
            printf("[Running: Idle]");
        }
        for (int level = 0; level < NUM_QUEUE_LEVELS; level++) {
            for (int i = mlf_size[level] - 1; i >= 0; i--) {
                mlf_queue[level][i]->waitingTime++;
                mlf_queue[level][i]->queueWaitTime++;
                if (mlf_queue[level][i]->queueWaitTime == max_process_num * 5) {
                    mlf_queue[level][i]->queueWaitTime = 0;
                    if (mlf_queue[level][i]->currentQueueLevel > 0) {
                        mlf_queue[level][i]->currentQueueLevel--;
                        printf("|P%d (elevated to queue[%d])|", mlf_queue[level][i]->pid, mlf_queue[level][i]->currentQueueLevel);
                        enqueue(mlf_queue[mlf_queue[level][i]->currentQueueLevel], &mlf_size[mlf_queue[level][i]->currentQueueLevel], mlf_queue[level][i]);
                        dequeue(mlf_queue[level], &mlf_size[level], mlf_queue[level][i]);
                    }
                }
            }
        }
        
        for (int i = job_size - 1; i >= 0; i--){
            if (job_queue[i] == NULL) continue;
            if (job_queue[i]->arrivalTime == current_time) {
                printf("|P%d(arrival to queue[0])|", job_queue[i]->pid);
                enqueue(mlf_queue[0], &mlf_size[0], job_queue[i]);
                dequeue(job_queue, &job_size, job_queue[i]);
                if (start == -1) {
                    start = current_time;
                }
            }
        }
        
        for (int i = waiting_size - 1; i >= 0; i--) {
            if (waiting_queue[i] == NULL) continue;
            waiting_queue[i]->IOremainingTime--;
            if (waiting_queue[i]->IOremainingTime == 0) {
                if (waiting_queue[i]->IOCount < MAX_IO_REPEAT) {
                    waiting_queue[i]->nextIOstart = waiting_queue[i]->IOburst[waiting_queue[i]->IOCount].IOstart;
                    waiting_queue[i]->IOremainingTime = waiting_queue[i]->IOburst[waiting_queue[i]->IOCount].IOduration;
                }
                printf("|P%d (waiting -> ready_queue[%d])|", waiting_queue[i]->pid, waiting_queue[i]->currentQueueLevel);
                waiting_queue[i]->queueWaitTime = 0;
                enqueue(mlf_queue[waiting_queue[i]->currentQueueLevel], &mlf_size[waiting_queue[i]->currentQueueLevel], waiting_queue[i]);
                dequeue(waiting_queue, &waiting_size, waiting_queue[i]);
            }
        }

        if (running_process != NULL) {
            remaining_TQ--;
            running_process->CPUremainingTime--;
            running_process->nextIOstart--;
            
            if (running_process->CPUremainingTime == 0) {
                enqueue(terminated_queue, &terminated_size, running_process);
                printf("|P%d (terminated)|", running_process->pid);
                running_process->turnaroundTime = current_time - running_process->arrivalTime;
                running_process->waitingTime = running_process->turnaroundTime - running_process->CPUburst;
                for (int i = 0; i < running_process->IOCount; i++) {
                    running_process->waitingTime -= running_process->IOburst[i].IOduration;
                }
                running_process = NULL;
                if (terminated_size == job_clone_size) {
                    printf("All processes terminated");
                    break;
                }
            } else if (running_process->nextIOstart == 0) {
                printf("|P%d (running -> waiting)|", running_process->pid);
                enqueue(waiting_queue, &waiting_size, running_process);
                running_process->IOCount++;
                running_process = NULL;
            }
        } 
        
        scheduler(alg, preemptive, &remaining_TQ);
        if (start != -1) {
            gantt_record(current_time, running_process ? running_process->pid : 0);
            if (running_process == NULL) idle_time++;
        }

        current_time++;
    }
    create_eval(alg, preemptive, start, current_time, idle_time);
    result();
    draw_gantt(current_time);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <number_of_processes>\n", argv[0]);
        return 1;
    }
    max_process_num = atoi(argv[1]);
    if (max_process_num <= 0 || max_process_num > MAX_QUEUE_SIZE) {
        printf("error. it should be between 1 and %d.\n", MAX_QUEUE_SIZE);
        return 1;
    }
    srand(time(NULL));
    init_queue(job_queue, &job_size);
    init_queue(job_queue_clone, &job_clone_size);
    init_queue(ready_queue, &ready_size);
    init_queue(waiting_queue, &waiting_size);
    init_queue(terminated_queue, &terminated_size);
    for (int level = 0; level < NUM_QUEUE_LEVELS; level++) {
        init_queue(mlf_queue[level], &mlf_size[level]);
    }
    running_process = NULL;
    create_process();
    clone_queue(job_queue, job_queue_clone, &job_size, &job_clone_size);

    printf("select algorithm:\n");
    printf("1. FCFS\n");
    printf("2. SJF (Non-preemptive)\n");
    printf("3. SJF (Preemptive)\n");
    printf("4. Priority (Non-preemptive)\n");
    printf("5. Priority (Preemptive)\n");
    printf("6. Round Robin \n");
    printf("7. Aging Priority (Non-preemptive)\n");
    printf("8. Aging Priority (Preemptive)\n");
    printf("9. HRRN\n");
    printf("10. Lottery (Non-preemptive)\n");
    printf("11. Lottery (Preemptive)\n");
    printf("12. Stride (Non-preemptive)\n");
    printf("13. Stride (Preemptive)\n");
    printf("14. Multi-Level Feedback Queue (MLFQ)\n");
    printf("15. All Algorithms comparsion\n");
    printf("Enter your choice (1-15): ");

    int choice;
    scanf("%d", &choice);
    switch (choice) {
        case 1:
            simulate(FCFS, NON_PREEMPTIVE);
            break;
        case 2:
            simulate(SJF, NON_PREEMPTIVE);
            break;
        case 3:
            simulate(SJF, PREEMPTIVE);
            break;
        case 4:
            simulate(PRIORITY, NON_PREEMPTIVE);
            break;
        case 5:
            simulate(PRIORITY, PREEMPTIVE);
            break;
        case 6:
            simulate(RR, PREEMPTIVE);
            break;
        case 7:
            simulate(AGING_PRIORITY, NON_PREEMPTIVE);
            break;
        case 8:
            simulate(AGING_PRIORITY, PREEMPTIVE);
            break;
        case 9:
            simulate(HRRN, NON_PREEMPTIVE);
            break;
        case 10:
            srand(42);
            simulate(LOTTERY, NON_PREEMPTIVE);
            break;
        case 11:
            srand(42);
            simulate(LOTTERY, PREEMPTIVE);
            break;
        case 12:
            srand(42);
            simulate(STRIDE, NON_PREEMPTIVE);
            break;       
        case 13:
            srand(42);
            simulate(STRIDE, PREEMPTIVE);
            break;
        case 14:
            simulate_mlfq(MLFQ, PREEMPTIVE);
            break;
        case 15:
            printf("<All Algorithms Comparison>\n");
            simulate(FCFS, NON_PREEMPTIVE);
            simulate(SJF, NON_PREEMPTIVE);
            simulate(SJF, PREEMPTIVE);
            simulate(PRIORITY, NON_PREEMPTIVE);
            simulate(PRIORITY, PREEMPTIVE);
            simulate(RR, PREEMPTIVE);
            simulate(AGING_PRIORITY, NON_PREEMPTIVE);
            simulate(AGING_PRIORITY, PREEMPTIVE);
            simulate(HRRN, NON_PREEMPTIVE);
            srand(42);
            simulate(LOTTERY, NON_PREEMPTIVE);
            srand(42);
            simulate(LOTTERY, PREEMPTIVE);
            srand(42);
            simulate(STRIDE, NON_PREEMPTIVE);
            srand(42);
            simulate(STRIDE, PREEMPTIVE);
            simulate_mlfq(MLFQ, PREEMPTIVE);
            break;
        default:
            printf("Invalid choice.\n");
            return 1;
    }
    evaluate();

    del_queue(job_queue);
    del_queue(job_queue_clone);
    del_queue(ready_queue);
    del_queue(waiting_queue);
    del_queue(terminated_queue);
    for (int level = 0; level < NUM_QUEUE_LEVELS; level++) {
        del_queue(mlf_queue[level]);
    }
    running_process = NULL;

    printf("time quantum: %d\n", TIME_QUANTUM);
    printf("number of processes: %d\n", max_process_num);
    printf("MLFQ(time quantum): ");
    for (int i = 0; i < NUM_QUEUE_LEVELS - 1; i++) {
        printf("%d ", mlf_time_quantums[i]);
    }
    printf("FCFS\n");
    printf("MLFQ(num of queue levels): %d\n", NUM_QUEUE_LEVELS);
    printf("aging criteria: %d\n", max_process_num * 5);
    printf("simulation complete\n");
    return 0;
}