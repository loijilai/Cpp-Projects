#ifndef COMMON_H
#define COMMON_H

#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>

#define FILE_LEN 50
#define MAX_MSG_LEN 512
#define TRAIN_NUM 5
#define SEAT_NUM 40
#define TRAIN_ID_START 902001
#define TRAIN_ID_END TRAIN_ID_START + (TRAIN_NUM - 1)

// Return value for state transition
#define FAILURE -1
#define SUCCESS 0
#define SEAT_TO_PAYMENT 1
#define PAYMENT_TO_SEAT 1
#define SHIFT_TO_SEAT 1

// Structures
enum SEAT {
    DEFAULT,    // Seat is unknown
    CHOSEN,     // Seat is currently being reserved 
    PAID        // Seat is already paid for
};

typedef struct {
    int file_fd;                    // fd of file
} train_info;

typedef struct {
    int shift_id;               // shift id 902001-902005
    int train_fd;               // train file fds
    int num_of_chosen_seats;    // num of chosen seats
    enum SEAT seat_stat[SEAT_NUM];   // seat status
} record;

enum STATE {
    INIT,       // Init state (before welcome banner)
    SHIFT,      // Shift selection
    SEAT,       // Seat selection
    PAYMENT,       // After payment

    // error states
    INVALID,    // Invalid state
    EXIT        // Exit
};

typedef struct {
    char hostname[512];  // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
    char host[512];             // client's host
    int conn_fd;                // fd to talk with client
    int client_id;              // client's id
    char buf[MAX_MSG_LEN];      // data sent by/to client
    size_t buf_len;             // bytes used by buf
    enum STATE status;          // request status
    record booking_info;        // booking status (only used by write server)
    struct timeval deadline; // connection deadline
} request;



#endif