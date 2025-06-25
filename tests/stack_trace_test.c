#define _POSIX_C_SOURCE 200809L // for nanosleep
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define TRACER_ALLOW_OVERWRITE 0
#include <tracering/tracering.h>
#include <tracering/receiver.h>
#include <tracering/adapter/stack_trace.h>

#define NUM_THREADS 2
#define EVENTS_PER_THREAD 3

#define SLEEP_NS(ns)                  \
    do                                \
    {                                 \
        struct timespec ts = {0, ns}; \
        nanosleep(&ts, NULL);         \
    } while (0)

static volatile sig_atomic_t keep_running = 1;

void handle_signal(int sig)
{
    (void)sig;
    keep_running = 0;
}

void trace_span_handler(const trace_span_t *span)
{
    double duration_ms = (double)(span->end_timestamp - span->start_timestamp) / 1000000.0;
    printf("SPAN [Thread %5u]: %-35s | Duration: %7.3f ms | Start: %lu | End: %lu\n",
           span->thread_id, span->full_path, duration_ms,
           span->start_timestamp, span->end_timestamp);
    fflush(stdout);
}

void *worker_thread(void *arg)
{
    int thread_num = *(int *)arg;

    TRACE(WorkerMain, {
        for (int i = 0; i < EVENTS_PER_THREAD; ++i)
        {
            TRACE(WorkerLoop, {
                TRACE(WorkerInner, {
                    SLEEP_NS(50000000); // 50ms of work
                });
                SLEEP_NS(25000000); // 25ms between inner tasks
            });
        }
    });

    printf("Worker thread %d completed\n", thread_num);
    return NULL;
}

void *receiver_thread(void *arg)
{
    (void)arg;

    while (keep_running)
    {
        tracer_receiver_poll();
        SLEEP_NS(1000000); // 1ms polling interval
    }

    return NULL;
}

int main(void)
{
    printf("Starting stack trace test...\n");

    // Register signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    tracer_receiver_init();

    // Initialize tracer components
    if (tracer_emit_init() != 0)
    {
        fprintf(stderr, "Failed to initialize tracer emitter\n");
        return 1;
    }

    if (stack_trace_adapter_init() != 0)
    {
        fprintf(stderr, "Failed to initialize stack trace adapter\n");
        return 1;
    }

    // Register handlers
    tracer_receiver_register_handler(stack_trace_event_handler);
    stack_trace_adapter_register_handler(trace_span_handler);

    // Start receiver thread
    pthread_t receiver_tid;
    if (pthread_create(&receiver_tid, NULL, receiver_thread, NULL) != 0)
    {
        perror("Failed to create receiver thread");
        return 1;
    }

    // Give receiver time to start
    SLEEP_NS(10000000); // 10ms

    TRACE(Main, {
        pthread_t threads[NUM_THREADS];
        int thread_ids[NUM_THREADS];

        TRACE(ThreadCreation, {
            for (int i = 0; i < NUM_THREADS; ++i)
            {
                thread_ids[i] = i;
                if (pthread_create(&threads[i], NULL, worker_thread, &thread_ids[i]) != 0)
                {
                    perror("pthread_create");
                    return 1;
                }
            }
        });

        TRACE(ThreadJoin, {
            for (int i = 0; i < NUM_THREADS; ++i)
            {
                pthread_join(threads[i], NULL);
            }
        });
    });

    // Let receiver process remaining events
    SLEEP_NS(100000000); // 100ms

    // Shutdown
    keep_running = 0;
    pthread_join(receiver_tid, NULL);

    stack_trace_adapter_shutdown();
    tracer_receiver_shutdown();
    tracer_emit_shutdown();

    printf("Stack trace test completed\n");
    return 0;
}