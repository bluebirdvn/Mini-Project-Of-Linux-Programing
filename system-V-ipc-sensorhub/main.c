#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#define NUM_APPS 3

pid_t children[NUM_APPS];

void handle_sigint(int sig)
{
    printf("get %d (Ctrl+C). kill childrents\n", sig);
    
    for (int i = 0; i < NUM_APPS; i++) {
        if (children[i] > 0) {
            printf("kill child with : %d\n", children[i]);
            kill(children[i], SIGTERM);
        }
    }
}

int main(void)
{
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    const char *apps[NUM_APPS] = {
        "./hub_controller", 
        "./sensor_writer", 
        "./sensor_monitor"
    };

    printf("starting...\n");

    for (int i = 0; i < NUM_APPS; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork error");
            exit(EXIT_FAILURE);
        } 
        else if (pid == 0) {
            execl(apps[i], apps[i], (char *)NULL);
            
            perror("execute file not valied");
            exit(EXIT_FAILURE);
        } 
        else {
            children[i] = pid;
            printf("running %s with PID: %d\n", apps[i], pid);
            
            if (i == 0) {
                sleep(1);
            }
        }
    }

    printf("running. ctrl + C to exit.\n");

    int status;
    pid_t wpid;
    while ((wpid = wait(&status)) > 0) {
        if (WIFEXITED(status)) {
            printf("child with PID %d exits with exit code %d.\n", wpid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("child with PID %d  stop by signal %d.\n", wpid, WTERMSIG(status));
        }
    }

    printf("shutdown\n");
    return 0;
}