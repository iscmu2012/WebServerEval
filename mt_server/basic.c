/** @file basic.c
 *  @brief Basic concurrent server that serves dynamic content.
 *  @author Georges Chamcham (gchamcha)
 *  @date 2/4/2012
 *  @note This code is based on the Tiny Webserver from CSAPP.
 */

#include "csapp.h"
#include "sbuf.h"

/* Number of pre-spawned thread */
#define NTHREADS 16
/* Number of file descriptors accepted */
#define SBUFSIZE 1024

#define SUCCESS 0
#define ERROR   1

/* Boolean values */
#define TRUE    1
#define FALSE   0

// #define DEBUG

#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif

/* Shared buffer of connection file descriptors */
sbuf_t sbuf;

void *conn_handler(void *vargp);
void handle_request(int conn_fd);
int read_request_headers(rio_t *rio);
int parse_request_header(int conn_fd, char *request, char *uri);
void find_file(char *uri, char *file, char *cgi_args, int *is_static);
void serve_static(int conn_fd, char *file_name, int file_size);
int send_response_header(int conn_fd, char *file_name, int file_size);
int send_data(int conn_fd, char *file_name, int file_size);
void get_file_type(char *file_name, char *file_type);
void serve_dynamic(int conn_fd, char *file_name, char *cgi_args);
void send_error_msg(int fd, char *err_code, char *err_msg);

/** @brief Main method of server.
 *  Opens listening ports and initiates synchronized buffer.
 *  Creates a pool of worker threads that will act on descriptors
 *  in the buffer.
 *  Loops and adds new connection descriptors to buffer.
 *  @param argc Number of command line arguments.
 *  @param argv Command line arguments, contains port number.
 *  @return EXIT CODE
 */
int main(int argc, char **argv)
{
	int listen_fd, conn_fd, port, i;
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	pthread_t tid;

	if (argc != 2) {
		printf("Usage: %s <port>\n", argv[0]);
		return -1;
	}

	/* bind listen_fd to port */
	port = atoi(argv[1]);
	listen_fd = Open_listenfd(port);

	/* Initialize shared buffer */
	sbuf_init(&sbuf, SBUFSIZE);

	/* Pre-spawn all worker threads */
	for (i = 0; i < NTHREADS; i++) {
		Pthread_create(&tid, NULL, conn_handler, NULL);
	}

	/* Accept new connections and add to shared buffer */
	while (1) {
		conn_fd = accept(listen_fd, (SA *) &client_addr, &client_len);
		if (conn_fd < 0) continue;
		sbuf_insert(&sbuf, conn_fd);
	}

	return 0;
}

/** @brief Handles each incoming connection.
 *  Detaches the thread from the parent, removes a single connection
 *  descriptor from the shared buffer, and handles its request.
 *  @param vargp NULL pointer.
 *  @return NULL.
 */
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

/** @brief Handles HTTP GET requests.
 *  Reads and parses the request, the uri, and dispatches the appropriate
 *  functions to serve content.
 *  @param conn_fd The connection file descriptor.
 *  @return none.
 */
void handle_request(int conn_fd)
{
	rio_t rio;
	char request[MAXLINE], uri[MAXLINE];
	char file_name[MAXLINE], cgi_args[MAXLINE];
	int is_static;
	struct stat st_buf;

	Rio_readinitb(&rio, conn_fd);

	/* Read first header line of incoming request. */
	if (rio_readlineb(&rio, request, MAXLINE) < 0) {
		dbg_printf("rio_readlineb error\n");
		return;
	}
	dbg_printf("Request: %s", request);

	if (parse_request_header(conn_fd, request, uri) != SUCCESS) {
		return;
	}

	if (read_request_headers(&rio) != SUCCESS) {
		return;
	}

	find_file(uri, file_name, cgi_args, &is_static);

	if (stat(file_name, &st_buf) < 0) {
		dbg_printf("404: File not found: %s\n", file_name);
		send_error_msg(conn_fd, "404", "File not found");
		return;
	}

	/* Handle static content */
	if (is_static) {
		if (!(S_ISREG(st_buf.st_mode)) || !(S_IRUSR & st_buf.st_mode)) {
			dbg_printf("403: Can't read the file: %s\n", file_name);
			send_error_msg(conn_fd, "403", "Can't read the file.");
			return;
		}
		serve_static(conn_fd, file_name, st_buf.st_size);
	}
	/* Handle dynamic content */
	else {
		if (!(S_ISREG(st_buf.st_mode)) || !(S_IXUSR & st_buf.st_mode)) {
			dbg_printf("403: Can't run the CGI program: %s\n", file_name);
			send_error_msg(conn_fd, "403", "Can't run the CGI program.");
	 		return;
		}
		serve_dynamic(conn_fd, file_name, cgi_args);
	}
}


int parse_request_header(int conn_fd, char *request, char *uri)
{
    regex_t reg;
    //char *pattern = "^(GET|HEAD|POST|PUT|DELETE|TRACE|CONNECT) ((http[s]?|ftp):/)?/?(([-#@%;$()~_?+=\\&[:alnum:]]*)((\\.[-#@%;$()~_?+=\\&[:alnum:]]*)*))(:([^/]*))?(.*) HTTP/1\\.(0|1)\r\n$";
    char *pattern = "^(GET) (.*) HTTP/1\\.0\r\n((.*)\r\n)*$";
    char errbuf[MAX_LINE];
    int errcode;


    //int i;
    regmatch_t pmatch[5];
    char method[MAX_LINE];
    //char page[MAX_LINE];
    int method_len, page_len;

    if((errcode = regcomp(&reg, pattern, REG_ICASE|REG_EXTENDED)) != 0)
    {
        regerror(errcode, &reg, errbuf, MAX_LINE);
        printf("regcomp: Failed. Code: %d, Error: %s\n", errcode, errbuf);
        return FAILURE;
    }

    if((errcode = regexec(&reg, request, 5, pmatch, 0)) != 0)
    {
        regfree(&reg);
        return FAILURE;
    }

    /* debugging */
/*
     for(i=0; i<3; i++)
     {
        dbg_printf("i = %d: %d-%d\n", i, pmatch[i].rm_so, pmatch[i].rm_eo);
     }
*/
    regfree(&reg);

    memset(method, 0, MAX_LINE);

    method_len = pmatch[1].rm_eo - pmatch[1].rm_so;
    page_len   = pmatch[2].rm_eo - pmatch[2].rm_so;

    strncpy(method, (request + pmatch[1].rm_so), method_len);
    strncpy(uri, (request + pmatch[2].rm_so), page_len);

    /* for root, serve index page */
    if(strncmp(uri, "/", 2) == 0)
    {
      strcpy(uri, "/index.html");      
    }

    dbg_printf("header = %s, method = %s, page = %s\n", request, method, uri);


    return SUCCESS;
}

#ifdef 0
int parse_request_header_bla(int conn_fd, char *request, char *uri)
{
	char method[MAXLINE], version[MAXLINE];

	/* Parse fields of incoming request. */
	if (sscanf(request, "%s %s %s", method, uri, version) != 3) {
		dbg_printf("Error occured during parsing request\n");
		return ERROR;
	}

	/* Only handles GET methods */
	if (strcasecmp(method, "GET") != 0) {
		dbg_printf("501: The method %s is not handled\n", method);
		send_error_msg(conn_fd, "501", "Not implemented.");
		return ERROR;
	}

	/* Only handles HTTP/1.0 versions */
	/* Not performing this check to be able to test with modern browsers. */
/*	if (strcasecmp(version, "HTTP/1.0") != 0) {
		printf("501: The version %s is not handled\n", version);
		return ERROR;
	}
*/
	return SUCCESS;
}
#endif

/** @brief Reads all the request headers until CRLF.
 *  @param rio The Robust IO buffer.
 *  @return none.
 */
int read_request_headers(rio_t *rio)
{
	char temp_buf[MAXLINE];
	int rc;
	if ((rc = rio_readlineb(rio, temp_buf, MAXLINE)) < 0) {
		dbg_printf("rio_readlineb error\n");
		return rc;
	}

	/* Keep reading header lines until CRLF */
	while(strcmp(temp_buf, "\r\n") != 0) {
		if ((rc = rio_readlineb(rio, temp_buf, MAXLINE)) < 0 ) {
			dbg_printf("rio_readlineb error\n");
			return rc;
		}
	}
	return SUCCESS;
}

void find_file(char *uri, char *file_name, char *cgi_args, int *is_static)
{
	char *idx;

	/* Check if request is for static content */
	if (strstr(uri, "cgi-bin") == NULL) {
		strcpy(cgi_args, "");
		strcpy(file_name, ".");
		strcat(file_name, uri);
		if (uri[strlen(uri) - 1] == '/') {
			strcat(file_name, "index.html");
		}
		*is_static = TRUE;
		return;
	}
	/* Content is dynamic */
	else {
		/* CGI arguments come after the '?' */
		idx = strchr(uri, '?');
		if (idx != NULL) {
			/* Copy CGI args and null terminate original uri. */
			strcpy(cgi_args, idx + 1);
			*idx = '\0';
		}
		/* No CGI arguments */
		else {
			strcpy(cgi_args, "");
		}
		strcpy(file_name, ".");
		strcat(file_name, uri);
		*is_static = FALSE;
		return;
	}	
}

void serve_static(int conn_fd, char *file_name, int file_size)
{
	/* Send header.*/
	if (send_response_header(conn_fd, file_name, file_size) != SUCCESS) {
		return;
	}
	/* Send data. */
	send_data(conn_fd, file_name, file_size); 
}

int send_response_header(int conn_fd, char *file_name, int file_size)
{
/* 
 * Transmit the HTTP response header on the client's connection socket.
 */
	char file_type[MAXLINE], buf[MAXBUF];
	int len;

	get_file_type(file_name, file_type);
	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	sprintf(buf, "%sContent-length: %d\r\n", buf, file_size);
	sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, file_type);
	len = strlen(buf);
	if (rio_writen(conn_fd, buf, len) != len) {
		dbg_printf("rio_writen returned an error\n");
		return ERROR;
	}
	return SUCCESS;
}

int send_data(int conn_fd, char *file_name, int file_size)
{
/*
 * Transmit the requested content the client's connection socket.
 */
	int file_fd;
	char *file_ptr;

	file_fd = open(file_name, O_RDONLY, 0);
	if (file_fd < 0) {
		dbg_printf("open returned an error\n");
		return ERROR;
	}
			
	file_ptr = mmap(0, file_size, PROT_READ, MAP_PRIVATE, file_fd, 0);
	if (file_ptr == (void *) -1) {
		dbg_printf("mmap returned an error\n");
		close(file_fd);
		return ERROR;
	}
 
	close(file_fd);

	if (rio_writen(conn_fd, file_ptr, file_size) != file_size) {
		munmap(file_ptr, file_size);
		return ERROR;
	}
	if (munmap(file_ptr, file_size) < 0) {
		return ERROR;
	}

	return SUCCESS;
}

void get_file_type(char *file_name, char *file_type)
{
	if (strstr(file_name, ".html")) {
		strcpy(file_type, "text/html");
	} else if (strstr(file_name, ".gif")) {
		strcpy(file_type, "image/gif");
	} else if (strstr(file_name, ".jpg")) {
		strcpy(file_type, "image/jpeg");
	} else {
		strcpy(file_type, "text/plain");
	}
}

void serve_dynamic(int conn_fd, char *file_name, char *cgi_args)
{
	char buf[MAXBUF];
	char *argv[] = { NULL };
	int len, rc;

	/* Send the HTTP header response. */
	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	len = strlen(buf);
	if (rio_writen(conn_fd, buf, strlen(buf)) != len) {
		return;
	}

	/* Fork and execute cgi program: Biggest source of overhead. */
	if ((rc = fork()) < 0) {
		dbg_printf("fork error\n");
		return;
	}
	else if (rc == 0) {
		/* No need to set other environment variables. */
		setenv("QUERY_STRING", cgi_args, 1);

		/* Redirect output to client file descriptor */
		if (dup2(conn_fd, STDOUT_FILENO) < 0) {
			dbg_printf("dup2 error\n");
			return;
		}
	
		/* Execute CGI program */
		if (execve(file_name, argv, environ) < 0) {
			dbg_printf("execve error\n");
			return;
		}
	}
	/* Reap child process */
	wait(NULL);
}

/**	@brief Sends an error message back to the client.
 * 	@param fd The connection file descriptor.
 * 	@param err_code The error code.
 * 	@param err_msg The error message.
 * 	@return void.
 */
void send_error_msg(int fd, char *err_code, char *err_msg)
{
	char header[MAXLINE], body[MAXBUF];
	int len, hlen;
	
	/* HTTP body */
	sprintf(body, "<html><title>Error</title>");
	sprintf(body, "%s<p>%s: %s</p><br/></html>", body, err_code, err_msg);

	sprintf(header, "HTTP/1.0 %s %s\r\n", err_code, err_msg);
	sprintf(header, "%sContent-type: text/html\r\n", header);
	len = strlen(body);
	sprintf(header, "%sContent-length: %d\r\n\r\n", header, len);
	hlen = strlen(header);

	if (rio_writen(fd, header, hlen) != hlen) {
		return;
	}
	/* If it fails, the program is going to return anyway */
	rio_writen(fd, body, len); 
}
