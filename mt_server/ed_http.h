#ifndef _ED_HTTP_H_
#define _ED_HTTP_H_

#define WWW_PATH "www"
#define FASTCGI_SERV_ADDR "settlers.lab.ece.cmu.local"
#define FASTCGI_SERV_PORT "12345"

#include "ed_epoll_event.h"
#include "csapp.h"

int process_http_request(char *header, parsed_request_t *http_req);
int write_http_response(int fd, struct parsed_request *http_req);
char *get_file_type(char *file_name);
int write_http_cgi_response(char *file_path, int fd);
FILE *run_cgi_file(char *file_path, char *query_str);
int write_http_fastcgi_response(char *file_path, int fd);
int read_request_headers(rio_t *rio);


#endif /* _ED_HTTP_H_ */
