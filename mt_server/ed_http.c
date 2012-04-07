
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "common.h"
#include "ed_http.h"

/**
 * @brief parse an incoming http request for a valid http 1.0 query
 */
int process_http_request(char *header, parsed_request_t *http_req)
{
    regex_t reg;
    //char *pattern = "^(GET|HEAD|POST|PUT|DELETE|TRACE|CONNECT) ((http[s]?|ftp):/)?/?(([-#@%;$()~_?+=\\&[:alnum:]]*)((\\.[-#@%;$()~_?+=\\&[:alnum:]]*)*))(:([^/]*))?(.*) HTTP/1\\.(0|1)\r\n$";
    char *pattern = "^(GET) (.*) HTTP/1\\.1\r\n((.*)\r\n)*$";
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
        err_printf("regcomp: Failed. Code: %d, Error: %s\n", errcode, errbuf);
        return FAILURE;
    }

    if((errcode = regexec(&reg, header, 5, pmatch, 0)) != 0)
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

    strncpy(method, (header + pmatch[1].rm_so), method_len);
    strncpy(http_req->page, (header + pmatch[2].rm_so), page_len);

    /* for root, serve index page */
    if(strncmp(http_req->page, "/", 2) == 0)
    {
      strcpy(http_req->page, "/index.html");      
    }

    dbg_printf("header = %s, method = %s, page = %s\n", header, method, http_req->page);


    return SUCCESS;
}

/**
 *writes the response on the given fd
 */
int write_http_response(int fd, struct parsed_request *http_req)
{
  char *type = NULL;
  FILE *fp = NULL;
  ssize_t size;
  char buffer[READ_CHUNK_SIZE];
  char file_path[MAX_LINE];
  int len;
  
  memset(file_path, 0, MAX_LINE);
  strcpy(file_path, WWW_PATH);
  strncat(file_path, http_req->page, MAX_LINE - 5);
  dbg_printf("File found: %s\n", file_path);

  /* serve appropriate dynamic content */
  if(strncmp(http_req->page, "/cgi-bin/", 9) == 0)
  {
    write_http_cgi_response(file_path, fd);
    return SUCCESS;
  }
  else if(strncmp(http_req->page, "/fastcgi-bin/", 13) == 0)
  {
    write_http_fastcgi_response(http_req->page+13, fd);
    return SUCCESS;
  }
    
  /* find file */
  fp = fopen(file_path, "r");
  if(fp == NULL)
  {
    err_printf("requested file not found: %s\n", file_path);
    write_http_response_error(404, fd);
    return FAILURE;
  }

  /* write http header */
  if(fseek(fp, 0, SEEK_END) == -1)
  {
    err_printf("fseek error: %s\n", file_path);
    fclose(fp);
    write_http_response_error(500, fd);
    return FAILURE;
  }

  size = ftell(fp);
  if(size == -1)
  {
    err_printf("ftell error: %s\n", file_path);
    fclose(fp);
    write_http_response_error(500, fd);
    return FAILURE;    
  }
  rewind(fp);

  http_req->response_size = size;
  type = get_file_type(http_req->page);
  write_http_response_header(fd, type, size, 200, "OK");

  dbg_printf("response header written: %s on socket %d\n", file_path, fd);

  /* write response data in chunks */
  while(!feof(fp))
  {
    len = fread(buffer, 1, READ_CHUNK_SIZE, fp);
    if(ferror(fp))
    {
      err_printf("fread failed for %s\n", file_path);
      clearerr(fp);
      fclose(fp);
      write_http_response_error(500, fd);
      return FAILURE;
    }
 
    if(len > 0)
    {
      dbg_printf("buffer: %s\n", buffer);
      write_http_response_data(fd, buffer, len); 
      dbg_printf("output returned for %s\n", file_path);
    }
  }

  if(fclose(fp) == EOF)
  {
    err_printf("fclose failed\n");
    return FAILURE;
  }
  return SUCCESS;
}

/**
 * @brief find mime type for the file 
 */
char *get_file_type(char *name)
{
  char *type = NULL;

  if (strstr(name, ".html"))
    type = "text/html";
  else if (strstr(name, ".jpg"))
    type = "image/jpg";
  else if (strstr(name, ".gif"))
    type = "image/gif"; 
  else if (strstr(name, ".js"))
    type = "text/javascript\n";
  else if (strstr(name, ".css"))
    type = "text/css";
  else
  {
    type = "text/plain";
  }

  return type;
}

/**
 * @brief writes cgi response on a given fd
 *        it is also done in stages
 */
int write_http_cgi_response(char *file_path, int fd)
{
  FILE *fp;
  char buffer[READ_CHUNK_SIZE];
  int n;
  char *command, *query_str, *pos;

  /* extract cgi command and query string */
  pos = strchr(file_path, '?');
  if(pos == NULL)
  {
    command = file_path;
    query_str = "";
  }
  else
  {
    *pos = '\0';
    command = file_path;
    query_str = pos + 1;
  }

  if((fp = fopen(command, "r")))
  {
    fclose(fp);
  }
  else
  {
    err_printf("cgi script %s Not Found\n", command);
    write_http_response_error(404, fd);
    return FAILURE;
  }

  dbg_printf("CGI File found: %s\n", file_path);

  /* run cgi program */
  fp = run_cgi_file(command, query_str);
  if(fp == NULL)
  {
    err_printf("cgi script %s Internal Server Error\n", command);
    write_http_response_error(500, fd);
    return FAILURE;    
  }

  dbg_printf("CGI proc %s is running\n", command);
 
  /* write cgi response in chunks */
  while (!feof(fp))
  {
    n = fread(buffer, 1, READ_CHUNK_SIZE, fp);
    if(ferror(fp))
    {
      err_printf("Broken Pipe: %s\n", command);
      clearerr(fp);
      fclose(fp);
      return FAILURE;
    }
    dbg_printf("cgi output: %d\n", n);
    write_http_response_data(fd, buffer, n);
  }

  fclose(fp);

  return SUCCESS; 
}

/**
 * @brief run a cgi script as a new process and captures its standard output
 */
FILE *run_cgi_file(char *command, char *query_str)
{
  int fds[2];
  char query_str_env[MAX_LINE];
/*
  char *path_env, *perl_path_env;
  char path[MAX_LINE], perl_path[MAX_LINE];
*/
  char *argv[4] = {"/bin/sh", "-c"};
  char *envp[8] = {"SERVER_SOFTWARE=ssuman_web_server", "SERVER_NAME=ssuman_18845.com", "GATEWAY_INTERFACE=CGI/1.0", "SERVER_PROTOCOL=HTTP/1.0", "REQUEST_METHOD=GET"};
  sprintf(query_str_env, "QUERY_STRING=%s", query_str);
  envp[5] = query_str_env;
/*
  path_env = getenv("PATH");
  if(path_env != NULL)
  {
    sprintf(path, "PATH=%s", path_env);
    dbg_printf("path = %s\n", path);
    envp[6] = path;
  }

  perl_path_env = getenv("PERL5LIB");
  if(perl_path_env != NULL)
  {
    sprintf(perl_path, "PERL5LIB=%s", perl_path_env);
    dbg_printf("perl_path = %s\n", perl_path);
    envp[7] = perl_path;
  }
*/
  argv[2] = command;

  pipe(fds);
  if (fork() == 0) {
      close(fds[0]);
      dup2(fds[1], 1);
      close(fds[1]);
      execve(argv[0], argv, envp);
      exit(-1);
  }
  close(fds[1]);
  return fdopen(fds[0], "r");
}

/**
 * @brief write a fastcgi response
 *        contacts a fastcgi server on a socket, sends request
 *        and recieve reponse over that. this response written back
 *        to http client
 */
int write_http_fastcgi_response(char *file_path, int fd)
{
  char buffer[READ_CHUNK_SIZE];
  int n;
  char *command, *query_str, *pos;
  int sock, request_query_len;
  struct sockaddr_in serv_addr;
  char request_query[MAX_LINE];
  
  pos = strchr(file_path, '?');
  if(pos == NULL)
  {
    command = file_path;
    query_str = "";
  }
  else
  {
    *pos = '\0';
    command = file_path;
    query_str = pos + 1;
  }

  memset(request_query, 0, MAX_LINE);

  /** fastcgi query is sent in a special format
   *  script name lenght(3digit long), script name, 
   *              query string length(3 digit long), query string
   */
  sprintf(request_query, "%3d%s%3d%s", (int)strlen(command), command, (int)strlen(query_str), query_str);

  dbg_printf("request_query: %s\n", request_query);

  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    err_printf("could not create socket\n");
    write_http_response_error(500, fd);
    return FAILURE;
  } 

  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = inet_addr(FASTCGI_SERV_ADDR);
  serv_addr.sin_port        = htons(atoi(FASTCGI_SERV_PORT));

  /* connect to fastcgi server */
  if(connect(sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
  {
    err_printf("could not connect to fastcgi server: %s\n", strerror(errno));
    write_http_response_error(500, fd);
    return FAILURE;    
  }

  request_query_len = strlen(request_query);
  if(send(sock, request_query, request_query_len, 0) != request_query_len)
  {
    err_printf("could not send request query: %s\n", request_query);
    write_http_response_error(500, fd);
    return FAILURE;
  }

  while(1)
  {
    n = read(sock, buffer, READ_CHUNK_SIZE);
    if(n > 0)
    {
      dbg_printf("fast cgi output: %d\n", n);
      write_http_response_data(fd, buffer, n); 
    }
    else
    {
      close_socket(sock);
      break;
    }    
  }

  dbg_printf("fastcgi response done\n");
  return SUCCESS;
}

/* reads an discards request headers until CRLF */
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
