#include "hw1.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdbool.h>
#include <sys/poll.h>


#define ERR_EXIT(a) do { perror(a); exit(1); } while(0)
#define BUFFER_SIZE 512

typedef struct {
    char hostname[512];  // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
    char host[512];  // client's host
    int conn_fd;  // fd to talk with client
    char buf[BUFFER_SIZE];  // data sent by/to client
    size_t buf_len;  // bytes used by buf
    int id;
} request;

// initailize a server, exit for error
static void init_server(unsigned short port);

// initailize a request instance
static void init_request(request* reqP);

// free resources used by a request instance
static void free_request(request* reqP);

int lock_record(int fd, int index);

int unlock_record(int fd, int index);

int is_record_locked(int fd, int index);

void writeBoard(int pollID, FILE* board);

int add_client_to_poll_list(int clientfd);

int remove_client_from_poll_list(int clientfd);

int cnt_record(FILE* board);

// Global varaibles
server svr;  // server
request* requestP = NULL;  // point to a list of requests
int maxfd;  // size of open file descriptor table, size of request list
int last = 0;
// pollID to locked record
int pollID2record[MAX_CLIENTS+1];
struct pollfd poll_list[MAX_CLIENTS+1]; // add listen_fd
bool locked[RECORD_NUM];

int main(int argc, char** argv) {

    // Parse args.
    if (argc != 2) {
        ERR_EXIT("usage: [port]");
        exit(1);
    }

    struct sockaddr_in cliaddr;  // used by accept(), Declares a structure for storing the client address
    int clilen; // Stores the length of the client address.

    // Initialize server
    init_server((unsigned short) atoi(argv[1]));
    fprintf(stderr, "\nstarting on %.80s, port %d, listen_fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);
    // Initialize pollID2record
    for (int i = 0; i < MAX_CLIENTS+1; i++) {
        pollID2record[i] = -1;
    }

    // Initialize BulletinBoard
    FILE* board = fopen(RECORD_PATH, "r+");
    if(board == NULL)
        ERR_EXIT("Fail to open board");
    fprintf(stderr, "Board fd: %d\n", fileno(board));

    // Initialize locked list
    for(int i = 0; i < RECORD_NUM; i++)
        locked[i] = false;

    // Initialize poll_list, fd = -1 to indicate available
    for(int i = 0; i < MAX_CLIENTS+1; i++) {
        poll_list[i].fd = -1;
        poll_list[i].events = 0;
        poll_list[i].revents = 0;
    }
    poll_list[0].fd = svr.listen_fd;
    poll_list[0].events = POLLIN;
    int timeout = -1;

    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);

    while (1) {
        if(poll(poll_list, MAX_CLIENTS+1, timeout) < 0)
            ERR_EXIT("poll");
        // Check for which pollID is available
        for(int pollID = 0; pollID < MAX_CLIENTS+1; pollID++) {

            // Can read client data
            if(poll_list[pollID].revents & POLLIN) { 
                fprintf(stderr, "%s\n", "===================");
                fprintf(stderr, "pollID: %d\n", pollID);
                // read case 1: listen_fd
                if(poll_list[pollID].fd == svr.listen_fd) {
                    clilen = sizeof(cliaddr);
                    int conn_fd = accept(svr.listen_fd, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen);
                    if (conn_fd < 0) {
                        if (errno == EINTR || errno == EAGAIN) continue;  // try again
                        if (errno == ENFILE) {
                            (void) fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
                            continue;
                        }
                        ERR_EXIT("accept");
                    }

                    requestP[conn_fd].conn_fd = conn_fd; // fd to talk to client
                    strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr)); // fill in the client host name
                    fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);
                    add_client_to_poll_list(conn_fd);
                }
                // read case 2: other clients send request
                else { 
                    // read the request sent by client
                    int fd = poll_list[pollID].fd;
                    fprintf(stderr, "fd: %d\n", fd);
                    memset(requestP[fd].buf, 0, BUFFER_SIZE);
                    int ret = read(fd, requestP[fd].buf, BUFFER_SIZE);
                    if(ret < 0) {
                        close(fd);
                        ERR_EXIT("read client request");
                    } else if (ret == sizeof(record)) {
                        writeBoard(pollID, board);
                        continue;
                    }

                    fprintf(stderr, "Request: %s\n", requestP[fd].buf);
                    if(strncmp(requestP[fd].buf, "post", strlen("post")) == 0) {
                        // Only set lock, does not write board
                        int recordNum = cnt_record(board);
                        fprintf(stderr, "recordNum before locked: %d\n", recordNum);

                        int recordIndex;
                        if(recordNum < RECORD_NUM) // not full
                            recordIndex = recordNum;
                        else // full
                            recordIndex = last % RECORD_NUM;

                        int lockNum = 0;
                        int i = recordIndex;

                        // find available record to lock
                        do
                        {
                            if(is_record_locked(fileno(board), i)) {
                                fprintf(stderr, "access locked recordIndex %d\n", i);
                                lockNum++;
                            } else {
                                lock_record(fileno(board), i);
                                pollID2record[pollID] = i;
                                last = i + 1;
                                break;
                            }
                            i++;
                            i %= RECORD_NUM;

                        } while (i != recordIndex);
                        

                        // write to client to notify success or failure
                        poll_list[pollID].events = POLLOUT;
                        if(poll(&poll_list[pollID], 1, timeout) < 0)
                            ERR_EXIT("poll");
                        poll_list[pollID].events = POLLIN; // NOTE
                        
                        int ret;
                        if(lockNum == RECORD_NUM) {
                            fprintf(stderr, "%s\n", "No lock available!");
                            ret = write(fd, "FAIL", strlen("FAIL"));
                        }
                        else
                            ret = write(fd, "SUCCESS", strlen("SUCCESS"));
                        if(ret < 0) {
                            ERR_EXIT("Fail to send records to client");
                        }

                    } else if(strncmp(requestP[fd].buf, "pull", strlen("pull")) == 0) {
                        record rec;
                        int lockNum = 0;
                        int recordNum = 0;
                        fseek(board, 0, SEEK_SET);
                        for(int i = 0; i < RECORD_NUM; i++) {
                            // collect unlocked post
                            if(is_record_locked(fileno(board), i)) {
                                lockNum++;
                                continue;
                            }
                            // read record one at a time and check if it is empty
                            fseek(board, i*sizeof(record), SEEK_SET); // NOTE: set to correct read position
                            if(fread(&rec, sizeof(rec), 1, board) == 1) {
                                if (strncmp(rec.From, "\0", 1) == 0) { // NOTE: board may be full of '\0'
                                    continue;
                                }
                                // collect record inside buffer
                                memcpy(requestP[fd].buf + recordNum * sizeof(rec), &rec, sizeof(rec));
                                recordNum++;
                            }
                        }
                        fprintf(stderr, "recordNum: %d\n", recordNum);

                        if(lockNum > 0)
                            printf("[Warning] Try to access locked post - %d\n", lockNum);

                        // Sent buffer to client
                        poll_list[pollID].events = POLLOUT;
                        if(poll(&poll_list[pollID], 1, timeout) < 0)
                            ERR_EXIT("poll");
                        poll_list[pollID].events = POLLIN; // NOTE

                        int ret;
                        if(recordNum == 0) {
                            ret = write(fd, "NODATA", strlen("NODATA"));
                        } else {
                            ret = write(fd, requestP[fd].buf, recordNum * sizeof(record));
                        }
                        if(ret < 0) {
                            ERR_EXIT("Fail to send records to client");
                        }
                    } else if(strncmp(requestP[fd].buf, "exit", strlen("exit")) == 0) {
                        // close fd
                        // free request
                        // clean poll_list
                        fprintf(stderr, "%s %d\n", "Exit fd:", fd);
                        close(fd);
                        free_request(&requestP[fd]);
                        remove_client_from_poll_list(fd);
                    }
                }
                fprintf(stderr, "%s\n", "===================");
            }
        }
    }
    free(requestP);
    return 0;
}

int lock_record(int fd, int index) {
    fprintf(stderr, "Locked record index: %d\n", index);
    struct flock lock;

    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = index * sizeof(record);
    lock.l_len = sizeof(record);
    locked[index] = true;

    if(fcntl(fd, F_SETLK, &lock) == -1)
        ERR_EXIT("Error locking record");
    return 0;
}

int unlock_record(int fd, int index) {
    fprintf(stderr, "Unlocked record index: %d\n", index);
    struct flock lock;

    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = index * sizeof(record);
    lock.l_len = sizeof(record);
    locked[index] = false;

    if(fcntl(fd, F_SETLK, &lock) == -1)
        ERR_EXIT("Error unlocking record");
    return 0;
}

int is_record_locked(int fd, int index) {
    struct flock lock;

    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = index * sizeof(record);
    lock.l_len = sizeof(record);

    if(fcntl(fd, F_GETLK, &lock) == -1)
        ERR_EXIT("Error checking lock state");
    return lock.l_type != F_UNLCK || locked[index];
}

void writeBoard(int pollID, FILE* board) {
    if(board == NULL) {
        ERR_EXIT("open board failed");
    } else {
        int recordIndex = pollID2record[pollID];
        fprintf(stderr, "Load back record index: %d of pollID %d\n", recordIndex, pollID);
        pollID2record[pollID] = -1;
        record rec;
        memcpy(&rec, requestP[poll_list[pollID].fd].buf, sizeof(record));
        printf("[Log] Receive post from %s\n", rec.From);
        fprintf(stderr, "[Log] Receive post content %s\n", rec.Content);

        // Note: Do not use fseek + fwrite, use pwrite instead to prevent race condition
        // fseek(board, recordIndex*sizeof(record), SEEK_SET);
        // fwrite(&rec, sizeof(rec), 1, board);
        pwrite(fileno(board), &rec, sizeof(rec), recordIndex*sizeof(record));

        fflush(board);
        unlock_record(fileno(board), recordIndex);
    }
    fprintf(stderr, "%s\n", "===================");
}

int add_client_to_poll_list(int clientfd) {
    for(int i = 0; i < MAX_CLIENTS+1; i++) {
        if(poll_list[i].fd == -1) {
            poll_list[i].fd = clientfd;
            poll_list[i].events = POLLIN;
            return 0; // Success
        }
    }
    return -1;
}

int remove_client_from_poll_list(int clientfd) {
    for(int i = 0; i < MAX_CLIENTS+1; i++) {
        if(poll_list[i].fd == clientfd) {
            poll_list[i].fd = -1;
            poll_list[i].events = 0;
            poll_list[i].revents = 0;
            return 0;
        }
    }
    return -1;
}

int cnt_record(FILE* board) {
    if(board == NULL)
        ERR_EXIT("open board failed");

    fseek(board, 0, SEEK_END);
    long fileSize = ftell(board);
    return fileSize / sizeof(record);
}

// ======================================================================================================
// You don't need to know how the following codes are working

static void init_request(request* reqP) {
    reqP->conn_fd = -1;
    reqP->buf_len = 0;
    reqP->id = 0;
}

static void free_request(request* reqP) {
    init_request(reqP);
}

static void init_server(unsigned short port) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;
    // Socket creation
    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0) ERR_EXIT("socket");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&tmp, sizeof(tmp)) < 0) {
        ERR_EXIT("setsockopt");
    }
    // Binding and listening
    if (bind(svr.listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0) {
        ERR_EXIT("listen");
    }

    // Get file descripter table size and initialize request table
    maxfd = getdtablesize();
    requestP = (request*) malloc(sizeof(request) * maxfd);
    if (requestP == NULL) {
        ERR_EXIT("out of memory allocating all requests");
    }
    for (int i = 0; i < maxfd; i++) {
        init_request(&requestP[i]);
    }
    requestP[svr.listen_fd].conn_fd = svr.listen_fd;
    strcpy(requestP[svr.listen_fd].host, svr.hostname);

    return;
}
