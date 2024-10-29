#include <stdio.h>
#include "server.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    init_db();
    init_server((unsigned short) atoi(argv[1]));
    fprintf(stderr, "\n[pid: %d] starting on %.80s, port %d, fd %d, maxfd %d...\n", getpid(), svr.hostname, svr.port, svr.listen_fd, maxfd);

    run_server(); // Start the event loop

    return 0;
}