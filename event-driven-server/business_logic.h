#ifndef BUSINESS_LOGIC_H
#define BUSINESS_LOGIC_H

#include "common.h"

// Global variables
extern train_info trains[TRAIN_NUM];
extern int local_locked[TRAIN_NUM][SEAT_NUM];

// Interface
int unlock(int filefd, int start, int len);
void unlock_unpaid_seat(request *rq);
void response_client_request(int conn_fd);
void process_client_request(int conn_fd);

#endif