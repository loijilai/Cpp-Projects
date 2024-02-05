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
    if(strstr(cmd, "NoPrint") != NULL) {
        return;
    } else if(strncmp(cmd, "read", strlen("read")) == 0) {
        fprintf(stderr, "%s has received %s\n", service_name, cmd);
    } else if(strncmp(cmd, "exchange", strlen("exchange")) == 0) {
        char command[MAX_CMD_LEN];
        char service_a[MAX_SERVICE_NAME_LEN], service_b[MAX_SERVICE_NAME_LEN];
        sscanf(cmd, "%s %s %s", command, service_a, service_b);
        printf("%s has received %s %s %s\n", service_name, command, service_a, service_b);
        return;
    } else {
        printf("%s has received %s\n", service_name, cmd);
    }
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
            token = strtok(NULL, " ");
            strncpy(parent_name, token, MAX_SERVICE_NAME_LEN);
            token = strtok(NULL, " ");
            strncpy(child_name, token, MAX_SERVICE_NAME_LEN);

            if(is_manager()) {
                *exchange_target_left = 2;
            } else {
                token = strtok(NULL, " ");
                *exchange_target_left = atoi(token);
            }
        } else if(strncmp(cmd, "read", strlen("read")) == 0) {
            token = strtok(NULL, " ");
            strncpy(parent_name, token, MAX_SERVICE_NAME_LEN);
            child_name[0] = '\0';
            *exchange_target_left = -1;
        } else {
            fprintf(stderr, "Invalid command: %s\n", cmd);
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
    else if(strncmp(cmd, "read", strlen("read")) == 0)
        return strncmp(service_name, parent_name, MAX_SERVICE_NAME_LEN) == 0;
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
        int ret_bytes = write(PARENT_WRITE_FD, "SIGSPAWN", strlen("SIGSPAWN")+1); // success signal
        if(ret_bytes < 0) {
            fprintf(stderr, "ret_bytes: %d\n", ret_bytes);
            ERR_EXIT("write");
        }
    }

    // this variable is only used by manager indicate the phase of exchange
    // 0: find exchange target and write data into the fifo, 
    // 1: service_a read data from the fifo, 2: service_b read data from the fifo
    int exchange_phase = 0; 
    // FIFO names is used when unlink
    char fifo_a_to_b[MAX_FIFO_NAME_LEN], fifo_b_to_a[MAX_FIFO_NAME_LEN];
    while(true) {
        // Handle command from stdin
        // command format: spawn [parent_service] [new_child_service]
        //                 kill [target_service]
        //                 exchange [service_a] [service_b]
        char full_command[BUFFER_SIZE], tmp[BUFFER_SIZE];
        char cmd[10];
        char parent_name[MAX_SERVICE_NAME_LEN];
        char child_name[MAX_SERVICE_NAME_LEN];
        // initialized when exchange command is received (line 284), and used when exchange command hit target(402)
        // this variable is needed because instruction "read [service_name]" cannot tell who is service_a and who is service_b
        ex_service exchange_service[2];
        int exchange_target_left;

        /********* Section 1: Get command from stdin or parent *********/
        if(exchange_phase && is_manager()) {
            snprintf(full_command, BUFFER_SIZE, "read %s", exchange_service[exchange_phase-1].name);
        }
        else if(is_manager()) {
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
        /************************ End Section 1 **********************/
        
        if(strncmp(tmp, "exchange", strlen("exchange")) == 0) {
            strncpy(exchange_service[0].name, parent_name, MAX_SERVICE_NAME_LEN);
            strncpy(exchange_service[1].name, child_name, MAX_SERVICE_NAME_LEN);
            if(is_manager()) {
                // make FIFO

                // Generate FIFO names
                snprintf(fifo_a_to_b, MAX_FIFO_NAME_LEN, "%s_to_%s.fifo", exchange_service[0].name, exchange_service[1].name);
                snprintf(fifo_b_to_a, MAX_FIFO_NAME_LEN, "%s_to_%s.fifo", exchange_service[1].name, exchange_service[0].name);
                fprintf(stderr, "[%s] fifo_a_to_b: %s\n", service_name, fifo_a_to_b);
                fprintf(stderr, "[%s] fifo_b_to_a: %s\n", service_name, fifo_b_to_a);
                if (mkfifo(fifo_a_to_b, 0666) == -1 || mkfifo(fifo_b_to_a, 0666) == -1) {
                    ERR_EXIT("mkfifo");
                }
            }
        }

        bool spawn_found = false;
        bool kill_found = false;
        bool read_found = false;
        char ksignal_from_child[BUFFER_SIZE];
        char ssignal_from_child[BUFFER_SIZE];
        char esignal_from_child[BUFFER_SIZE];
        char rsignal_from_child[BUFFER_SIZE];
        /********* Section 2: Main logic: destination match or not *********/
        if(request_destination_match(cmd, service_name, parent_name, child_name)) {
            // destination match: execute command
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
                char ksignal_to_parent[BUFFER_SIZE];
                int killed_num = 0;
                for(service *cur = head->next; cur != tail; cur = cur->next) {
                    snprintf(kill_child_cmd, BUFFER_SIZE, "kill %s NoPrint", cur->name);
                    write(cur->write_child_fd, kill_child_cmd, strlen(kill_child_cmd)+1);
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

                if(is_manager())
                    print_kill(service_name, killed_num);
                else {
                    // send SIGKILL to parent
                    snprintf(ksignal_to_parent, BUFFER_SIZE, "SIGKILL-%d", killed_num);
                    write(PARENT_WRITE_FD, ksignal_to_parent, strlen(ksignal_to_parent)+1);
                }

                exit(0);
            } else if(strncmp(cmd, "exchange", strlen("exchange")) == 0) {
                fprintf(stderr, "[%s] target hit, exchange_target_left: %d\n", service_name, exchange_target_left-1);
                // write secret to FIFO
                int fd_a_to_b, fd_b_to_a;
                char fifo_a_to_b[MAX_FIFO_NAME_LEN], fifo_b_to_a[MAX_FIFO_NAME_LEN];
                snprintf(fifo_a_to_b, MAX_FIFO_NAME_LEN, "%s_to_%s.fifo", exchange_service[0].name, exchange_service[1].name);
                snprintf(fifo_b_to_a, MAX_FIFO_NAME_LEN, "%s_to_%s.fifo", exchange_service[1].name, exchange_service[0].name);

                fd_a_to_b = open(fifo_a_to_b, O_RDWR);
                fd_b_to_a = open(fifo_b_to_a, O_RDWR);
                if(fd_a_to_b == -1 || fd_b_to_a == -1)
                    ERR_EXIT("open FIFO");

                if(strncmp(service_name, exchange_service[0].name, MAX_SERVICE_NAME_LEN) == 0) {
                    write(fd_a_to_b, &secret, sizeof(secret));
                    exchange_service[0].fifo_write_fd = fd_a_to_b;
                    exchange_service[0].fifo_read_fd = fd_b_to_a;
                    fprintf(stderr, "[%s] write secret to %s(fd: %d)\n", service_name, fifo_a_to_b, fd_a_to_b);
                    fprintf(stderr, "[%s] read fd stored: %d\n", service_name, fd_b_to_a);
                } else {
                    write(fd_b_to_a, &secret, sizeof(secret));
                    exchange_service[1].fifo_write_fd = fd_b_to_a;
                    exchange_service[1].fifo_read_fd = fd_a_to_b;
                    fprintf(stderr, "[%s] write secret to %s(fd: %d)\n", service_name, fifo_b_to_a, fd_b_to_a);
                    fprintf(stderr, "[%s] read fd stored: %d\n", service_name, fd_a_to_b);
                }

                // target hit, so forward exchange command to children with exchange_target_left-1
                exchange_target_left--;
                // if exchange_target_left == 0, then do not pass down the command
                if(exchange_target_left > 0) {
                    char exchange_cmd[MAX_CMD_LEN];
                    snprintf(exchange_cmd, MAX_CMD_LEN, "exchange %s %s %d", exchange_service[0].name, exchange_service[1].name, exchange_target_left);
                    for(service *cur = head->next; cur != tail; cur = cur->next) {
                        fprintf(stderr, "[%s] Forwarding %s to [%s]\n", service_name, exchange_cmd, cur->name);
                        write(cur->write_child_fd, exchange_cmd, strlen(exchange_cmd)+1);
                        read(cur->read_child_fd, esignal_from_child, BUFFER_SIZE);
                        fprintf(stderr, "[%s] receive esignal_from_child from %s: %s\n", service_name, cur->name, esignal_from_child);
                        // extract exchange_target_left from esignal_from_child and update exchange_target_left
                        char* token = strtok(esignal_from_child, "-");
                        token = strtok(NULL, "-");
                        exchange_target_left = atoi(token);
                        fprintf(stderr, "[%s] exchange_target_left: %d\n", service_name, exchange_target_left);
                        if(exchange_target_left == 0)
                            break; // early stop
                    }
                }
            } else if(strncmp(cmd, "read", strlen("read")) == 0) {
                // format: read [service_name]
                fprintf(stderr, "[%s] read command match\n", service_name);

                // find the correct fd to read
                for(int i = 0; i < 2; i++) {
                    if(strncmp(service_name, exchange_service[i].name, MAX_SERVICE_NAME_LEN) == 0) {
                        fprintf(stderr, "[%s] read secret from fd: %d\n", service_name, exchange_service[i].fifo_read_fd);
                        if(read(exchange_service[i].fifo_read_fd, &secret, sizeof(secret)) == -1)
                            ERR_EXIT("read secret from FIFO");
    
                        if(i == 0) {
                            // NOTE: after read secret from fifo, close the fds
                            close(exchange_service[0].fifo_read_fd);
                            close(exchange_service[0].fifo_write_fd);
                            print_acquire_secret(exchange_service[0].name, exchange_service[1].name, secret);
                        } else { // i == 1
                            close(exchange_service[1].fifo_read_fd);
                            close(exchange_service[1].fifo_write_fd);
                            print_acquire_secret(exchange_service[1].name, exchange_service[0].name, secret);
                        }
                    }
                }
                read_found = true;
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
                    } else if(strncmp(ssignal_from_child, "NOTFOUND", strlen("NOTFOUND")) == 0) {
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
                        kill_found = true;
                        break; // early stop
                    } else if(strncmp(ksignal_from_child, "NOTFOUND", strlen("NOTFOUND")) == 0) {
                        kill_found = false;
                    }
                }
            } else if(strncmp(cmd, "exchange", strlen("exchange")) == 0) {
                // forward exchange command to children with exchange_target_left unchanged
                fprintf(stderr, "[%s] Exchange target not match\n", service_name);
                char exchange_cmd[MAX_CMD_LEN];
                for(service *cur = head->next; cur != tail; cur = cur->next) {
                    fprintf(stderr, "[%s] Forwarding command to [%s]\n", service_name, cur->name);

                    snprintf(exchange_cmd, MAX_CMD_LEN, "exchange %s %s %d", exchange_service[0].name, exchange_service[1].name, exchange_target_left);
                    write(cur->write_child_fd, exchange_cmd, strlen(exchange_cmd)+1);
                    read(cur->read_child_fd, esignal_from_child, BUFFER_SIZE);
                    fprintf(stderr, "[%s] receive esignal_from_child from %s: %s\n", service_name, cur->name, esignal_from_child);
                    // extract exchange_target_left from esignal_from_child and update exchange_target_left
                    char* token = strtok(esignal_from_child, "-");
                    token = strtok(NULL, "-");
                    exchange_target_left = atoi(token);
                    fprintf(stderr, "[%s] exchange_target_left: %d\n", service_name, exchange_target_left);
                    if(exchange_target_left == 0)
                        break; // early stop
                }
            } else if(strncmp(cmd, "read", strlen("read")) == 0) {
                fprintf(stderr, "[%s] read command not match\n", service_name);
                for(service *cur = head->next; cur != tail; cur = cur->next) {
                    fprintf(stderr, "[%s] Forwarding command to [%s]\n", service_name, cur->name);
                    write(cur->write_child_fd, full_command, strlen(full_command)+1);
                    read(cur->read_child_fd, rsignal_from_child, BUFFER_SIZE);
                    if(strncmp(rsignal_from_child, "SIGREAD", strlen("SIGREAD")) == 0) {
                        fprintf(stderr, "[%s] receive rsignal_from_child from %s: %s\n", service_name, cur->name, rsignal_from_child);
                        read_found = true;
                        break; // early stop
                    } else if(strncmp(rsignal_from_child, "NOTFOUND", strlen("NOTFOUND")) == 0) {
                        read_found = false;
                    }
                }
            }
        }
        /************************ End Section 2 **********************/

        /**** Section 3: Print output to terminal or write signal to parent ****/
        if(strncmp(cmd, "spawn", strlen("spawn")) == 0) {
            if(spawn_found) {
                if(is_manager())
                    print_spawn(parent_name, child_name);
                else
                    write(PARENT_WRITE_FD, "SIGSPAWN", strlen("SIGSPAWN")+1);
            } else {
                if(is_manager())
                    print_not_exist(parent_name);
                else
                    write(PARENT_WRITE_FD, "NOTFOUND", strlen("NOTFOUND")+1);
            }
        } else if(strncmp(cmd, "kill", strlen("kill")) == 0) {
            if(kill_found) {
                if(is_manager()) {
                    char* token = strtok(ksignal_from_child, "-"); // extract number
                    token = strtok(NULL, "-");
                    int num = atoi(token);
                    print_kill(parent_name, num);
                } else {
                    write(PARENT_WRITE_FD, ksignal_from_child, strlen(ksignal_from_child)+1);
                }
            } else {
                if(is_manager())
                    print_not_exist(parent_name);
                else
                    write(PARENT_WRITE_FD, "NOTFOUND", strlen("NOTFOUND")+1);
            }
        } else if(strncmp(cmd, "exchange", strlen("exchange")) == 0) {
            if(is_manager()) {
                exchange_phase++;
                fprintf(stderr, "[%s] switch into exchange_phase: %d\n", service_name, exchange_phase);
            }
            else {
                // write signal to parent to notify how many exchange_target_left left
                char esignal_to_parent[BUFFER_SIZE];
                snprintf(esignal_to_parent, BUFFER_SIZE, "SIGEXCHANGE-%d", exchange_target_left);
                write(PARENT_WRITE_FD, esignal_to_parent, strlen(esignal_to_parent)+1);
                fprintf(stderr, "[%s] send %s to parent\n", service_name, esignal_to_parent);
            }
        } else if(strncmp(cmd, "read", strlen("read")) == 0) {
            if(read_found) {
                if(is_manager()) {
                    exchange_phase++;
                    fprintf(stderr, "[%s] switch into exchange_phase: %d\n", service_name, exchange_phase);
                    if(exchange_phase > 2) {
                        // reset the exchange_phase and unlink FIFO
                        print_exchange(exchange_service[0].name, exchange_service[1].name);
                        exchange_phase = 0;
                        fprintf(stderr, "[%s] exchange complete with exchange_phase: %d\n", service_name, exchange_phase);
                        unlink(fifo_a_to_b);
                        unlink(fifo_b_to_a);
                        fprintf(stderr, "[%s] unlink FIFO %s\n", service_name, fifo_a_to_b);
                        fprintf(stderr, "[%s] unlink FIFO %s\n", service_name, fifo_b_to_a);

                        // only close your own fds
                        if(strncmp(service_name, exchange_service[0].name, MAX_SERVICE_NAME_LEN) == 0) {
                            close(exchange_service[0].fifo_read_fd);
                            close(exchange_service[0].fifo_write_fd);
                            fprintf(stderr, "[%s] unlink FIFO %s\n", service_name, fifo_a_to_b);
                            fprintf(stderr, "[%s] unlink FIFO %s\n", service_name, fifo_b_to_a);
                        } else {
                            close(exchange_service[1].fifo_read_fd);
                            close(exchange_service[1].fifo_write_fd);
                            fprintf(stderr, "[%s] unlink FIFO %s\n", service_name, fifo_a_to_b);
                            fprintf(stderr, "[%s] unlink FIFO %s\n", service_name, fifo_b_to_a);
                        }
                    }

                } else {
                    write(PARENT_WRITE_FD, "SIGREAD", strlen("SIGREAD")+1);
                }
            } else {
                write(PARENT_WRITE_FD, "NOTFOUND", strlen("NOTFOUND")+1);
            }
        }
        /************************ End Section 3 **********************/
    }

    return 0;
}
