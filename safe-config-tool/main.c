
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/fcntl.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define  MAX_PATH_NAME 256
#define  BUFFER_SIZE 256

int restore_file(const char* back_up_path, const char* path) {
    if (rename(back_up_path, path) == -1) {
        perror("Khôi phục thất bại!");
        return -1;
    }
    return 0;
}

int back_up_file(int fd, char* back_up_path, char* orig_path, size_t* size)
{
    struct stat stat;

    if (fstat(fd, &stat) == -1) {
        if (errno == EACCES) {
            perror("Must have permission to perform.\n");
            exit(EXIT_FAILURE);
        } else if (errno == EBADF) {
            perror("File descriptor is not valid.\n");
            exit(EXIT_FAILURE);
        }
    }

    *size  = stat.st_size;

    snprintf(back_up_path, MAX_PATH_NAME, "%s.bak", orig_path);

    int fd_out = open(back_up_path, O_RDWR | O_CREAT, 0660);
    if (fd_out < 0) {
        perror("Open file config failed.\n");
        return -1;
    }
    int ret;
    off64_t off_in = 0;
    off64_t off_out = 0;
    ssize_t remaining = *size;
    do {
        ret = copy_file_range(fd, &off_in, fd_out, &off_out, remaining, 0);
        if (ret < 0) {
            if (errno == EBADF) {
                perror("One or more file descriptors are not valid.\n");
            } else if (errno == EINVAL) {
                perror("arg not valid.\n");
            }
            close(fd_out);
            exit(EXIT_FAILURE);

        }

        remaining -= ret;
        off_in += ret;
        off_out+= ret;

    } while (remaining > 0);

    close(fd_out);
    return 0;
    
}

int safe_write_file(const char* tmp_path, const char* target_path)
{
    if (tmp_path == NULL || target_path == NULL) {
        perror("Null param.\n");
        exit(EXIT_FAILURE);
    }

    if (rename(tmp_path, target_path) == -1) {
        if (errno == EACCES) {
            perror("write permisstion require.\n");
            exit(EXIT_FAILURE);
        } else if (errno == EBUSY) {
            perror("Files are used by another process.\n");
            exit(EXIT_FAILURE);
        }
    }

    return 0;
}


int read_param_config(char *path, char *param)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("Open file config failed.\n");
        exit(EXIT_FAILURE);
    }
    int ret = flock(fd, LOCK_SH | LOCK_NB);
    if (ret != 0) {
        if (errno == EINVAL) {
            perror("Invalid param in flock().\n");
            close(fd);
            exit(EXIT_FAILURE);
        } else if (errno == ENOLCK) {
            perror("Run out of mem to allocating a lock.\n");
            close(fd);
            exit(EXIT_FAILURE); 
        } else if (errno == EWOULDBLOCK) {
            perror("Another Process use lock_ex.\n");
            close(fd);
            exit(EXIT_FAILURE);
        }
    }
    char buf[BUFFER_SIZE];
    ssize_t read_byte = read(fd, buf, sizeof(buf)-1);

    if (read_byte == 0) {
        printf("file empty.\n");
        exit(EXIT_FAILURE);
    } 
    
    if (read_byte == -1) {
        if (errno == EINTR) {
            perror("read file failed EINTR\n");
            ret = flock(fd, LOCK_UN);
            close(fd);
            return -1;
        }
    }

    buf[read_byte] = '\0';
    
    char *state;
    char *p = strtok_r(buf, "\n", &state);

    int found = 0;
    while (p != NULL) {
        size_t len = strlen(p);
        if (len > 0 && p[len-1] == '\r') {
            p[len-1] = '\0';
        } 

        if (p[0] != '\0' && p[0] != '#') {
            char* delimiter = strchr(p, '=');
            if (delimiter != NULL) {
                *delimiter = '\0';
                if (strcmp(param, p) == 0) {
                    found = 1;
                    char *output = delimiter + 1;
                    printf("found param: %s=%s", param, output);
                    break;
                }
            }
        }

        p = strtok_r(NULL, "\n", &state);
    }

    if (found == 0) {
        printf("Can't find param: %s.\n", param);
    }

    ret = flock(fd, LOCK_UN);
    if (ret < 0) {
        perror("undefined.\n");
    }
    ret = close(fd);

    if (ret < 0) {
        perror("undefined.\n");
    }

    return 0;

}

int write_param_config(char* path, char *param, char *value)
{
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        perror("Open file config failed.\n");
        exit(EXIT_FAILURE);
    }

    int ret = flock(fd, LOCK_EX | LOCK_NB);

    if (ret != 0) {
        if (errno == EINVAL) {
            perror("Invalid param in flock().\n");
            close(fd);
            exit(EXIT_FAILURE);
        } else if (errno == ENOLCK) {
            perror("Run out of mem to allocating a lock.\n");
            close(fd);
            exit(EXIT_FAILURE); 
        } else if (errno == EWOULDBLOCK) {
            perror("Another Process use lock_ex.\n");
            close(fd);
            exit(EXIT_FAILURE);
        }
    }

    char back_up_path[MAX_PATH_NAME];
    size_t size;
    if (back_up_file(fd, back_up_path, path, &size) != 0) {
        perror("Backup failed!\n");
        flock(fd, LOCK_UN);
        close(fd);
        return -1;   
    }


    char buf[BUFFER_SIZE];
    ssize_t read_byte = read(fd, buf, sizeof(buf)-1);

    if (read_byte == 0) {
        printf("file empty.\n");
        ret = flock(fd, LOCK_UN);
        exit(EXIT_FAILURE);
    } 
    
    if (read_byte == -1) {
        if (errno == EINTR) {
            perror("read file failed EINTR\n");
            ret = flock(fd, LOCK_UN);
            close(fd);
            exit(EXIT_FAILURE);
        }
    }

    char tmp_file[MAX_PATH_NAME];
    snprintf(tmp_file, MAX_PATH_NAME, "%s.tmp", path);
    int tmp_fd = open(tmp_file, O_WRONLY | O_CREAT | O_CLOEXEC, 0660);

    if (tmp_fd < 0) {
        perror("Open file config failed.\n");
        flock(fd, LOCK_UN);
        close(fd);
        exit(EXIT_FAILURE);
    }

    buf[read_byte] = '\0';
    
    char *state;
    char *p = strtok_r(buf, "\n", &state);
    char tmp[MAX_PATH_NAME];
    int found = 0;
    while (p != NULL) {
        size_t len = strlen(p);
        if (len > 0 && p[len-1] == '\r') {
            p[len-1] = '\0';
        } 

        if (p[0] != '\0' && p[0] != '#') {
            char* delimiter = strchr(p, '=');
            if (delimiter != NULL) {
                *delimiter = '\0';
                if (strcmp(param, p) == 0) {
                    found = 1;
                    int n = snprintf(tmp, MAX_PATH_NAME, "%s=%s\n", p, value);

                    ret = write(tmp_fd, tmp, n);
                    if (ret < 0) {
                        perror("write changes failed.\n");
                        close(fd);
                        close(tmp_fd);
                        exit(EXIT_FAILURE);
                    }
                    p = strtok_r(NULL, "\n", &state);
                    continue;
                }
                *delimiter = '=';
            }
        }

        ret = write(tmp_fd, p, strlen(p));
        if (ret < 0) {
            perror("write changes failed.\n");
            close(fd);
            close(tmp_fd);
            unlink(tmp_file);
            flock(fd, LOCK_UN);
            exit(EXIT_FAILURE);
        }
        ret = write(tmp_fd, "\n", 1);
        if (ret < 0) {
            perror("write changes failed.\n");
            close(fd);
            close(tmp_fd);
            exit(EXIT_FAILURE);
        }
        p = strtok_r(NULL, "\n", &state);
    }

    if (found == 0) {
        int n = snprintf(tmp, MAX_PATH_NAME, "%s=%s\n", param, value);
        ret = write(tmp_fd, tmp, n);
        if (ret < 0) {
            perror("write param additionally failed.\n");
        }
    }
    
    if (fsync(tmp_fd) < 0) {
        perror("Lỗi xả bộ đệm fsync!\n");
        close(tmp_fd);
        unlink(tmp_file);           
        flock(fd, LOCK_UN);
        close(fd);
        return -1;      
    }

    if (safe_write_file(tmp_file, path) != 0) {
        perror("Atomic exchange failed.\n");
        unlink(tmp_file);
        restore_file(back_up_path, path);
    } else {
        unlink(back_up_path);
    }

    close(tmp_fd);
    ret = flock(fd, LOCK_UN);
    if (ret < 0) {
        perror("undefined.\n");
    }

    ret = close(fd);

    if (ret < 0) {
        perror("undefined.\n");
    }

    return 0;

}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <read|write> <path> [param] [value]\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "read") == 0) {
        if (argc != 4) {
            fprintf(stderr, "Usage: %s read <path> <param>\n", argv[0]);
            return 1;
        }
        return read_param_config(argv[2], argv[3]);
    }
    else if (strcmp(argv[1], "write") == 0) {
        if (argc != 5) {
            fprintf(stderr, "Usage: %s write <path> <param> <value>\n", argv[0]);
            return 1;
        }
        return write_param_config(argv[2], argv[3], argv[4]);
    }
    else {
        fprintf(stderr, "Invalid command. Use 'read' or 'write'.\n");
        return 1;
    }
}