#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <assert.h>

#include "common.h"
#include "ed_server.h"
#include "ed_epoll_event.h"
#include "ed_http.h"

ed_epoll_t ed_epoll;

int main (int argc, char *argv [])
{
  int server_port;
  int server_sockfd;
  int fd;
  int i;

  if (argc < 2 || (server_port = atoi(argv[1])) <= 0) {
    err_printf("Usage: %s port\n", argv[0]);
    exit(1);
  }

  set_signal_handlers();
  
  /* initialize epoll object */
  memset(&ed_epoll, 0, sizeof(ed_epoll_t));
  if(ed_epoll_init(&ed_epoll, MAX_ED_EVENTS))
  {
    err_printf("ed_epoll_init: Failed. Code: %d, Error: %s\n", errno, strerror(errno));
    exit(1);
  }  

  /* Generate helper processes */
  for (i = 0; i < N_HELPER_PROCS; i++) {
    fd = create_generic_slave("reader_slave");
    ed_epoll.helper_info[i].hi_fd = fd;
    ed_epoll.helper_info[i].hi_client = NULL;
    ed_epoll_add(&ed_epoll, fd, ed_reader_callback, NULL);
  }

  /* Start HTTP Server  and Listen for incoming requests */
  server_sockfd = start_server_socket(server_port);
  if(server_sockfd < 0)
  {
    err_printf("Could not start server on port %d\n", server_port);
    exit(1);
  }

  /* Set the socket to non-blocking */
  if(setnonblock(server_sockfd) < 0)
  {
    close_socket(server_sockfd);
    err_printf("failed to set server socket %d to non-blocking\n", server_sockfd);
    exit(1);
  }

  /* accept requests */
  ed_epoll_add(&ed_epoll, server_sockfd, ed_accept_callback, NULL);
  handle_epoll_events();

  /* close server socket */
  close_socket(server_sockfd);

  return 0;
}

/* start handling epoll events */
void handle_epoll_events()
{
  while(1) 
  {
    ed_epoll_dispatch_events(&ed_epoll, EVENT_WAIT_TIMEOUT);

    /* do any cleanup */
  }
}

/* callback for events on listening server socket */
int ed_accept_callback(struct epoll_event *event, void *data)
{
  int new_fd;
  struct sockaddr_in client_addr;
  socklen_t len;
  char client_ip4[INET_ADDRSTRLEN];

  dbg_printf("Got new request\n");

  len = sizeof(struct sockaddr_in);
  
  /* there could be more than one incoming connections */
  while(1)
  {
    new_fd = -1;
    new_fd = accept(event->data.fd, (struct sockaddr *) &client_addr, &len);
    
    if(new_fd == -1)
    {
      if(errno == EAGAIN || errno == EWOULDBLOCK)
      {
        /* we have processed all requests */
        break;
      }
      else
      {
        err_printf("accept failed for fd %d, code: %d, Error: %s\n", event->data.fd, errno,  strerror(errno));
        break;
      }
    }
    
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip4, INET_ADDRSTRLEN);
    //dbg_printf("got request. new socket %d from: %s\n", new_fd, client_ip4);
    dbg_printf("got request from: %s:%d, assign socket is:%d\n", client_ip4, ntohs(client_addr.sin_port), new_fd);
    /* Set client socket to non-blocking */
    if(setnonblock(new_fd) < 0)
    {
      close_socket(new_fd);
      err_printf("failed to set client socket %d to non-blocking\n", new_fd);
      break;
    }

    ed_epoll_add(&ed_epoll, new_fd, ed_socket_callback, NULL);
  }

  return SUCCESS;
}

/* handle events on http request sockets */
int ed_socket_callback(struct epoll_event *event, void *data)
{
  //char buf[BUF_SIZE_PER_READ];
  int read_len, fd, n;
    
//  dbg_printf("received event for socket %d\n", event->data.fd);
  //memset(buf, 0, BUF_SIZE_PER_READ);
  fd = event->data.fd;
 
  if(event->events & EPOLLERR)
  {
    dbg_printf("Event:EPOLLERR %d on socket %d\n", event->events, fd);
  }
  else if(event->events & EPOLLHUP)
  {
      dbg_printf("Event:EPOLLHUP %d on socket %d\n", event->events, fd);
      ed_epoll_del(&ed_epoll, fd);
      close_socket(fd);
  }
  else if(event->events & EPOLLRDHUP)
  {
      dbg_printf("Event:EPOLLRDHUP %d on socket %d\n", event->events, fd);
      ed_epoll_del(&ed_epoll, fd);
      close_socket(fd);
  }
  else if(event->events & EPOLLOUT)
  {
      if(data)
      {
//        dbg_printf("we have something to write on socket %d\n", fd);
        write_http_response(&ed_epoll, fd, data);
        if(ed_epoll.ed_clients[fd].http_req.status == STATUS_REQUEST_FINISH)
        {  
          ed_epoll_del(&ed_epoll, fd);
          close_socket(fd);
        }
      }
      else
      {
        dbg_printf("we have nothing to write on socket %d\n", fd);
        ed_epoll_set(&ed_epoll, fd, EPOLLIN, NULL);
      }
  }  
  else if(event->events & EPOLLIN)
  {
    struct ed_client *client = &ed_epoll.ed_clients[fd];
    int max_read;
    
    dbg_printf("data available to read on %d\n", fd);

    read_len = 0;
    n = 0;    
    //do
    {
      max_read = ((client->buf_len + BUF_SIZE_PER_READ) <= REQUEST_SIZE) ? BUF_SIZE_PER_READ : (REQUEST_SIZE - client->buf_len);  
      n = read(fd, (client->buffer + client->buf_len), max_read);
      if(n > 0)
      {
        read_len += n;
        client->buf_len += n;
      }
    } //while(n == BUF_SIZE_PER_READ);
  
    dbg_printf("%d bytes(n = %d) read on socket %d\n", read_len, n, fd);
   
    if(n == -1)
    {
      err_printf("read on socket %d failed. Code: %d, Error %s\n", fd, errno, strerror(errno));
      if(errno == EWOULDBLOCK)
      {
        err_printf("EWOULDBLOCK\n");
        if(read_len == 0)
          err_printf("no data to read then why I got read event\n");        
      }
    }
    else
    {
      /* start http state machine */
      dbg_printf("Echo: %s\n", client->buffer);
      if(process_http_request(client->buffer, &client->http_req) == SUCCESS)
      {
        if(client->http_req.page != NULL)
        {
          /* page requested */
          dbg_printf("page requested: %s on socket %d\n", client->http_req.page, fd);
          ed_epoll_set(&ed_epoll, fd, EPOLLOUT, client->http_req.page);
        }
      }
      else if(client->buf_len > 0 && client->buffer[client->buf_len-1] == '\n')
      {
        err_printf("illegal page request on socket %d\n", fd);
        ed_epoll_del(&ed_epoll, fd);
        close_socket(fd);        
      }
    }    
  }
  else
  {
    dbg_printf("Event %d on socket %d\n", event->events, fd);
  }

  return SUCCESS;
}

int ed_reader_callback(struct epoll_event *event, void *data)
{
  int helper_fd, client_fd, n, max_read, read_len, size, helper_idx;
  ed_client_t *req_client, *helper_client;
  char request[MAX_LINE];

  helper_fd = event->data.fd;
  helper_client = &(ed_epoll.ed_clients[helper_fd]);
  helper_idx = hi_get_helper_index(&ed_epoll, helper_fd);

  // Find the helper process associated with this FD.
  // --> helper_info[fd] -> fd <-> client mapping
  // Find the client request associated with this helper process
  // Now you act on the status of this client request.
  // Ignore the first EPOLLOUT: by checking the status of
  // the corresponding client request or if client is null.
  //
  // EPOLLOUT with data: where the data is the request to be handled
  // by the helper (unless dealing with killing idle processes)
  // - send all data in one try.
  // - change status to be waiting on response
  //
  // EPOLLIN while waiting for response:
  // read as much as possible from the buffer
  // when all response is read, change status to move to next step of
  // state machine. Free helper to be used by someone else.
    
  if(event->events & EPOLLERR)
  {
    dbg_printf("Event:EPOLLERR %d on socket %d\n", event->events, helper_fd);
  }
  else if(event->events & EPOLLHUP)
  {
    err_printf("Event:EPOLLHUP %d on socket %d\n", event->events, helper_fd);
    ed_epoll_del(&ed_epoll, helper_fd);
    close_socket(helper_fd);
    assert(0);
    if (ed_epoll.helper_info[helper_idx].hi_client != NULL) {
      client_fd = ed_epoll.helper_info[helper_idx].hi_client->fd;
      ed_epoll_del(&ed_epoll, client_fd);
      close_socket(client_fd);
    }
  }
  else if(event->events & EPOLLRDHUP)
  {
    err_printf("Event:EPOLLRDHUP %d on socket %d\n", event->events, helper_fd);
    ed_epoll_del(&ed_epoll, helper_fd);
    close_socket(helper_fd);
    assert(0);
    if (ed_epoll.helper_info[helper_idx].hi_client != NULL) {
      client_fd = ed_epoll.helper_info[helper_idx].hi_client->fd;
      ed_epoll_del(&ed_epoll, client_fd);
      close_socket(client_fd);
    }
  }
  else if(event->events & EPOLLOUT)
  {
      // We know what FD caused the event
      // We can get the appropriate helper info by searching
      // the array for this FD.

      // This call should not fail.
      req_client = ed_epoll.helper_info[helper_idx].hi_client;

      if((data != NULL) && (req_client != NULL))
      {
        client_fd = req_client->fd;

        if(req_client->http_req.status != STATUS_HEADER_WRITTEN) {
          ed_epoll_set(&ed_epoll, helper_fd, EPOLLIN, NULL);
          return SUCCESS;
        }

        dbg_printf("we have something to write on socket %d\n", helper_fd);

        size = req_client->http_req.response_size; 
        sprintf(request, "%d %d %s", req_client->key, size, (char *) data);
        dbg_printf("Sent request to helper: %s\n", request);
        
        n = write(helper_fd, request, strlen(request) + 1);
        if(n != (strlen(request) + 1)) {
       	  err_printf("not everything was written out to %d\n", helper_fd);
          assert(0);
          // TODO error: close helper socket, kill process, and set client
          // status to be STATUS_REQUEST_FINISH.
        }
        req_client->http_req.status = STATUS_MAP_REQUESTED;
        dbg_printf("we have finished writing on socket %d\n", helper_fd);
      }
      else
      {
  //      dbg_printf("we have nothing to write on socket %d\n", helper_fd);
        // ed_epoll_set(&ed_epoll, fd, EPOLLIN, NULL);
      }
  }  
  else if(event->events & EPOLLIN)
  {
    dbg_printf("data available to be read on %d\n", helper_fd);

    req_client = ed_epoll.helper_info[helper_idx].hi_client;
    client_fd = req_client->fd;

    if(req_client->http_req.status != STATUS_MAP_REQUESTED) {
      dbg_printf("why is there data to be read but process is not waiting?\n");
      assert(0);
    }

    // Read response from helper which should be: "done"
    read_len = 0;
    n = 0;
    max_read = MAX_LINE;

    // Read IPC response into helper_client->buffer

    n = read(helper_fd, (req_client->buffer + req_client->buf_len), max_read);
    dbg_printf("%d bytes(n = %d) read on socket %d\n", read_len, n, helper_fd);

    if(n > 0)
    {
      read_len += n;
      req_client->buf_len += n;
    }
    else if (n == 0)
    {
      return SUCCESS;
    }
    else if(n < 0)
    {
      err_printf("read on socket %d failed. Code: %d, Error %s\n", helper_fd, errno, strerror(errno));
      if(errno == EWOULDBLOCK)
      {
        err_printf("EWOULDBLOCK\n");
        if(read_len == 0)
          err_printf("no data to read then why did I get a read event\n");        
      }
    }
    else
    {
      dbg_printf("read returned %s\n", req_client->buffer);
      // Note: Response should be "done" (or part of "done", like "don")
      // or maybe an error? fuck if i know
      write_http_response(&ed_epoll, req_client->fd, req_client->http_req.page);
    }    
  }
  else
  {
    dbg_printf("Event %d on socket %d\n", event->events, helper_fd);
  }

  return SUCCESS;
}
