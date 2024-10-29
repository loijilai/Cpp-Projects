#include <stdio.h>
#include "server.h"
#include "business_logic.h"

// Global variable
server svr;
int maxfd;
request* requestP = NULL;  // point to a list of requests
struct pollfd *fds;

static nfds_t nfd; // number of fds to poll (including listen_fd)
static int num_conn = 1; // server's current number of connections
static const char* file_prefix = "./csie_trains/train_";
static const char* exit_msg = ">>> Client exit.\n";
static const char* invalid_op_msg = ">>> Invalid operation.\n";
static const char* timeout_msg = ">>> Connection timeout.\n";

static int accept_conn(void) {
    // update requestP[conn_fd]
    // 1. host
    // 2. conn_fd
    // 3. client_id
    struct sockaddr_in cliaddr;
    size_t clilen;
    int conn_fd;  // fd for a new connection with client

    clilen = sizeof(cliaddr);
    // server listen from svr.listen_fd and get a new connection with client (conn_fd)
    conn_fd = accept(svr.listen_fd, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen);
    if (conn_fd < 0) {
        if (errno == EINTR || errno == EAGAIN) return -1;  // try again
        if (errno == ENFILE) {
            (void) fprintf(stderr, "out of file descriptor table ... (maxfd %d)\n", maxfd);
                return -1;
        }
        ERR_EXIT("accept");
    }
    
    requestP[conn_fd].conn_fd = conn_fd;
    strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
    fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);
    requestP[conn_fd].client_id = (svr.port * 1000) + num_conn;    // This should be unique for the same machine.
    num_conn++;
    // Get current time and +5 sec to set the deadline
    if(gettimeofday(&requestP[conn_fd].deadline, NULL) < 0) {
        fprintf(stderr, "cannot gettimeofday\n");
        return -1;
    }
    requestP[conn_fd].deadline.tv_sec += 5;

    // Already init_request but make sure again
    requestP[conn_fd].status = INIT;
    memset(requestP[conn_fd].buf, 0, MAX_MSG_LEN);
    requestP[conn_fd].buf_len = 0;
    return conn_fd;
}

static void init_request(request* reqP) {
    reqP->conn_fd = -1;
    reqP->client_id = -1;
    reqP->buf_len = 0;
    reqP->status = INIT;
    reqP->deadline.tv_sec = -1; // Mark as unitialized
    reqP->deadline.tv_usec = -1;

    reqP->booking_info.num_of_chosen_seats = 0;
    reqP->booking_info.train_fd = -1;
    reqP->booking_info.shift_id = -1;
    for (int i = 0; i < SEAT_NUM; i++)
        reqP->booking_info.seat_stat[i] = DEFAULT;
}

// clear request structure in requestP
static void clear_request(request* reqP) {
    memset(reqP, 0, sizeof(request));
    init_request(reqP);
}

static void getfilepath(char* filepath, int extension) {
    char fp[FILE_LEN*2];
    
    memset(filepath, 0, FILE_LEN);
    sprintf(fp, "%s%d", file_prefix, extension);
    strcpy(filepath, fp);
}

int get_client_timeout(int conn_fd) {
    struct timeval now;
    if(gettimeofday(&now, NULL) < 0) {
        fprintf(stderr, "cannot gettimeofday\n");
        // TODO: should handle error
    }
    long deadline_millisecond = (requestP[conn_fd].deadline.tv_sec * 1000) + (requestP[conn_fd].deadline.tv_usec / 1000);
    long now_millisecond = (now.tv_sec * 1000) + (now.tv_usec / 1000);
    int timeout = deadline_millisecond - now_millisecond;
    return timeout;
}

static int calculate_timeout() {
    // return timeout in millisecond (10^-6)
    int num_of_clients = 0;
    int conn_fd;
    int min_timeout = INT_MAX;
    for(int i = 0; i < nfd; i++) {
        conn_fd = fds[i].fd;
        if(conn_fd == -1 || conn_fd == svr.listen_fd) // fd is cleared or server listenfd (should not timeout)
            continue;
        num_of_clients++;
        int timeout = get_client_timeout(conn_fd);
        if(timeout < 0) { // Some fd has passed deadline, should be cleared immediately
            min_timeout = 0;
        } else {
            min_timeout = (timeout < min_timeout) ? timeout : min_timeout;
        }
    }
    if(num_of_clients == 0)
        return -1; // wait indefinitely
    return min_timeout;
}

static void clean_expired_client() {
    int conn_fd;
    for(int i = 0; i < nfd; i++) {
        conn_fd = fds[i].fd;
        if(conn_fd == -1 || conn_fd == svr.listen_fd) // fd is cleared or server listenfd (should not timeout)
            continue;
        int timeout = get_client_timeout(conn_fd);
        if(timeout <= 0) { // bye bye
            fprintf(stderr, "connection timeout, closing fd %d\n", requestP[conn_fd].conn_fd);
            unlock_unpaid_seat(&requestP[conn_fd]);
            write(requestP[conn_fd].conn_fd, timeout_msg, strlen(timeout_msg));
            close(requestP[conn_fd].conn_fd);
            clear_request(&requestP[conn_fd]);
            fds[i].fd = -1; // TODO3: fd collection
        }
    }
}

void init_db(void) {
    char filename[FILE_LEN];

    for (int i = TRAIN_ID_START, j = 0; i <= TRAIN_ID_END; i++, j++) {
        getfilepath(filename, i);
#ifdef READ_SERVER
        trains[j].file_fd = open(filename, O_RDONLY);
#elif defined WRITE_SERVER
        trains[j].file_fd = open(filename, O_RDWR);
#else
        trains[j].file_fd = -1;
#endif
        if (trains[j].file_fd < 0) {
            ERR_EXIT("open");
        }
    }
}

void init_server(unsigned short port) {
    // Initialize server
    // Input: port number
    // Result: 
    // 1. server svr
    // 2. requestP is init with length maxfd, and went through init_request
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

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

void run_server(void) {

    fds = (struct pollfd *) malloc(sizeof(struct pollfd) * (maxfd+1)); // TODO: optimized
    nfd = 1; 
    fds[0].fd = svr.listen_fd;
    fds[0].events = POLLIN; // svr.listen_fd should be only read from

    while (1) {
        int timeout = calculate_timeout();
        fprintf(stderr, "Timeout: %d\n", timeout);
        // poll should return either
        // 1. Some fds are ready
        // 2. Some fds are expired and should be cleaned up
        int ready = poll(fds, nfd, timeout);
        if(ready == -1)
            ERR_EXIT("poll");
        for(nfds_t i = 0; i < nfd; i++) {
            int conn_fd = fds[i].fd;
            if(fds[i].revents == POLLIN) {
                if(conn_fd == svr.listen_fd) {
                    int new_fd = accept_conn();
                    if (new_fd < 0)
                        ERR_EXIT("cannot get new connection\n");
                    fds[nfd].fd = new_fd;
                    fds[nfd].events = POLLOUT; // POLLOUT newly added fd
                    nfd++;
                } else {
                    process_client_request(conn_fd);
                    fds[i].events = POLLOUT;
                }
            }
            if(fds[i].revents == POLLOUT) {
                // closing connection
                if(requestP[conn_fd].status == INVALID) {
                    fprintf(stderr, "invalid operation, closing fd %d\n", requestP[conn_fd].conn_fd);
                    write(requestP[conn_fd].conn_fd, invalid_op_msg, strlen(invalid_op_msg));
                    unlock_unpaid_seat(&requestP[conn_fd]);
                    close(requestP[conn_fd].conn_fd);
                    clear_request(&requestP[conn_fd]);
                    fds[i].fd = -1; // TODO3: fd collection
                } else if(requestP[conn_fd].status == EXIT) {
                    fprintf(stderr, "fd: %d closed, bye bye!\n", requestP[conn_fd].conn_fd);
                    write(requestP[conn_fd].conn_fd, exit_msg, strlen(exit_msg));
                    unlock_unpaid_seat(&requestP[conn_fd]);
                    close(requestP[conn_fd].conn_fd);
                    clear_request(&requestP[conn_fd]);
                    fds[i].fd = -1; // TODO3: fd collection
                } else {
                    response_client_request(conn_fd);
                    fds[i].events = POLLIN;
                }
            }
        }

        clean_expired_client();

    }

    free(requestP);
    free(fds);
    close(svr.listen_fd);
    for (int i = 0;i < TRAIN_NUM; i++)
        close(trains[i].file_fd);
}