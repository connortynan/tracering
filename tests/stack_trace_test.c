#define _POSIX_C_SOURCE 200809L // for nanosleep
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

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

    if (tracer_adapter_stktrce_init() != 0)
    {
        fprintf(stderr, "Failed to initialize stack trace adapter\n");
        return 1;
    }

    // Register handlers
    tracer_adapter_stktrce_register_handler(trace_span_handler);

    // Start receiver thread
    pthread_t receiver_tid;
    if (pthread_create(&receiver_tid, NULL, receiver_thread, NULL) != 0)
    {
        perror("Failed to create receiver thread");
        return 1;
    }

    pthread_join(receiver_tid, NULL);

    tracer_adapter_stktrce_shutdown();
    tracer_receiver_shutdown();

    printf("Stack trace test completed\n");
    return 0;
}