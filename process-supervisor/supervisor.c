#include <asm-generic/errno-base.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <stdbool.h>
#include <fcntl.h> // Bắt buộc phải có thư viện này để dùng hàm open()

#define TIMEOUT "--timeout"
#define RESTART "--restart" // FIX: Thêm -- cho khớp với lệnh gõ
#define KILL_GROUP "-g"
#define CAPTURE_OUT "-c"

#define TIMEOUT_DEFAULT 5
volatile sig_atomic_t is_running = 1;

void signal_handler(int sig) {
    (void)sig;
    is_running = 0;
}

void sigchld_handler(int sig) {
    (void)sig;
}

int main(int argc, char* argv[])
{
    long time_val = 0;
    int restart_val = 0;
    int cmd_idx = -1;
    bool kill_group = false;
    bool capture_out = false;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], RESTART) == 0 && i + 1 < argc) {
            restart_val = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], TIMEOUT) == 0 && i + 1 < argc) {
            time_val = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], KILL_GROUP) == 0) {
            kill_group = true;
        } else if (strcmp(argv[i], CAPTURE_OUT) == 0) {
            capture_out = true;
        } else {
            cmd_idx = i;
            break; 
        }
    }

    if (cmd_idx == -1) {
        fprintf(stderr, "Loi: Khong tim thay lenh de chay worker.\n");
        return -1;
    }

    sigset_t set, orig_set;
    sigemptyset(&set);
    sigemptyset(&orig_set);

    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGCHLD); 
    sigprocmask(SIG_SETMASK, &set, &orig_set);

    struct sigaction sigact;
    sigact.sa_handler = signal_handler;
    sigact.sa_flags = 0; 
    sigemptyset(&sigact.sa_mask);
    sigaction(SIGINT, &sigact, NULL);
    sigaction(SIGTERM, &sigact, NULL);

    struct sigaction sa_chld;
    sa_chld.sa_handler = sigchld_handler;
    sa_chld.sa_flags = 0;
    sigemptyset(&sa_chld.sa_mask);
    sigaction(SIGCHLD, &sa_chld, NULL);

    int restart_time = 0;

    int fd[2];
    

    while (restart_time <= restart_val && is_running) {
        if (capture_out && pipe(fd) == -1) {
            perror("Create pipe failed.\n");
            break;
        }

        printf("\n[Supervisor] Chay worker (Lan %d/%d)...\n", restart_time, restart_val);
        pid_t pid = fork();

        if (pid == 0) {
            sigprocmask(SIG_SETMASK, &orig_set, NULL); 

            if (kill_group) {
                setpgid(0, 0);
            }

            if (capture_out) {
                close(fd[0]);

                dup2(fd[1], STDOUT_FILENO);
                dup2(fd[1], STDERR_FILENO);

                close(fd[1]); 
            }

            execvp(argv[cmd_idx], &argv[cmd_idx]);
            
            perror("[Worker] EXEC failed");
            exit(EXIT_FAILURE);        

        } else if (pid > 0) {
            if (kill_group) {
                setpgid(pid, pid); 
            }
            
            if (capture_out) {
                close(fd[1]);

            }
            struct timespec time;

            if (time_val > 0) {
                time.tv_sec = time_val;
                time.tv_nsec = 0;
            } else {
                time.tv_nsec = 0;
                time.tv_sec = TIMEOUT_DEFAULT;
            }

            while(is_running) {
                fd_set my_fd;
                FD_ZERO(&my_fd);
                FD_SET(fd[0], &my_fd);


                int ret = pselect(capture_out? (fd[0] + 1) : 1, capture_out ? &my_fd : NULL, NULL, NULL, &time, &orig_set);

                if (ret == 0) {
                    printf("Time expire, kill child.\n");
                    kill(kill_group? -pid : pid, SIGKILL);
                } else if (ret > 0) {

                    if (capture_out && FD_ISSET(fd[0], &my_fd)) {
                        printf("data from child: \n");
                        char buf[128];    
                        ret = read(fd[0], buf, sizeof(buf));
                        
                        if (ret > 0) {
                            printf("[CHILD]: %s\n", buf);
                        } else if (ret == 0) {
                            perror("pipe end.\n");
                            break;
                        }
                    }


                } else {
                    if (errno == EINTR) {
                        if (!is_running) {
                            printf("SUPERVISOR: ctrl C.\n");
                            kill(kill_group ? -pid : pid, SIGKILL);
                        }
                        break;
                    }
                    
                }               
            }

            if (capture_out) {
                close(fd[0]);
            }

            int status;
            waitpid(pid, &status, 0);

            if (WIFEXITED(status)) {
                printf("[Supervisor] Child thoat an toan, ma loi: %d\n", WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                printf("[Supervisor] Child bi giet boi tin hieu: %d\n", WTERMSIG(status));
            }

            restart_time++;
            
            if (restart_time <= restart_val && is_running) {
                sleep(1);
            }

        } else {
            perror("Fork failed");
            break;
        }
    } 

    return 0;
}