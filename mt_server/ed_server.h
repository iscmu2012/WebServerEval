#ifndef _SERVER_H_
#define _SERVER_H_

#include "ed_epoll_event.h"

#define EVENT_WAIT_TIMEOUT 1000
#define BUF_SIZE_PER_READ 1024
#define NTHREADS 64
#define SBUFSIZE 65535

void *conn_handler(void *vargp);
void handle_request(int conn_fd);

#endif /* _SERVER_H_ */
