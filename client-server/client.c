#include "hw1.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/poll.h>


#define ERR_EXIT(a) do { perror(a); exit(1); } while(0)
#define BUFFER_SIZE 512
#define COMMAND_SIZE 6

typedef struct {
    char* ip; // server's ip
    unsigned short port; // server's port
    int conn_fd; // fd to talk with server
    char buf[BUFFER_SIZE]; // data sent by/to server
    size_t buf_len; // bytes used by buf
} client;

client cli;
static void init_client(char** argv);
void readBoard(void);
struct pollfd fds[1];

int main(int argc, char** argv){
    
    // Parse args.
    if(argc!=3){
        ERR_EXIT("usage: [ip] [port]");
    }

    // Handling connection
    init_client(argv);
    fprintf(stderr, "connect to %s %d\n", cli.ip, cli.port);
    printf("==============================\n");
    printf("Welcome to CSIE Bulletin board\n");

    // Send pull to server
    fds[0].fd = cli.conn_fd;
    fds[0].events = POLLOUT;
    int timeout = -1;
    // fprintf(stderr, "POLLOUT: %d\n", 0);
    if(poll(fds, 1, timeout) < 0)
        ERR_EXIT("poll");

    if(fds[0].revents & POLLOUT) {
        if(write(cli.conn_fd, "pull", strlen("pull")) < 0) {
            ERR_EXIT("write to server");
        } 
    }

    // Read from server
    fds[0].events = POLLIN;
    // fprintf(stderr, "POLLIN: %d\n", 1);
    if(poll(fds, 1, timeout) < 0)
        ERR_EXIT("read from server");

    if(fds[0].revents & POLLIN) {
        printf("==============================\n");
        readBoard();
        printf("==============================\n");
    }
    

    while(1) {
        printf("Please enter your command (post/pull/exit): ");
        char command[COMMAND_SIZE];
        fgets(command, COMMAND_SIZE, stdin);

        fds[0].events = POLLOUT;
        // fprintf(stderr, "POLLOUT: %d\n", 2);
        if(poll(fds, 1, timeout) < 0)
            ERR_EXIT("poll");

        if(fds[0].revents & POLLOUT) {
            if (strncmp(command, "post", strlen("post")) == 0) {
                // fprintf(stderr, "write ret: %d\n", ret);
                if(write(cli.conn_fd, "post", 4) < 0) {
                    ERR_EXIT("write to server");
                }
                // get server write success signal
                fds[0].events = POLLIN;
                if(poll(fds, 1, timeout) < 0)
                    ERR_EXIT("poll");
                if(fds[0].revents & POLLIN) {
                    if(read(cli.conn_fd, cli.buf, BUFFER_SIZE) < 0)
                        ERR_EXIT("read from server");
                    if(strncmp(cli.buf, "FAIL", strlen("FAIL")) == 0)
                        printf("[Error] Maximum posting limit exceeded\n");
                    else if(strncmp(cli.buf, "SUCCESS", strlen("SUCCESS")) == 0)
                        fprintf(stderr, "%s\n", "Lock record sucess!");
                }

                record rec;
                printf("FROM: ");
                fgets(cli.buf, BUFFER_SIZE, stdin);
                strncpy(rec.From, cli.buf, FROM_LEN);
                rec.From[strcspn(rec.From, "\n")] = 0;
                memset(cli.buf, 0, BUFFER_SIZE);
                printf("CONTENT:\n");
                fgets(cli.buf, BUFFER_SIZE, stdin);
                strncpy(rec.Content, cli.buf, CONTENT_LEN);
                rec.Content[strcspn(rec.Content, "\n")] = 0;
                memset(cli.buf, 0, BUFFER_SIZE);
                
                fds[0].events = POLLOUT;
                // fprintf(stderr, "POLLOUT: %d\n", 3);
                if(poll(fds, 1, timeout) < 0)
                    ERR_EXIT("poll");

                if(fds[0].revents & POLLOUT) {
                    if(write(cli.conn_fd, &rec, sizeof(rec)) < 0) {
                        ERR_EXIT("write to server");
                    }
                }
            } else if (strncmp(command, "pull", strlen("pull")) == 0) {
                if(write(cli.conn_fd, "pull", 4) < 0) {
                    ERR_EXIT("write to server");
                }

                // Receive and print messages from the server
                fds[0].events = POLLIN;
                // fprintf(stderr, "POLLIN: %d\n", 4);
                if (poll(fds, 1, timeout) < 0)
                    ERR_EXIT("poll");

                if (fds[0].revents & POLLIN) {
                    // Read data from fds[i].fd
                    printf("==============================\n");
                    readBoard();
                    printf("==============================\n");
                }
            } else if (strncmp(command, "exit", strlen("exit")) == 0) {
                if(write(cli.conn_fd, "exit", 4) < 0) {
                    ERR_EXIT("write to server");
                }
                close(cli.conn_fd);
                break;
            }
        }
    }
    return 0;
}

void readBoard() {
    int received = read(cli.conn_fd, cli.buf, BUFFER_SIZE);
    if(received < 0) {
        ERR_EXIT("read from server");
    }

    if(strncmp(cli.buf, "NODATA", strlen("NODATA")) == 0)
        return;

    int recordNum = received / sizeof(record);
    record* recP = (record*) cli.buf;
    for(int i = 0; i < recordNum; i++) {
        printf("FROM: ");
        printf("%.5s\n", recP->From);
        printf("CONTENT:\n");
        printf("%.20s\n", recP->Content);
        recP++;
    }
}

static void init_client(char** argv){
    
    cli.ip = argv[1];

    if(atoi(argv[2])==0 || atoi(argv[2])>65536){
        ERR_EXIT("Invalid port");
    }
    cli.port=(unsigned short)atoi(argv[2]);

    struct sockaddr_in servaddr;
    cli.conn_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(cli.conn_fd<0){
        ERR_EXIT("socket");
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(cli.port);

    if(inet_pton(AF_INET, cli.ip, &servaddr.sin_addr)<=0){
        ERR_EXIT("Invalid IP");
    }

    if(connect(cli.conn_fd, (struct sockaddr*)&servaddr, sizeof(servaddr))<0){
        ERR_EXIT("connect");
    }

    return;
}
