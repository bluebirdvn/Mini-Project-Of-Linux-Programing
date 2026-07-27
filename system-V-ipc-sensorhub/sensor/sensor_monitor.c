#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>

volatile sig_atomic_t keep_running = 1;

void handle_sigint(int sig)
{
    (void)sig;
    keep_running = 0;
}


int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);
    key_t shm_key, msq_key, sem_key;
    int shm_id, msq_id, sem_id;

    if (create_key_all(&shm_key, &msq_key, &sem_key) != 0) {
        return -1;
    }

    if (get_id_all(&msq_id, &shm_id, &sem_id, &shm_key, &msq_key, &sem_key, false) != 0) {
        return -1;
    }
    void *addr;
    if (attach_share_mem(shm_id, 0, &addr) != 0 || addr == NULL) {
        return -1;
    }
    struct shm_data *shared_mem = (struct shm_data *)addr;

    printf("[MONITOR] waiting data...\n\n");

    while (keep_running) {
        get_data(shared_mem, sem_id);

        sleep(1);
    }

    printf("\n[MONITOR] detach share mem\n");
    detach_share_mem(shared_mem);
    printf("[MONITOR] exit.\n");

    return 0;
}