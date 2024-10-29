#ifndef SERVER_H
#define SERVER_H

#include "common.h"
#define ERR_EXIT(a) do { perror(a); exit(1); } while(0)

// Global variables
extern server svr;
extern int maxfd;
extern request* requestP;
extern struct pollfd *fds;

// Interface
void init_db(void);
void init_server(unsigned short port);
void run_server(void);

#endif
