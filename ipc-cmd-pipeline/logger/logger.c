

#include <asm-generic/errno-base.h>
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
#include <unistd.h>

#include <stdbool.h>
#include <pthread.h>


#define  PATH_LOG_FIFO "/tmp/ipc_log_pipeline.fifo"


int main(int argc, char* argv[])
{

    int recv_fd = open(PATH_LOG_FIFO, O_RDONLY | O_CLOEXEC);
    if (recv_fd < 0) {
        perror("Open fifo failed.\n");
    }    
    bool is_running = true;

    while (is_running) {
        char buf[30];
        int ret = read(recv_fd, buf, sizeof(buf)-1);

        if (ret < 0) {
            if (errno == EINTR)  {
                printf("read is interrupted.\n");
                is_running = false;
            } else {
                printf("another error\n");
                is_running = false;
            }
        } else if (ret == 0) {
            printf("write end.\n");
            is_running = false;
        } else {
            buf[ret] = '\0';
            printf("Log: %s\n", buf);
        }
    }

    close(recv_fd);

    return 0;
}