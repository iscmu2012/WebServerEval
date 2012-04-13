#ifndef _FASTCGI_SERVER_H_
#define _FASTCGI_SERVER_H_

#define FASTCGI_PORT 12345
#define APP_NAME_LEN 256
#define FASTCGI_CONFIG_FILE "fastcgi.config"
#define MAX_REQUESTS 64

typedef struct apps_info {
  int thread_count;
  char app_name[APP_NAME_LEN];
  pthread_t *threads;
  pthread_mutex_t requests_mutex;
  pthread_cond_t requests_cond;
  int accepted_requests[MAX_REQUESTS];
  int get_index;
  int put_index;
  int is_queue_full;
} apps_info_t;

typedef void (*plugin_func_t)(char *);

int read_fastcgi_config(int *NUM_APPS, apps_info_t **apps_info);
int create_app_threads(int NUM_APPS, apps_info_t *apps_info);
void *run_app_by_thread(void *vargp);
plugin_func_t load_plugin_func(char *app_name);
void call_plugin_function(int sockfd, char *app_name, plugin_func_t plugin_func);
int accept_fastcgi_requests(int server_fd);

#endif /* _FASTCGI_SERVER_H_ */


