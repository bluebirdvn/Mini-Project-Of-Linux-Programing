
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <stdbool.h>
#include <pthread.h>

#define  PATH_CMD_FIFO "/tmp/ipc_cmd_pipeline.fifo"
#define  PATH_LOG_FIFO "/tmp/ipc_log_pipeline.fifo"

#define  PATH_CMD_ACK "/tmp/ipc_ack.fifo"


int create_fifo(void)
{
    int ret = mkfifo(PATH_CMD_FIFO, 0666);
    if (ret < 0) {
        if (errno == EEXIST) {
            printf("fifo exist.\n");
            return 0;
        } else {
            perror("error cmd ack create.\n");
            return -1;
        }
    }

    ret = mkfifo(PATH_LOG_FIFO, 0666);
    if (ret < 0) {
        if (errno == EEXIST) {
            printf("fifo exist.\n");
            return 0;
        } else {
            perror("error cmd ack create.\n");
            return -1;
        }
    }    
    
    
    ret = mkfifo(PATH_CMD_ACK, 0666);
    if (ret < 0) {
        if (errno == EEXIST) {
            printf("fifo exist.\n");
            return 0;
        } else {
            perror("error cmd ack create.\n");
            return -1;
        }
    }

    return 0;
}

void unlink_fifo(void)
{
    unlink(PATH_CMD_FIFO);
    unlink(PATH_LOG_FIFO);
    unlink(PATH_CMD_ACK);
}

int main(int argc, char* argv[])
{

    if (argc < 3) {
        printf("Not enough argument\n");
        return -1;
    }

    create_fifo();

    atexit(unlink_fifo);

    char *args_ctrl[] = {argv[1], NULL};
    char *args_log[]  = {argv[2], NULL};
    char *args_work[] = {argv[3], NULL};
    
    pid_t ctrl, log, work;

    ctrl = fork();
    if (ctrl == 0) {
        printf("This is child for ctrl.\n");
        execv(argv[1], args_ctrl);

        perror("Run ctrl process failed.\n");
        exit(EXIT_FAILURE);
    } else if (ctrl < 0) {
        perror("fork() for ctrl failed.\n");
    } 

    log = fork();
    if (log == 0) {
        printf("This is child for log.\n");
        execv(argv[2], args_log);

        perror("Run log process failed.\n");
        exit(EXIT_FAILURE);
    } else if (ctrl < 0) {
        perror("fork() for log failed.\n");
    } 

    work = fork();
    if (work == 0) {
        printf("This is child for work.\n");
        execv(argv[3], args_work);

        perror("Run work process failed.\n");
        exit(EXIT_FAILURE);
    } else if (ctrl < 0) {
        perror("fork() for work failed.\n");
    } 

    int status, finish;

    while ((finish = wait(&status)) > 0) {
        if (WIFEXITED(status)) {
            printf(
                "process %d exit with status %d.\n", finish, WEXITSTATUS(status)
            );

        } else if (WIFSIGNALED(status)) {
            printf("process %d killed by signal %d.\n", finish, WTERMSIG(status));
        }
    }

    return 0;
}