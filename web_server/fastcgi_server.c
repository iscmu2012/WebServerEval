#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <dlfcn.h>

#include "common.h"
#include "fastcgi_server.h"

int NUM_APPS;
apps_info_t *apps_info;

int main (int argc, char *argv [])
{

  int server_fd;  
  int i;

  set_signal_handlers();  

  server_fd = start_server_socket(FASTCGI_PORT);
  if(server_fd < 0)
  {
    err_printf("Could not fastcgi server on port %d\n", FASTCGI_PORT);
    exit(1);
  }

  if(read_fastcgi_config(&NUM_APPS, &apps_info) != SUCCESS)
  {
    err_printf("could not read config file\n");
    close_socket(server_fd);
    exit(1);
  }

  for(i=0; i<NUM_APPS; i++)
    dbg_printf("%d--%s\n", apps_info[i].thread_count, apps_info[i].app_name);

  create_app_threads(NUM_APPS, apps_info);

  
  accept_fastcgi_requests(server_fd);
  return 0;
}

/* Read fastcgi config from file */
int read_fastcgi_config(int *NUM_APPS, apps_info_t **apps_info_ptr)
{
  FILE *fp;
  int n, i;
  apps_info_t *apps_info;

  fp = fopen(FASTCGI_CONFIG_FILE, "r");
  if(fp == NULL)
  {
    err_printf("FASTCGI config file %s not found\n", FASTCGI_CONFIG_FILE);
    return FAILURE;
  }

  fscanf(fp, "%d\n", &n);
  if(n > 0)
    *NUM_APPS = n;

  apps_info = (apps_info_t *)malloc(sizeof(apps_info_t) * n);
  if(apps_info == NULL)
  {
    fclose(fp);
    err_printf("malloc failed\n");
    return FAILURE;
  }

  for(i=0; i<n; i++)
  {
    fscanf(fp, "%d-%s\n", &apps_info[i].thread_count, apps_info[i].app_name);
    apps_info[i].threads = malloc(sizeof(pthread_t) * apps_info[i].thread_count);
    if(apps_info[i].threads == NULL)
    {
      fclose(fp);
      err_printf("malloc failed\n");
      return FAILURE;
    }
    pthread_mutex_init(&apps_info[i].requests_mutex, NULL);
    pthread_cond_init(&apps_info[i].requests_cond, NULL);
    apps_info[i].get_index = 0;
    apps_info[i].put_index = 0;
    apps_info[i].is_queue_full = 0;
  }

  *apps_info_ptr = apps_info;

  fclose(fp);
  return SUCCESS;
}       

/* crea thread pool for each plugin as per config */    
int create_app_threads(int NUM_APPS, apps_info_t *apps_info)
{
  int errcode;
  int i, j;
  int *app_index;

  dbg_printf("creating app threads: %d\n", NUM_APPS);

  for(i = 0; i<NUM_APPS; i++)
  {
    for(j=0; j<apps_info[i].thread_count; j++)
    {
      app_index = malloc(sizeof(int));
      if(app_index == NULL)
      {
        err_printf("malloc failed\n");
        exit(1);
      }
      *app_index = i;

      dbg_printf("creating app threads for app %s\n", apps_info[i].app_name);      
      if((errcode = pthread_create(&apps_info[i].threads[j], NULL, run_app_by_thread, (void *)app_index)) != 0)
      {
        err_printf("pthread_create Failed. Could not create thread. code %d, error: %s\n", errcode, strerror(errcode));
        free(app_index);
        continue;
      }
    }

  }

  return SUCCESS;
}

/* run plugin function by each thread assiigned to them
 * thread waits for queue and picks to serve as soon it comes
 */
void *run_app_by_thread(void *vargp)
{
  int app_index, sockfd;
  int errcode;
  plugin_func_t plugin_func;


  app_index = *((int *)vargp);

  dbg_printf("starting app thread for app: %s\n", apps_info[app_index].app_name);

  plugin_func = load_plugin_func(apps_info[app_index].app_name);
  if(plugin_func == NULL)
  {
    err_printf("could not load fastcgi plugin function for %s\n", apps_info[app_index].app_name);
    return NULL;
  }
 
  while(1)
  {
    /* get Lock to access clientfd array, keep short as possible */
    if((errcode = pthread_mutex_lock(&apps_info[app_index].requests_mutex)) != 0)
    {
        /* if could not take lock and it failed */
        err_printf("pthread_mutex_lock: Failed. Could not take lock. "
          "Code: %d, Error: %s\n", errcode, strerror(errcode));
        continue;
    }    

    /* if nothing to serve, block on conditional mutex
     * It'll be signalled whenever a new request is added to Queue
     */
    while(apps_info[app_index].get_index == apps_info[app_index].put_index && 
            apps_info[app_index].is_queue_full == 0)
    {
      if((errcode = pthread_cond_wait(&apps_info[app_index].requests_cond, 
            &apps_info[app_index].requests_mutex)) != 0)
      {
        err_printf("pthread_cond_wait: Failed. "
            "Could not block for the condition. "
            "Code: %d, Error: %s\n", errcode, strerror(errcode));
        continue;
      }
    }

    sockfd = apps_info[app_index].accepted_requests[apps_info[app_index].get_index];

    /* remove chosen socket from Queue */
    if(++apps_info[app_index].get_index == MAX_REQUESTS)
    {
      apps_info[app_index].get_index = 0;
    }

    /* if queue was full, reset it */
    apps_info[app_index].is_queue_full = 0;

    dbg_printf("Removing request: get_index %d, put_index %d\n", 
      apps_info[app_index].get_index, apps_info[app_index].put_index);

    /* unlock mutex */
    if((errcode = pthread_mutex_unlock(&apps_info[app_index].requests_mutex)) != 0)
    {
      /* if could not unlock */
      err_printf("pthread_mutex_unlock: Failed. Could not unlock. "
          "Code: %d, Error: %s\n", errcode, strerror(errcode));
    }

    /* handle this request */ 
    call_plugin_function(sockfd, apps_info[app_index].app_name, plugin_func);
  }
 
  return NULL;
}

/* load plugin func symbol by app name */
plugin_func_t load_plugin_func(char *app_name)
{
  void *handle;
  plugin_func_t plugin_func;
  char *error;
  char libname[MAX_LINE];

   memset(libname, 0, MAX_LINE);
  sprintf(libname, "./lib%s.so", app_name);
  handle = dlopen(libname, RTLD_LAZY);
  if(!handle)
  {
    err_printf("dlopen failed: %s\n", dlerror());
    return NULL;
  }
  dlerror();

  plugin_func = dlsym(handle, app_name);

  if((error = dlerror()) != NULL)
  {
    err_printf("dlsym error: %s\n", error);
    return NULL;
  }

  return plugin_func;
}

/* run plugin function for given query string */
void call_plugin_function(int sockfd, char *app_name, plugin_func_t plugin_func)
{
  char query_len_str[4], query_str[APP_NAME_LEN]; 
  int query_str_len;
  int stdout_bkp;

  memset(query_str, 0, APP_NAME_LEN);
  memset(query_len_str, 0, 4);

  query_str_len = read(sockfd, query_len_str, 3);
  query_str_len = atoi(query_len_str);
  if(query_str_len > 0)
  {
    query_str_len = read(sockfd, query_str, query_str_len);
    dbg_printf("query_str: %s\n", query_str);
  }

  fflush(stdout);
  stdout_bkp = dup(STDOUT_FILENO);
  dup2(sockfd, STDOUT_FILENO);
  (*plugin_func)(query_str);

  /* VERY NECESSARY: flush standard output before duping it back
                     printf does some buffering */
  fflush(stdout);
  dup2(stdout_bkp, STDOUT_FILENO);

  close_socket(sockfd);
  close_socket(stdout_bkp);

  dbg_printf("fastcgi function call done\n");
}

/* accepts new fastcgi request and add it into the queue */
int accept_fastcgi_requests(int server_fd)
{
  int fd, i, error_code;
  socklen_t sin_size;
  struct sockaddr_storage client_addr;
  char app_len_str[4], app_name[APP_NAME_LEN]; 
  int app_name_len;
  int app_index = -1;

  sin_size = sizeof(struct sockaddr_storage);
  while(1)
  {
    fd = accept(server_fd, (struct sockaddr *)&client_addr, &sin_size);
    if(fd < 0)
    {
      err_printf("accept failed: Code: %d, Error: %s\n", errno, strerror(errno));
      continue;
    }
  
    dbg_printf("got new request: %d\n", fd);  
    memset(app_name, 0, APP_NAME_LEN);
    memset(app_len_str, 0, 4);

    app_name_len = read(fd, app_len_str, 3);
    app_name_len = atoi(app_len_str);
    app_name_len = read(fd, app_name, app_name_len);
    dbg_printf("app_name: %s\n", app_name);
    
    for(i=0; i<NUM_APPS;i++)
    {
      if(strcmp(app_name, apps_info[i].app_name) == 0)
      {
        app_index = i;
        break;
      }
    }

    if(app_index != -1)
    {
      if((error_code = pthread_mutex_lock(&apps_info[app_index].requests_mutex)) != 0)
      {
        err_printf("pthread_mutex_lock failed. Error: %s\n", strerror(errno));
        write_http_response_error(500, fd);
        close_socket(fd);
        continue;        
      }

      /* queue already full */
      if(apps_info[app_index].is_queue_full)
      {
        if((error_code = pthread_mutex_unlock(&apps_info[app_index].requests_mutex)) != 0)
        {
          err_printf("pthread_mutex_unlock failed. Error: %s\n", strerror(errno));
        }
        write_http_response_error(500, fd);
        close_socket(fd);
        continue;
      }
      
      apps_info[app_index].accepted_requests[apps_info[app_index].put_index] = fd;

      if(++apps_info[app_index].put_index == MAX_REQUESTS)
      {
        apps_info[app_index].put_index = 0;
      }

      /* if Queue is full */
      if(apps_info[app_index].get_index == apps_info[app_index].put_index)
      {
        apps_info[app_index].is_queue_full = 1;
      }
  
      dbg_printf("Adding request: get_index %d, put_index %d\n", 
          apps_info[app_index].get_index, apps_info[app_index].put_index);

        /*
         * Signal that we have Accepted Client Request to serve
         * It'll awake one of sleeping Thread to server this Request
         * If all threads are busy, it'll be picked whenever a Thread
         * becomes Free.
         */
        if((error_code = pthread_cond_signal(&apps_info[app_index].requests_cond)) != 0)
        {
            /* if could not signal */
            err_printf("pthread_cond_signal: Failed. Could not Signal. "
                "Code: %d, Error: %s\n", error_code, strerror(error_code));
        }

        /* We are done with Request Array. Unlock mutex */
        if((error_code = pthread_mutex_unlock(&apps_info[app_index].requests_mutex)) != 0)
        {
            /* if could not unlock */
            err_printf("pthread_mutex_unlock: Failed. Could not unlock. "
                "Code: %d, Error: %s\n", error_code, strerror(error_code));
        }

    }
    else
    {
      /* script not found */
      dbg_printf("script not found: %s\n", app_name);
      write_http_response_error(404, fd);
      close_socket(fd);
    }
  }

  return SUCCESS;
}  





