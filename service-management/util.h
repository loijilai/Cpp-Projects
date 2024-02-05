#define PARENT_READ_FD 3
#define PARENT_WRITE_FD 4
#define MAX_CHILDREN 8
#define MAX_FIFO_NAME_LEN 64
#define MAX_SERVICE_NAME_LEN 16
#define MAX_CMD_LEN 128
#define MAX_FIFO_NAME_LEN 64
#define BUFFER_SIZE 1024
#include <sys/types.h>
typedef struct service{
    pid_t pid;
    int read_child_fd;
    int write_child_fd;
    struct service *prev;
    struct service *next;
    char name[MAX_SERVICE_NAME_LEN];
} service;

