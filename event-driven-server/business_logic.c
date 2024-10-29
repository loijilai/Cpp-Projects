#include <stdio.h>
#include <fcntl.h>
#include "server.h"
#include "business_logic.h"

// Global variables
train_info trains[TRAIN_NUM];
int local_locked[TRAIN_NUM][SEAT_NUM]; // stores client id

static const char IAC_IP[3] = "\xff\xf4";
static const char* welcome_banner = "======================================\n"
                                    " Welcome to CSIE Train Booking System \n"
                                    "======================================\n";
#ifdef READ_SERVER
static char* read_shift_msg = "Please select the shift you want to check [902001-902005]: ";
#elif defined WRITE_SERVER
static const char* book_succ_msg = ">>> Your train booking is successful.\n";
static const char* no_seat_msg = ">>> No seat to pay.\n";
static const char* seat_booked_msg = ">>> The seat is booked.\n";
static const char* full_msg = ">>> The shift is fully booked.\n";
static const char* cancel_msg = ">>> You cancel the seat.\n";
static const char* lock_msg = ">>> Locked.\n";
static char* write_shift_msg = "Please select the shift you want to book [902001-902005]: ";
static char* write_seat_msg = "Select the seat [1-40] or type \"pay\" to confirm: ";
static char* write_seat_or_exit_msg = "Type \"seat\" to continue or \"exit\" to quit [seat/exit]: ";
#endif


#ifdef READ_SERVER

int check_lock(int fd, int whence, int offset, int len, int type) {
    struct flock lock;
    lock.l_whence = whence; // seek from begining of the file
    lock.l_start = offset;
    lock.l_len = len; // lock two bytes
    lock.l_type = type;
    if(fcntl(fd, F_GETLK, &lock) == -1) {
        fprintf(stderr, "fcntl error\n");
        return -1;
    }

    if(lock.l_type == F_UNLCK)
        return 0; // available
    else
        return 1; // locked
}

static int get_and_check_seat_state(int train_fd, int seat_num) {
    // should lock if available
    if(seat_num < 1 || seat_num > SEAT_NUM) {
        fprintf(stderr, "get_and_check_seat_state invalid seat_num %d\n", seat_num);
        return -1;
    }
    // database
    char buf[1];
    off_t offset = (seat_num-1) * 2;
    int ret = pread(train_fd, buf, 1, offset);
    if(ret < 0) {
        fprintf(stderr, "get_and_check_seat_state pread train_fd: %d, ret %d\n", train_fd, ret);
        return -1;
    }
    int seat_state = buf[0] - '0';
    if(seat_state == 1)
        return 1; // booked by other request
    
    ret = check_lock(train_fd, SEEK_SET, offset, 2, F_WRLCK);
    if(ret == -1)
        return -1;
    else if(ret == 0)
        return 0; // available
    else
        return 2; // locked
}

static int fill_train_info(int filefd, request *rq) {
    // fill train info into the request buffer
    // return 0: Success
    // return -1: Error
    memset(rq->buf, 0, MAX_MSG_LEN);
    int seat_state;
    for(int i = 0; i < SEAT_NUM; i++) {
        if((seat_state = get_and_check_seat_state(filefd, i+1)) < 0)
            return -1;
        rq->buf[i*2] = seat_state + '0'; // to character
        rq->buf[i*2+1] = ' ';
        if((i+1) % 4 == 0)
            rq->buf[i*2+1] = '\n';
    }
    rq->buf_len = SEAT_NUM * 2;
    return 0;
}

#elif defined WRITE_SERVER
static int fill_train_info(request *reqP) {
    // fill train info into the request buffer
    // return 0: Success
    // return -1: Error
    // Example:
    /*
     * Booking info
     * |- Shift ID: 902001
     * |- Chose seat(s): 1,2
     * |- Paid: 3,4
     */
    char chosen_seat[MAX_MSG_LEN];
    char paid_seat[MAX_MSG_LEN];
    int chosen_index = 0;
    int paid_index = 0;

    memset(chosen_seat, 0, SEAT_NUM);
    memset(paid_seat, 0, SEAT_NUM);
    memset(reqP->buf, 0, MAX_MSG_LEN);

    for(int i = 0; i < SEAT_NUM; i++) {
        if(reqP->booking_info.seat_stat[i] == CHOSEN) {
            if(chosen_index > 0) {
                strncat(chosen_seat, ",", sizeof(chosen_seat)-strlen(chosen_seat)-1);
            }
            char temp[3]; // at most 2 digits + '\0
            snprintf(temp, sizeof(temp), "%d", i+1);
            strncat(chosen_seat, temp, sizeof(chosen_seat)-strlen(chosen_seat)-1);
            chosen_index++;
        }
        else if(reqP->booking_info.seat_stat[i] == PAID) {
            if(paid_index > 0) {
                strncat(paid_seat, ",", sizeof(paid_seat)-strlen(paid_seat)-1);
            }
            char temp[3];
            snprintf(temp, sizeof(temp), "%d", i+1);
            strncat(paid_seat, temp, sizeof(paid_seat)-strlen(paid_seat)-1);
            paid_index++;
        }
    }

    reqP->buf_len = snprintf(reqP->buf, MAX_MSG_LEN, "\nBooking info\n"
                        "|- Shift ID: %d\n"
                        "|- Chose seat(s): %s\n"
                        "|- Paid: %s\n\n"
                        ,reqP->booking_info.shift_id, chosen_seat, paid_seat);
    return 0;
}


int lock(int fd, int whence, int offset, int len, int type) {
    struct flock lock;
    lock.l_whence = whence; // seek from begining of the file
    lock.l_start = offset;
    lock.l_len = len; // lock two bytes
    lock.l_type = type;

    if(fcntl(fd, F_SETLK, &lock) == -1) {
        if (errno == EACCES || errno == EAGAIN) {
            fprintf(stderr, "Unable to obtain lock on\n");
            return 1; // locked by other process
        } else {
            fprintf(stderr, "fcntl error\n");
            return -1;
        }
    }
    return 0;
}

static int get_and_lock_seat_state(request *rq, int seat_num) {
    // should lock if available
    record booking_info = rq->booking_info;
    int train_fd = booking_info.train_fd;
    int shift_id = booking_info.shift_id;
    if(seat_num < 1 || seat_num > SEAT_NUM) {
        fprintf(stderr, "get_and_lock_seat_state invalid seat_num %d\n", seat_num);
        return -1;
    }

    // Check database
    char buf[1];
    off_t offset = (seat_num-1) * 2;
    int ret = pread(train_fd, buf, 1, offset);
    if(ret < 0) {
        fprintf(stderr, "get_and_lock_seat_state pread ret %d\n", ret);
        return -1;
    }
    int seat_state = buf[0] - '0';
    if(seat_state == 1)
        return 1; // booked by other request

    // Check local state (other client booked from the same server as me)
    if(local_locked[shift_id - TRAIN_ID_START][seat_num-1] == rq->client_id)
        return 0; // availabe, chosen by myself
    else if(local_locked[shift_id - TRAIN_ID_START][seat_num-1] != 0)
        return 2; // locked by other request
    
    // Check inter-process state (other client booked from different server as me)
    // Note: Directly get lock to prevent race condition
    ret = lock(train_fd, SEEK_SET, offset, 2, F_WRLCK); // lock 2 bytes
    if(ret == -1) {
        return -1;
    }
    else if(ret == 0) {
        local_locked[shift_id - TRAIN_ID_START][seat_num-1] = rq->client_id;
        return 0; // sucessfully locked
    }
    else {
        return 2; // locked by other process
    }
}

// used in payment (state = 1 always)
static int set_seat_state(int train_fd, int seat_num, int state) {
    if(seat_num < 1 || seat_num > SEAT_NUM)
        return -1;
    
    char buf[3]; // state + '\0' (actually 2 is enough)
    // convert integer to string
    snprintf(buf, sizeof(buf), "%d", state);
    off_t offset = (seat_num-1) * 2;
    int ret = pwrite(train_fd, buf, strlen(buf), offset);
    if(ret < 0)
        return -1;
    return 0;
}

static bool fully_booked(int train_fd) {
    int seat_state;
    for(int seat_num = 1; seat_num <= SEAT_NUM; seat_num++) {
        char buf[1];
        off_t offset = (seat_num-1) * 2;
        int ret = pread(train_fd, buf, 1, offset);
        if(ret < 0)
            fprintf(stderr, "pread error\n");
        seat_state = buf[0] - '0';
        if(seat_state == 0) // available
            return false;
    }
    return true;
}

static int response_seat(request *rq) {
    write(rq->conn_fd, rq->buf, rq->buf_len);
    fill_train_info(rq);
    write(rq->conn_fd, rq->buf, rq->buf_len);
    write(rq->conn_fd, write_seat_msg, strlen(write_seat_msg));
    return SUCCESS;

}
static int response_payment(request *rq) {
    write(rq->conn_fd, rq->buf, rq->buf_len);
    fill_train_info(rq);
    write(rq->conn_fd, rq->buf, rq->buf_len);
    write(rq->conn_fd, write_seat_or_exit_msg, strlen(write_seat_or_exit_msg));
    return SUCCESS;
}

static int finish_payment(request *rq) {
    if(strncmp(rq->buf, "seat", 4) == 0) {
        rq->buf_len = 0;
        return PAYMENT_TO_SEAT;
    } else {
        return FAILURE;
    }
}

static int select_seat(request *rq) {
    int ret = 0;
    if(strncmp(rq->buf, "pay", 3) == 0) {
        // Case 1. Failed if chosen seat is empty, print out msg and continue
        if(rq->booking_info.num_of_chosen_seats == 0) {
            rq->buf_len = snprintf(rq->buf, MAX_MSG_LEN, "%s", no_seat_msg);
        }
        // Case 2. Success seat payment updated
        else {
            // update booking record and write to db
            for(int i = 0; i < SEAT_NUM; i++) {
                if(rq->booking_info.seat_stat[i] == CHOSEN) {
                    rq->booking_info.seat_stat[i] = PAID;
                    if((ret = set_seat_state(rq->booking_info.train_fd, i+1, 1)) < 0) {
                        fprintf(stderr, "%s", "Failed to set_seat_state!\n");
                        return FAILURE;
                    }
                    if(unlock(rq->booking_info.train_fd, i*2, 2) < 0)
                        return FAILURE;
                    local_locked[rq->booking_info.shift_id - TRAIN_ID_START][i] = 0;
                }
            }
            rq->booking_info.num_of_chosen_seats = 0;
            rq->buf_len = snprintf(rq->buf, MAX_MSG_LEN, "%s", book_succ_msg);
            return SEAT_TO_PAYMENT;
        }
    } else {
        char* endptr;
        int seat_num = strtol(rq->buf, &endptr, 10);
        if(!seat_num || seat_num < 0 || seat_num > SEAT_NUM || *endptr != '\0') {
            return FAILURE;
        }
        int seat_state = get_and_lock_seat_state(rq, seat_num);
        fprintf(stderr, "seat_num: %d, seat_state: %d\n", seat_num, seat_state);
        if(seat_state == 0) { // seat is available
            if(rq->booking_info.seat_stat[seat_num-1] == DEFAULT) {
                rq->booking_info.seat_stat[seat_num-1] = CHOSEN;
                rq->booking_info.num_of_chosen_seats++;
                rq->buf_len = 0; // no message sent to client in this case
            } else if(rq->booking_info.seat_stat[seat_num-1] == CHOSEN) {
                rq->booking_info.seat_stat[seat_num-1] = DEFAULT;
                rq->booking_info.num_of_chosen_seats--;
                rq->buf_len = snprintf(rq->buf, MAX_MSG_LEN, "%s", cancel_msg);
                if(unlock(rq->booking_info.train_fd, (seat_num-1)*2, 2) < 0)
                    return FAILURE;
                local_locked[rq->booking_info.shift_id - TRAIN_ID_START][seat_num-1] = 0;
            } else {
                return FAILURE;
            }
        }
        // else if(seat is booked)
        else if(seat_state == 1) {
            rq->buf_len = snprintf(rq->buf, MAX_MSG_LEN, "%s", seat_booked_msg);
        }
        // else if (seat is reserved)
        else if(seat_state == 2) {
            rq->buf_len = snprintf(rq->buf, MAX_MSG_LEN, "%s", lock_msg);
        }
        else {
            return FAILURE;
        }
    }
    return SUCCESS;
}

#endif

static int select_shift(request *rq) {
    // Ensure buffer content & length correctness
    // fill_train_info will ensure the buffer content & length correctness
    char *endptr;
    int shift = strtol(rq->buf, &endptr, 10);
    if(!shift || shift < TRAIN_ID_START || shift > TRAIN_ID_END || *endptr != '\0') {
        rq->buf_len = 0;
        return FAILURE;
    }
    #ifdef READ_SERVER      
    if(fill_train_info(trains[shift-TRAIN_ID_START].file_fd, rq) < 0) {
        return FAILURE;
    }
    #elif defined WRITE_SERVER
    int ret = 0;
    if(fully_booked(trains[shift-TRAIN_ID_START].file_fd)) {
        ret = snprintf(rq->buf, MAX_MSG_LEN, "%s", full_msg);
        rq->buf_len = ret;
    } else {
        rq->booking_info.shift_id = shift;
        rq->booking_info.train_fd = trains[shift-TRAIN_ID_START].file_fd;
        if((ret = fill_train_info(rq)) < 0) {
            return FAILURE;
        }
    }
    #endif
    return SUCCESS;
}

static int response_init(request *rq) {
    write(rq->conn_fd, welcome_banner, strlen(welcome_banner));
    #ifdef READ_SERVER
    write(rq->conn_fd, read_shift_msg, strlen(read_shift_msg));
    #elif defined WRITE_SERVER
    write(rq->conn_fd, write_shift_msg, strlen(write_shift_msg));
    #endif
    return SUCCESS;
}

static int response_shift(request *rq) {
    #ifdef READ_SERVER
    write(rq->conn_fd, rq->buf, rq->buf_len);
    write(rq->conn_fd, read_shift_msg, strlen(read_shift_msg));
    #elif defined WRITE_SERVER
    // shift selection failed
    if(rq->booking_info.shift_id == -1) {
        // prompt user to select shift again
        write(rq->conn_fd, rq->buf, strlen(rq->buf));
        write(rq->conn_fd, write_shift_msg, strlen(write_shift_msg));
    } else {
        write(rq->conn_fd, rq->buf, strlen(rq->buf));
        write(rq->conn_fd, write_seat_msg, strlen(write_seat_msg));
        return SHIFT_TO_SEAT;
    }
    #endif
    return SUCCESS;
}

static int handle_read(request* reqP) {
    /*  Return value:
     *      1: read successfully
     *      0: read EOF (client down)
     *     -1: read failed
     *   TODO: handle incomplete input
     */
    int r;
    char buf[MAX_MSG_LEN];
    size_t len;

    memset(buf, 0, sizeof(buf));

    // Read in request from client
    r = read(reqP->conn_fd, buf, sizeof(buf));
    if (r < 0) return -1;
    if (r == 0) return 0;
    char* p1 = strstr(buf, "\015\012"); // \r\n
    if (p1 == NULL) {
        p1 = strstr(buf, "\012");   // \n
        if (p1 == NULL) {
            if (strncmp(buf, IAC_IP, 2) == 0) {
                // Client presses ctrl+C, regard as disconnection
                fprintf(stderr, "Client presses ctrl+C....\n");
                return 0;
            }
        }
    }

    len = p1 - buf + 1; // containing \n
    memmove(reqP->buf, buf, len);
    reqP->buf[len - 1] = '\0'; // replace \n with \0
    reqP->buf_len = len-1;
    return 1;
}

int unlock(int filefd, int start, int len) {
    struct flock lock;
    lock.l_whence = SEEK_SET;
    lock.l_start = start;
    lock.l_len = len;
    lock.l_type = F_UNLCK;
    if(fcntl(filefd, F_SETLK, &lock) == -1) {
        return -1;
    }
    return 0;
}

void unlock_unpaid_seat(request *rq) {
    // unlock the unpaid seat
    for(int i = 0; i < SEAT_NUM; i++) {
        if(rq->booking_info.seat_stat[i] == CHOSEN) {
            unlock(rq->booking_info.train_fd, i*2, 2);
            local_locked[rq->booking_info.shift_id-TRAIN_ID_START][i] = 0;
        }
    }
}

void response_client_request(int conn_fd) {
    fprintf(stderr, "Handle [POLLOUT] for conn_fd: %d\n", conn_fd);
    int ret = 0;
    switch(requestP[conn_fd].status) {
        case INIT:
            ret = response_init(&requestP[conn_fd]);
            requestP[conn_fd].status = SHIFT;
            break;
        case SHIFT:
            ret = response_shift(&requestP[conn_fd]);
            if(ret == SHIFT_TO_SEAT)
                requestP[conn_fd].status = SEAT;
            break;
        #ifdef WRITE_SERVER
        case SEAT:
            ret = response_seat(&requestP[conn_fd]);
            break;
        case PAYMENT:
            ret = response_payment(&requestP[conn_fd]);
            break;
        #endif
        default:
            fprintf(stderr, "Unknown operation state for fd %d\n", conn_fd);
            return;
    }
}

void process_client_request(int conn_fd) {
    // Should determine requestP[conn_fd].status with no ambiguity
    fprintf(stderr, "Handle [POLLIN] for conn_fd: %d\n", conn_fd);
    int ret = handle_read(&requestP[conn_fd]);
    if (ret < 0) {
        fprintf(stderr, "bad request from %s\n", requestP[conn_fd].host);
        requestP[conn_fd].status = INVALID;
        return;
    }
    if (strncmp(requestP[conn_fd].buf, "exit", 4) == 0) {
        requestP[conn_fd].status = EXIT;
        return;
    }

    ret = 0;
    switch(requestP[conn_fd].status) {
        case SHIFT: // State 1. Shift selection
            ret = select_shift(&requestP[conn_fd]);
            if (ret == FAILURE) {
                requestP[conn_fd].status = INVALID;
            }
            break;
        #ifdef WRITE_SERVER
        case SEAT: // State 2. Seat selection
            ret = select_seat(&requestP[conn_fd]);
            if (ret == FAILURE) {
                requestP[conn_fd].status = INVALID;
            } else if(ret == SEAT_TO_PAYMENT) {
                requestP[conn_fd].status = PAYMENT;
            }
            break;
        case PAYMENT: // State 3. After payment
            ret = finish_payment(&requestP[conn_fd]);
            if (ret == FAILURE) {
                requestP[conn_fd].status = INVALID;
            } else if(ret == PAYMENT_TO_SEAT) {
                requestP[conn_fd].status = SEAT;
            }
            break;
        #endif
        default:
            fprintf(stderr, "Unknown operation state for fd %d\n", conn_fd);
            requestP[conn_fd].status = INVALID;
            return;
    }
}
