// RemoteBASH
// Thread Pool Source

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define INIT_TASKS_PER_THREAD 4

// Thread pool struct with variables for queue, mutexes, and condition variables
typedef struct tpool {
    int *queue;
    int queue_first;
    int queue_last;
    int queue_max;
    int task_count;
    int num_worker_threads;
    pthread_mutex_t queue_mtx;
    pthread_mutex_t queue_empty_mtx;
    pthread_cond_t queue_empty_cv;
} tpool_t;

// Declare tpool struct for the thread pool
tpool_t tpool;

// Worker function to be passed to thread pool threads
static void *thread_worker(void *process_task_func)
{
    while (1) {
        // Get process_task function pointer from passed void pointer
        void (*process_task)(int) = (void (*)(int))process_task_func;
        
        // Lock queue_empty mutex or wait for it to be unlocked
        pthread_mutex_lock(&tpool.queue_empty_mtx);
        
        while (tpool.task_count == 0) {
            pthread_cond_wait(&tpool.queue_empty_cv, &tpool.queue_empty_mtx); }
        
        tpool.task_count--;
        
        // Unlock queue_empty mutex
        pthread_mutex_unlock(&tpool.queue_empty_mtx);
        
        // Unlock main queue mutex
        pthread_mutex_lock(&tpool.queue_mtx);
        
        // Dequeue data and update queue variables
        int task = tpool.queue[tpool.queue_first];
        if (tpool.queue_first == tpool.queue_last) {
            tpool.queue_first = -1;
            tpool.queue_last = -1; }
        else {
            tpool.queue_first = (tpool.queue_first+1)%tpool.queue_max; }
        
        // Lock main queue mutex or wait for it to be unlocked
        pthread_mutex_unlock(&tpool.queue_mtx);
        
        // Process task using function passed in tpool_init
        process_task(task);
    }
    
    // Should not get here
    return NULL;
}

// Function to initialize tpool struct and create worker threads
int tpool_init(void (*process_task)(int))
{
    // Set num_worker_threads equal to the number of cores available
    tpool.num_worker_threads = sysconf(_SC_NPROCESSORS_ONLN);
    
    // Initialize tpool queue
    tpool.queue_first = -1;
    tpool.queue_last = -1;
    tpool.task_count = 0;
    tpool.queue_max = tpool.num_worker_threads * INIT_TASKS_PER_THREAD;
    tpool.queue = malloc(tpool.queue_max * sizeof(int));
    
    // Check for queue allocation failure
    if (tpool.queue == NULL) {
        perror("Tpool: Error allocating memory for queue");
        return 0; }
    
    // Initialize tpool mutexes
    if (pthread_mutex_init(&tpool.queue_mtx, NULL) ||
            pthread_mutex_init(&tpool.queue_empty_mtx, NULL)) {
        perror("Tpool: Error initializing tpool mutexes");
        return 0; }
    
    // Initialize tpool condition variables
    if (pthread_cond_init(&tpool.queue_empty_cv, NULL)) {
        perror("Tpool: Error initializing tpool condition variables");
        return 0; }
    
    // Loop to create a number of threads equal to the number of available cores
    for (int i=0; i < tpool.num_worker_threads; i++) {
        pthread_t worker_tid;
        
        if (pthread_create(&worker_tid, NULL, thread_worker, process_task)) {
            perror("Tpool: Error creating worker thread\n");
            return 0; }
    }
    
    // Thread pool initialized successfully
    return 1;
}

// Function to add task to thread pool's task queue
int tpool_add_task(int new_task)
{
    // Lock main queue mutex or wait for it to be unlocked
    pthread_mutex_lock(&tpool.queue_mtx);
    
    // If queue is full, expand it
    if (tpool.task_count == tpool.queue_max) {
        tpool.queue_max *= 2;
        tpool.queue = realloc(tpool.queue, tpool.queue_max * sizeof(int));
        if (tpool.queue_first > tpool.queue_last) {
            int i = tpool.task_count;
            for (int j=0; j<=tpool.queue_last; j++) {
                tpool.queue[i++] = tpool.queue[j];
            }
            tpool.queue_last += tpool.task_count; }
    }
    
    // Enqueue task and update queue variables
    tpool.queue_last = (tpool.queue_last+1)%tpool.queue_max;
    tpool.queue[tpool.queue_last] = new_task;
    if (tpool.queue_first == -1) {
        tpool.queue_first = tpool.queue_last; }
    
    // Unlock main queue mutex
    pthread_mutex_unlock(&tpool.queue_mtx);
    
    // Lock queue_empty mutex or wait for it to be unlocked
    pthread_mutex_lock(&tpool.queue_empty_mtx);
    
    tpool.task_count++;
    
    // Unlock queue_empty mutex
    pthread_mutex_unlock(&tpool.queue_empty_mtx);
    
    // Signal queue_empty condition variable
    // Allows task to be removed if queue was previously empty
    pthread_cond_signal(&tpool.queue_empty_cv);
    
    // Task added successfully
    return 1;
}


// EOF
