#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>

#include "util.h"

#define ERR_EXIT(s) perror(s), exit(errno);

// define signals

// Data structures
static unsigned long secret;
static char service_name[MAX_SERVICE_NAME_LEN];
service *head, *tail;

void initialize_service_list() {
    head = (service *)malloc(sizeof(service));
    tail = (service *)malloc(sizeof(service));

    head->prev = NULL;
    head->next = tail;
    tail->prev = head;
    tail->next = NULL;
}

void add_service(char *name, pid_t pid, int read_child_fd, int write_child_fd) {
    service *new_service = (service *)malloc(sizeof(service));
    strncpy(new_service->name, name, MAX_SERVICE_NAME_LEN);
    new_service->pid = pid;
    new_service->read_child_fd = read_child_fd;
    new_service->write_child_fd = write_child_fd;

    new_service->prev = tail->prev;
    new_service->next = tail;
    tail->prev->next = new_service;
    tail->prev = new_service;
}

void remove_service(char *name) {
    service *cur = head->next;
    while(cur != tail) {
        if(strncmp(cur->name, name, MAX_SERVICE_NAME_LEN) == 0) {
            cur->prev->next = cur->next;
            cur->next->prev = cur->prev;
            free(cur);
            return;
        }
        cur = cur->next;
    }
}

bool is_service_empty() {
    return head->next == tail;
}

void print_service_list() {
    fprintf(stderr, "%s ", "Service List:");
    service *cur = head->next;
    while(cur != tail) {
        fprintf(stderr, "%s->", cur->name);
        cur = cur->next;
    }
    fprintf(stderr, "NULL\n");
}

// Utilites
static inline bool is_manager() {
    return strcmp(service_name, "Manager") == 0;
}

void print_not_exist(char *service_name) {
    printf("%s doesn't exist\n", service_name);
}

void print_receive_command(char *service_name, char *cmd) {
    printf("%s has received %s\n", service_name, cmd);
    // fprintf(stderr, "[%s] has received %s\n", service_name, cmd);
}

void print_spawn(char *parent_name, char *child_name) {
    printf("%s has spawned a new service %s\n", parent_name, child_name);
    // fprintf(stderr, "[%s] has spawned a new service %s\n", parent_name, child_name);
}

void print_kill(char *target_name, int decendents_num) {
    printf("%s and %d child services are killed\n", target_name, decendents_num);
}

void print_acquire_secret(char *service_a, char *service_b, unsigned long secret) {
    printf("%s has acquired a new secret from %s, value: %lu\n", service_a, service_b, secret);
}

void print_exchange(char *service_a, char *service_b) {
    printf("%s and %s have exchanged their secrets\n", service_a, service_b);
}

void print_service_creation(char *service_name, pid_t pid, unsigned long secret) {
    printf("%s has been spawned, pid: %d, secret: %lu\n", service_name, pid, secret);
    // fprintf(stderr, "[%s] has been spawned, pid: %d, secret: %lu\n", service_name, pid, secret);
}

void parse_cmd(char *buf, char *cmd, char *parent_name, char *child_name) {
    char *token;
    token = strtok(buf, " ");
    if(token != NULL) {
        strncpy(cmd, token, 10);
        if(strncmp(cmd, "spawn", strlen("spawn")) == 0) {
            token = strtok(NULL, " ");
            strncpy(parent_name, token, MAX_SERVICE_NAME_LEN);
            token = strtok(NULL, " ");
            strncpy(child_name, token, MAX_SERVICE_NAME_LEN);
        }
        else if(strncmp(cmd, "kill", strlen("kill")) == 0) {
            token = strtok(NULL, " ");
            strncpy(parent_name, token, MAX_SERVICE_NAME_LEN);
        }
        else if(strncmp(cmd, "exchange", strlen("exchange")) == 0) {
            token = strtok(NULL, " ");
            strncpy(parent_name, token, MAX_SERVICE_NAME_LEN);
            token = strtok(NULL, " ");
            strncpy(child_name, token, MAX_SERVICE_NAME_LEN);
        }
        else if(strncmp(cmd, "printall", strlen("printall")) == 0) {
            return;
        }
        else {
            fprintf(stderr, "%s: %s\n", "Invalid command", cmd);
        }
    } else {
        ERR_EXIT("strtok");
    }
}

bool request_destination_match(char *cmd, char *service_name, char *parent_name, char *child_name) {
    if(strncmp(cmd, "spawn", strlen("spawn")) == 0)
        return strncmp(service_name, parent_name, MAX_SERVICE_NAME_LEN) == 0;
    else if(strncmp(cmd, "kill", strlen("kill")) == 0)
        return strncmp(service_name, parent_name, MAX_SERVICE_NAME_LEN) == 0;
    else if(strncmp(cmd, "exchange", strlen("exchange")) == 0)
        return strncmp(service_name, parent_name, MAX_SERVICE_NAME_LEN) == 0 ||
               strncmp(service_name, child_name, MAX_SERVICE_NAME_LEN) == 0;
    else if(strncmp(cmd, "printall", strlen("printall")) == 0)
        return false;
    return false;
}

void clearCloseOnExec(int fd) {
    int flags = fcntl(fd, F_GETFD);
    if (flags == -1) {
        ERR_EXIT("fcntl F_GETFD");
    }

    flags &= ~FD_CLOEXEC; // Clear FD_CLOEXEC bit

    if (fcntl(fd, F_SETFD, flags) == -1) {
        ERR_EXIT("fcntl F_SETFD");
    }
}

int main(int argc, char *argv[]) {
    pid_t pid = getpid();        

    if (argc != 2) {
        fprintf(stderr, "Usage: ./service [service_name]\n");
        return 0;
    }

    srand(pid);
    secret = rand();
    setvbuf(stdout, NULL, _IONBF, 0); // setting stdout to unbuffered

    strncpy(service_name, argv[1], MAX_SERVICE_NAME_LEN);

    // Service Creation Message
    initialize_service_list();
    print_service_creation(service_name, pid, secret);

    if(!is_manager()) {
        int ret_bytes = write(PARENT_WRITE_FD, "SIGSPAWN", strlen("SIGSPAWN")); // success signal
        if(ret_bytes < 0) {
            fprintf(stderr, "ret_bytes: %d\n", ret_bytes);
            ERR_EXIT("write");
        }
    }


    while(true) {
        // Handle command from stdin
        // command format: spawn [parent_service] [new_child_service]
        //                 kill [target_service]
        //                 exchange [service_a] [service_b]
        char buf[BUFFER_SIZE], tmp[BUFFER_SIZE];
        char cmd[10];
        char parent_name[MAX_SERVICE_NAME_LEN];
        char child_name[MAX_SERVICE_NAME_LEN];

        if(is_manager()) {
            fprintf(stderr, "[%s] Waiting fgets...\n", service_name);
            fgets(buf, MAX_CMD_LEN, stdin);
            buf[strcspn(buf, "\n")] = '\0';
        } else {
            fprintf(stderr, "[%s] Waiting command...\n", service_name);
            int ret_bytes = read(PARENT_READ_FD, buf, BUFFER_SIZE);
            if(ret_bytes < 0) {
                fprintf(stderr, "ret_bytes: %d\n", ret_bytes);
                ERR_EXIT("read");
            }
            buf[ret_bytes] = '\0';
        }

        print_receive_command(service_name, buf);
        strcpy(tmp, buf); // copy buf to tmp, because strtok will modify buf
        parse_cmd(tmp, cmd, parent_name, child_name);

        bool found = false;
        if(request_destination_match(cmd, service_name, parent_name, child_name)) {
            if(strncmp(cmd, "spawn", strlen("spawn")) == 0) {
                int parent_to_child_fd[2], child_to_parent_fd[2]; 

                // Creating pipes
                if (pipe2(parent_to_child_fd, O_CLOEXEC) == -1 || pipe2(child_to_parent_fd, O_CLOEXEC) == -1) {
                    ERR_EXIT("pipe2")
                }

                pid_t pid = fork();
                if(pid == -1)
                    ERR_EXIT("fork");

                if(pid == 0) {
                    // duplication of file descriptors
                    // The close-on-exec flag set on a file descriptor is not copied by dup2()
                    dup2(parent_to_child_fd[0], PARENT_READ_FD);
                    dup2(child_to_parent_fd[1], PARENT_WRITE_FD);
                    // but for clarity, we set the close-on-exec flag explicitly
                    clearCloseOnExec(PARENT_READ_FD);
                    clearCloseOnExec(PARENT_WRITE_FD);

                    // execute service
                    execl("./service", "service", child_name, NULL);
                } else {
                    // close unused end of the pipe
                    close(parent_to_child_fd[0]);
                    close(child_to_parent_fd[1]);

                    // wait for the child to send the creation message through pipe
                    // fprintf(stderr, "[%s] Waiting for the child to send the creation message\n", service_name);
                    read(child_to_parent_fd[0], tmp, BUFFER_SIZE);
                    if(strncmp(tmp, "SIGSPAWN", strlen("SIGSPAWN")) != 0)
                        ERR_EXIT("SIGSPAWN");
                    
                    // add new service to the list
                    found = true;
                    add_service(child_name, pid, child_to_parent_fd[0], parent_to_child_fd[1]);

                }
            } else if(strncmp(cmd, "kill", strlen("kill")) == 0) {
                // kill all children
            } else if(strncmp(cmd, "exchange", strlen("exchange")) == 0) {
                // exchange secret with child
            }
                
        } else {
            // destination not match: forward command
            // if (strncmp(cmd, "printall", strlen("printall")) == 0) {
            //     fprintf(stderr, "===== Service Name: %s =====\n", service_name);
            //     print_service_list();
            //     fprintf(stderr, "=============================\n");
            //     // invoke child service
            //     for(service *cur = head->next; cur != tail; cur = cur->next) {
            //         write(cur->write_child_fd, "printall", strlen("printall"));
            //         read(cur->read_child_fd, tmp, BUFFER_SIZE);
            //         if(strncmp(tmp, "SIGPRINTALL\n", strlen("SIGPRINTALL\n")) != 0)
            //             ERR_EXIT("SIGPRINTALL");
            //     }
            //     // notify parent that I've finished the command
            //     write(PARENT_WRITE_FD, "SIGPRINTALL\n", strlen("SIGPRINTALL\n"));
            // }
            // invoke child service
            for(service *cur = head->next; cur != tail; cur = cur->next) {
                fprintf(stderr, "[%s] Forwarding command to [%s]\n", service_name, cur->name);
                write(cur->write_child_fd, buf, strlen(buf));
                read(cur->read_child_fd, tmp, BUFFER_SIZE);
                if(strncmp(tmp, "SIGSPAWN", strlen("SIGSPAWN")) == 0 ||
                   strncmp(tmp, "SIGKILL", strlen("SIGKILL")) == 0 ||
                   strncmp(tmp, "SIGEXCHANGE", strlen("SIGEXCHANGE")) == 0) {
                    found = true;
                    break;
                }
            }
        }
        if(strncmp(cmd, "spawn", strlen("spawn")) == 0) {
            if(found) {
                if(is_manager())
                    print_spawn(parent_name, child_name);
                else
                    write(PARENT_WRITE_FD, tmp, sizeof(tmp));
            } else {
                if(is_manager())
                    print_not_exist(parent_name);
                else
                    write(PARENT_WRITE_FD, "NOTFOUND", strlen("NOTFOUND"));
            }
        }
    }

    return 0;
}
