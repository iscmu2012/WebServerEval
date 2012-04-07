
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include "csapp.h"
#include "sbuf.h"
#include "common.h"
#include "ed_server.h"
#include "ed_epoll_event.h"
#include "ed_http.h"

/* Shared buffer of connection file descriptors */
sbuf_t sbuf;

int main (int argc, char *argv [])
{
  int server_port;
  int server_sockfd;
	int conn_fd;
  int i;
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	pthread_t tid;

  if (argc < 2 || (server_port = atoi(argv[1])) <= 0) {
    err_printf("Usage: %s port\n", argv[0]);
    exit(1);
  }

  set_signal_handlers();

  /* Start HTTP Server  and Listen for incoming requests */
  server_sockfd = start_server_socket(server_port);
  if(server_sockfd < 0)
  {
    err_printf("Could not start server on port %d\n", server_port);
    exit(1);
  }

	/* Initialize shared buffer */
	sbuf_init(&sbuf, SBUFSIZE);

	/* Pre-spawn all worker threads */
	for (i = 0; i < NTHREADS; i++) {
		Pthread_create(&tid, NULL, conn_handler, NULL);
	}

	/* Accept new connections and add to shared buffer */
	while (1) {
		conn_fd = accept(server_sockfd, (SA *) &client_addr, &client_len);
		if (conn_fd < 0) continue;
		sbuf_insert(&sbuf, conn_fd);
	}

  /* close server socket */
  close_socket(server_sockfd);

  return 0;
}

/* detaches the thread and handles a single request at a time */
void *conn_handler(void *vargp)
{
	int conn_fd;	

	Pthread_detach(pthread_self());

	while(1) {
		conn_fd = sbuf_remove(&sbuf);
		handle_request(conn_fd);
		close(conn_fd);
	}
}

/* start handling an incoming request */
void handle_request(int conn_fd)
{
	rio_t rio;
  parsed_request_t http_req;
	char header[MAX_LINE];

	Rio_readinitb(&rio, conn_fd);

	/* Read first header line of incoming request. */
	if (rio_readlineb(&rio, header, MAX_LINE) < 0) {
		dbg_printf("rio_readlineb error\n");
		return;
	}
	dbg_printf("Header: %s", header);

	if (process_http_request(header, &http_req) != SUCCESS) {
    dbg_printf("Illegal page request\n");
		return;
	}

	if (read_request_headers(&rio) != SUCCESS) {
		return;
	}

  write_http_response(conn_fd, &http_req);
}



