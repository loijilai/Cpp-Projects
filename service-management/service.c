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
            close(cur->read_child_fd);
            close(cur->write_child_fd);
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
    fprintf(stderr, "[%s] service list: ", service_name);
    service *cur = head->next;
    while(cur != tail) {
        fprintf(stderr, "%s->", cur->name);
        cur = cur->next;
    }
    fprintf(stderr, "NULL\n");
}

void clear_service_list() {
    service *cur = head->next;
    while(cur != tail) {
        service *tmp = cur;
        close(cur->read_child_fd);
        close(cur->write_child_fd);
        cur = cur->next;
        free(tmp);
    }
    free(head);
    free(tail);
}

bool is_service_exist(char *name) {
    service *cur = head->next;
    while(cur != tail) {
        if(strncmp(cur->name, name, MAX_SERVICE_NAME_LEN) == 0)
            return true;
        cur = cur->next;
    }
    return false;
}

// Utilites
static inline bool is_manager() {
    return strcmp(service_name, "Manager") == 0;
}

void print_not_exist(char *service_name) {
    printf("%s doesn't exist\n", service_name);
}

void print_receive_command(char *service_name, char *cmd) {
    if(strstr(cmd, "NoPrint") != NULL)
        return;
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

void parse_cmd(char *buf, char *cmd, char *parent_name, char *child_name, int *exchange_target_left) {
    char *token;
    token = strtok(buf, " ");
    if(token != NULL) {
        strncpy(cmd, token, 10);
        if(strncmp(cmd, "spawn", strlen("spawn")) == 0) {
            token = strtok(NULL, " ");
            strncpy(parent_name, token, MAX_SERVICE_NAME_LEN);
            token = strtok(NULL, " ");
            strncpy(child_name, token, MAX_SERVICE_NAME_LEN);
            *exchange_target_left = -1;
        }
        else if(strncmp(cmd, "kill", strlen("kill")) == 0) {
            token = strtok(NULL, " ");
            strncpy(parent_name, token, MAX_SERVICE_NAME_LEN);
            child_name[0] = '\0';
            *exchange_target_left = -1;
        }
        else if(strncmp(cmd, "exchange", strlen("exchange")) == 0) {
            fprintf(stderr, "cmd: %s\n", cmd);
            token = strtok(NULL, " ");
            strncpy(parent_name, token, MAX_SERVICE_NAME_LEN);
            fprintf(stderr, "parent_name: %s\n", parent_name);
            token = strtok(NULL, " ");
            strncpy(child_name, token, MAX_SERVICE_NAME_LEN);
            fprintf(stderr, "child_name: %s\n", child_name);

            if(is_manager()) {
                *exchange_target_left = 2;
            } else {
                token = strtok(NULL, " ");
                *exchange_target_left = atoi(token);
            }
            fprintf(stderr, "exchange_target_left: %d\n", *exchange_target_left);
        }
        else {
            fprintf(stderr, "%s: %s\n", "Invalid command", cmd);
            ERR_EXIT("parse_cmd");
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
        char full_command[BUFFER_SIZE], tmp[BUFFER_SIZE];
        char cmd[10];
        char parent_name[MAX_SERVICE_NAME_LEN];
        char child_name[MAX_SERVICE_NAME_LEN];
        int exchange_target_left;

        if(is_manager()) {
            fprintf(stderr, "[%s] Waiting fgets...\n", service_name);
            fgets(full_command, MAX_CMD_LEN, stdin);
            full_command[strcspn(full_command, "\n")] = '\0';
        } else {
            fprintf(stderr, "[%s] Waiting command...\n", service_name);
            int ret_bytes = read(PARENT_READ_FD, full_command, MAX_CMD_LEN);
            if(ret_bytes < 0) {
                fprintf(stderr, "ret_bytes: %d\n", ret_bytes);
                ERR_EXIT("read");
            }
            full_command[ret_bytes] = '\0';
        }
        fprintf(stderr, "--------------start------------------\n");
        print_service_list();
        print_receive_command(service_name, full_command);
        fprintf(stderr, "---------------end-----------------\n");
        strcpy(tmp, full_command); // copy full_command to tmp, because strtok will modify full_command
        parse_cmd(tmp, cmd, parent_name, child_name, &exchange_target_left);
        
        // exchange pre-process
        if(strncmp(tmp, "exchange", strlen("exchange")) == 0) {
            if(is_manager()) {
                // make FIFO
                const char* service_a = parent_name;
                const char* service_b = child_name;
                char fifo_a_to_b[MAX_FIFO_NAME_LEN], fifo_b_to_a[MAX_FIFO_NAME_LEN];

                // Generate FIFO names
                snprintf(fifo_a_to_b, MAX_FIFO_NAME_LEN, "%s_to_%s.fifo", service_a, service_b);
                snprintf(fifo_b_to_a, MAX_FIFO_NAME_LEN, "%s_to_%s.fifo", service_b, service_a);
                fprintf(stderr, "[%s] fifo_a_to_b: %s\n", service_name, fifo_a_to_b);
                fprintf(stderr, "[%s] fifo_b_to_a: %s\n", service_name, fifo_b_to_a);
                if (mkfifo(fifo_a_to_b, 0666) == -1 || mkfifo(fifo_b_to_a, 0666) == -1) {
                    ERR_EXIT("mkfifo");
                }
            }
        }


        bool spawn_found = false;
        bool kill_found = false;
        char ksignal_from_child[BUFFER_SIZE];
        char ssignal_from_child[BUFFER_SIZE];
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
                    read(child_to_parent_fd[0], ssignal_from_child, BUFFER_SIZE);
                    if(strncmp(ssignal_from_child, "SIGSPAWN", strlen("SIGSPAWN")) != 0)
                        ERR_EXIT("SIGSPAWN");
                    
                    // add new service to the list
                    spawn_found = true;
                    add_service(child_name, pid, child_to_parent_fd[0], parent_to_child_fd[1]);

                }
            } else if(strncmp(cmd, "kill", strlen("kill")) == 0) {
                // kill all children (send kill [child_name] to all children)
                // count killed number
                char kill_child_cmd[MAX_CMD_LEN];
                char signal_to_parent[BUFFER_SIZE];
                int killed_num = 0;
                for(service *cur = head->next; cur != tail; cur = cur->next) {
                    snprintf(kill_child_cmd, BUFFER_SIZE, "kill %s NoPrint", cur->name);
                    write(cur->write_child_fd, kill_child_cmd, strlen(kill_child_cmd));
                    fprintf(stderr, "[%s] Waiting [%s] killed...\n", service_name, cur->name); 
                    read(cur->read_child_fd, ksignal_from_child, BUFFER_SIZE);
                    if(strncmp(ksignal_from_child, "SIGKILL", strlen("SIGKILL")) != 0) {
                        fprintf(stderr, "[%s] ksignal_from_child: %s\n", service_name, ksignal_from_child);
                        ERR_EXIT("SIGKILL");
                    }
                    int status;
                    // waitpid and check status
                    if(waitpid(cur->pid, &status, 0) == -1)
                        ERR_EXIT("waitpid");
                    if(WIFEXITED(status) && WEXITSTATUS(status) != 0)
                        ERR_EXIT("WEXITSTATUS");
                    
                    char* token = strtok(ksignal_from_child, "-"); // extract number
                    token = strtok(NULL, "-");
                    int num = atoi(token);
                    killed_num += (num + 1);
                }
                // clean linked list (close all fds and free memory)
                clear_service_list();
                // send SIGKILL to parent
                snprintf(signal_to_parent, BUFFER_SIZE, "SIGKILL-%d", killed_num);
                int len = strlen(signal_to_parent);

                if(is_manager())
                    print_kill(service_name, killed_num);
                else
                    write(PARENT_WRITE_FD, signal_to_parent, len+1);

                fprintf(stderr, "[%s] Before exit: %s\n", service_name, signal_to_parent);
                exit(0);
            } else if(strncmp(cmd, "exchange", strlen("exchange")) == 0) {
                // exchange secret with child
                // write secret to FIFO
                // target hit, so exchange_target_left-1 and send exchange command to child
                // if exchange_target_left == 0, then do not pass down the command
                // send signal to parent to notify how many exchange_target_left left
            }
                
        } else {
            // destination not match: forward command
            if(strncmp(cmd, "spawn", strlen("spawn")) == 0) {
                for(service *cur = head->next; cur != tail; cur = cur->next) {
                    fprintf(stderr, "[%s] Forwarding command to [%s]\n", service_name, cur->name);
                    write(cur->write_child_fd, full_command, strlen(full_command));
                    read(cur->read_child_fd, ssignal_from_child, BUFFER_SIZE);
                    fprintf(stderr, "[%s] receive ssignal_from_child from %s: %s\n", service_name, cur->name, ssignal_from_child);
                    if(strncmp(ssignal_from_child, "SIGSPAWN", strlen("SIGSPAWN")) == 0) {
                        spawn_found = true;
                        break;
                    } else if(strncmp(ssignal_from_child, "NOTFOUND", sizeof("NOTFOUND")) == 0) {
                        spawn_found = false;
                    }
                }
            } else if(strncmp(cmd, "kill", strlen("kill")) == 0) {
                for(service *cur = head->next; cur != tail; cur = cur->next) {
                    fprintf(stderr, "[%s] Forwarding command to [%s]\n", service_name, cur->name);
                    write(cur->write_child_fd, full_command, strlen(full_command));
                    read(cur->read_child_fd, ksignal_from_child, BUFFER_SIZE);
                    fprintf(stderr, "[%s] receive ksignal_from_child from %s: %s\n", service_name, cur->name, ksignal_from_child);

                    if(strncmp(ksignal_from_child, "SIGKILL", strlen("SIGKILL")) == 0) {
                        kill_found = true;
                        // Note: Do not directly use waitpid for the child, because the child may not be the target
                        if(is_service_exist(parent_name)) {
                            remove_service(parent_name); // remove the child and close fds
                            int status;
                            pid_t result = waitpid(cur->pid, &status, 0);
                            if(result == -1) {
                                if(errno == ECHILD)
                                    fprintf(stderr, "[%s] No child process [%s]\n", service_name, cur->name);
                                else
                                    ERR_EXIT("waitpid");
                            }
                        }
                        break; // early stop
                    } else if(strncmp(ksignal_from_child, "NOTFOUND", sizeof("NOTFOUND")) == 0) {
                        kill_found = false;
                    }
                }
            } else if(strncmp(cmd, "exchange", strlen("exchange")) == 0) {
                // TODO: forward exchange command to children with exchange_target_left-1
            }
        }
        if(strncmp(cmd, "spawn", strlen("spawn")) == 0) {
            if(spawn_found) {
                if(is_manager())
                    print_spawn(parent_name, child_name);
                else
                    write(PARENT_WRITE_FD, "SIGSPAWN", strlen("SIGSPAWN"));
            } else {
                if(is_manager())
                    print_not_exist(parent_name);
                else
                    write(PARENT_WRITE_FD, "NOTFOUND", sizeof("NOTFOUND"));
            }
        } else if(strncmp(cmd, "kill", strlen("kill")) == 0) {
            if(kill_found) {
                if(is_manager()) {
                    char* token = strtok(ksignal_from_child, "-"); // extract number
                    token = strtok(NULL, "-");
                    int num = atoi(token);
                    print_kill(parent_name, num);
                } else {
                    write(PARENT_WRITE_FD, ksignal_from_child, sizeof(ksignal_from_child));
                }
            } else {
                if(is_manager())
                    print_not_exist(parent_name);
                else
                    write(PARENT_WRITE_FD, "NOTFOUND", sizeof("NOTFOUND"));
            }
        }
    }

    return 0;
}
