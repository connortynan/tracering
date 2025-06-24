#define _XOPEN_SOURCE 700
#include "tracering/receiver.h"
#include "tracering/receiver_ex.h"
#include <unistd.h>
#include <stddef.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>

#include "tracering_buffer.h"

#define TRACER_RECEIVER_THREADS 4
#define MAX_HANDLERS 16

typedef struct
{
    trace_event_handler_ex_t handler;
    void *context;
} handler_entry_t;

typedef struct
{
    const trace_event_t *event;
    trace_event_handler_ex_t handler;
    void *context;
} callback_task_t;

static trace_shared_buffer_t *shared_buffer = NULL;
static int shm_fd = -1;

static handler_entry_t handlers[MAX_HANDLERS];
static int handler_count = 0;
static pthread_mutex_t handler_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_t workers[TRACER_RECEIVER_THREADS];
static callback_task_t task_queue[MAX_HANDLERS];
static int task_count = 0;
static pthread_cond_t task_available = PTHREAD_COND_INITIALIZER;
static pthread_cond_t tasks_completed = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t task_mutex = PTHREAD_MUTEX_INITIALIZER;

static int pending_tasks = 0;
static bool running = false;

// Worker thread function for processing callbacks
static void *worker_thread(void *arg)
{
    (void)arg; // Unused
    while (running)
    {
        pthread_mutex_lock(&task_mutex);
        while (task_count == 0 && running)
        {
            pthread_cond_wait(&task_available, &task_mutex);
        }
        if (!running)
        {
            pthread_mutex_unlock(&task_mutex);
            break;
        }
        // Dequeue task (simple shift for fixed-size queue)
        callback_task_t task = task_queue[0];
        for (int i = 0; i < task_count - 1; i++)
        {
            task_queue[i] = task_queue[i + 1];
        }
        task_count--;
        pthread_mutex_unlock(&task_mutex);

        // Execute the callback
        task.handler(task.event, task.context);

        // Signal completion
        pthread_mutex_lock(&task_mutex);
        pending_tasks--;
        if (pending_tasks == 0)
        {
            pthread_cond_signal(&tasks_completed);
        }
        pthread_mutex_unlock(&task_mutex);
    }
    return NULL;
}

void tracer_receiver_init(void)
{
    // Create and map shared memory for the buffer
    shm_fd = shm_open(TRACE_SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        fprintf(stderr, "Failed to open shared memory\n");
        return;
    }
    if (ftruncate(shm_fd, sizeof(trace_shared_buffer_t)) == -1)
    {
        fprintf(stderr, "Failed to resize shared memory\n");
        close(shm_fd);
        return;
    }
    shared_buffer = mmap(NULL, sizeof(trace_shared_buffer_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_buffer == MAP_FAILED)
    {
        fprintf(stderr, "Failed to map shared memory\n");
        close(shm_fd);
        return;
    }

    // Initialize the shared buffer
    atomic_store_explicit(&shared_buffer->read_index, 0, memory_order_release);
    atomic_store_explicit(&shared_buffer->write_index, 0, memory_order_release);

    for (size_t i = 0; i < sizeof(shared_buffer->valid); i++)
    {
        atomic_store_explicit(&shared_buffer->valid[i], 0, memory_order_release);
    }

    // Set running flag and create worker threads
    running = true;
    for (int i = 0; i < TRACER_RECEIVER_THREADS; i++)
    {
        if (pthread_create(&workers[i], NULL, worker_thread, NULL) != 0)
        {
            fprintf(stderr, "Failed to create worker thread %d\n", i);
            running = false;
            // clean up partially created threads (todo)
            for (int j = 0; j < i; j++)
            {
                pthread_join(workers[j], NULL);
            }
            return;
        }
    }
    handler_count = 0; // Ensure reset
}

void tracer_receiver_shutdown(void)
{
    // Signal shutdown and wake workers
    pthread_mutex_lock(&task_mutex);
    running = false;
    pthread_cond_broadcast(&task_available);
    pthread_mutex_unlock(&task_mutex);

    // Join worker threads
    for (int i = 0; i < TRACER_RECEIVER_THREADS; i++)
    {
        pthread_join(workers[i], NULL);
    }

    // Clean up synchronization primitives
    pthread_mutex_destroy(&handler_mutex);
    pthread_mutex_destroy(&task_mutex);
    pthread_cond_destroy(&task_available);
    pthread_cond_destroy(&tasks_completed);

    // Unmap and unlink shared memory
    if (shared_buffer != NULL)
    {
        munmap(shared_buffer, sizeof(trace_shared_buffer_t));
        shared_buffer = NULL;
    }
    if (shm_fd != -1)
    {
        close(shm_fd);
        shm_unlink(TRACE_SHM_NAME);
        shm_fd = -1;
    }
}

// This is not intended to be ran in multiple threads at once
// It is designed to be ran on the main thread or a dedicated receiver thread
void tracer_receiver_poll(void)
{
    if (shared_buffer == NULL || !running)
        return;

    uint32_t read_idx = atomic_load_explicit(&shared_buffer->read_index, memory_order_acquire);
    uint32_t write_idx = atomic_load_explicit(&shared_buffer->write_index, memory_order_acquire);

    while (read_idx != write_idx)
    {
        uint32_t buffer_read_idx = read_idx & (TRACE_BUFFER_SIZE - 1);
        if (GET_EVENT_VALID(shared_buffer, buffer_read_idx))
        {
            trace_event_t local_event = shared_buffer->events[buffer_read_idx];
            CLEAR_EVENT_VALID(shared_buffer, buffer_read_idx);

            handler_entry_t local_handlers[MAX_HANDLERS];
            int local_count = 0;
            pthread_mutex_lock(&handler_mutex);
            for (int i = 0; i < handler_count; i++)
            {
                local_handlers[i] = handlers[i];
            }
            local_count = handler_count;
            pthread_mutex_unlock(&handler_mutex);

            // Dispatch tasks for parallel execution
            pthread_mutex_lock(&task_mutex);
            for (int i = 0; i < local_count && task_count < MAX_HANDLERS; i++)
            {
                task_queue[task_count].event = &local_event;
                task_queue[task_count].handler = local_handlers[i].handler;
                task_queue[task_count].context = local_handlers[i].context;
                task_count++;
                pending_tasks++;
            }
            pthread_cond_broadcast(&task_available); // Wake workers
            while (pending_tasks > 0)
            {
                pthread_cond_wait(&tasks_completed, &task_mutex);
            }
            pthread_mutex_unlock(&task_mutex);
        }

        atomic_store_explicit(&shared_buffer->read_index, read_idx++, memory_order_release);
        write_idx = atomic_load_explicit(&shared_buffer->write_index, memory_order_acquire);
    }
}

void tracer_receiver_register_handler_ex(trace_event_handler_ex_t handler, void *context)
{
    if (handler == NULL)
        return;

    pthread_mutex_lock(&handler_mutex);

    if (handler_count >= MAX_HANDLERS)
    {
        pthread_mutex_unlock(&handler_mutex);
        return; // Handler limit reached
    }

    // Check for duplicates
    for (int i = 0; i < handler_count; i++)
    {
        if (handlers[i].handler == handler && handlers[i].context == context)
        {
            pthread_mutex_unlock(&handler_mutex);
            return; // Already registered
        }
    }
    handlers[handler_count].handler = handler;
    handlers[handler_count].context = context;
    handler_count++;

    pthread_mutex_unlock(&handler_mutex);
}

void tracer_receiver_unregister_handler_ex(trace_event_handler_ex_t handler, void *context)
{
    if (handler == NULL)
        return;
    pthread_mutex_lock(&handler_mutex);
    for (int i = 0; i < handler_count; i++)
    {
        if (handlers[i].handler == handler && handlers[i].context == context)
        {
            // Remove by shifting
            for (int j = i; j < handler_count - 1; j++)
            {
                handlers[j] = handlers[j + 1];
            }
            handler_count--;
            break;
        }
    }
    pthread_mutex_unlock(&handler_mutex);
}

// Adapter trampoline for legacy handlers (no context)
static void handler_adapter(const trace_event_t *event, void *context)
{
    trace_event_handler_t fn = (trace_event_handler_t)context;
    fn(event);
}

void tracer_receiver_register_handler(trace_event_handler_t handler)
{
    tracer_receiver_register_handler_ex(handler_adapter, (void *)handler);
}

void tracer_receiver_unregister_handler(trace_event_handler_t handler)
{
    tracer_receiver_unregister_handler_ex(handler_adapter, (void *)handler);
}
