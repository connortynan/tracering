#define _POSIX_C_SOURCE 200809L // for nanosleep
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>

#define TRACER_ALLOW_OVERWRITE 0
#include <tracering/tracering.h>

#define NUM_THREADS 4
#define EVENTS_PER_THREAD 10

#define SLEEP_NS(ns)                  \
    do                                \
    {                                 \
        struct timespec ts = {0, ns}; \
        nanosleep(&ts, NULL);         \
    } while (0)

void *worker_thread(void *arg)
{
    (void)arg; // Unused argument
    TRACE(WorkerOuter, {
        for (int i = 0; i < EVENTS_PER_THREAD; ++i)
        {
            TRACE(WorkerInner, {
                SLEEP_NS(100000000); // Simulate 100ms of work
            });
        }
    });

    return NULL;
}

int main(void)
{
    if (tracer_emit_init() != 0)
    {
        fprintf(stderr, "Failed to initialize tracer emitter\n");
        return 1;
    }

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

    tracer_emit_shutdown();

    printf("Emit test completed\n");
    return 0;
}
