#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <pwd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <fcntl.h>
#include "common.h"

#define OUTFD 0
#define READ_CHUNK_SIZE 1024

/* ---------------------------------------------------------------- */
int main(int argc, char *argv[])
{
  FILE *fp = NULL;
  char *reply = "done";
  char request[MAX_LINE];
  char buffer[READ_CHUNK_SIZE];
  char file_path[MAX_LINE];
  int size, shmid, key, len;
  char *shm, *old_shm;
  dbg_printf("Reader slave started.\n");

  while (1) {
    reply = "done";
    int ret = read(0, request, MAX_LINE);
    if (ret == 0)	{	/* master closed connection */
      err_printf("read slave returned 0.\n");
      exit(0);
    }
    if (ret < 0) {
      err_printf("read slave had negative return value.\n");
      exit(0);
    }
    if (sscanf(request, "%d %d %s", &key, &size, file_path) != 3) {
      err_printf("scanf returned an error\n");
      exit(0);
    }

    dbg_printf("Master request at slave: key: %d, size: %d, file_path: %s\n",
         key, size, file_path);

    fp = fopen(file_path, "r");
    if (fp < 0) {
      reply = "fd failed";
    }
    else {
      if ((shmid = shmget(key, size, IPC_CREAT | 0666)) < 0) {
        err_printf("shmget() error: %s\n", strerror(errno));
        exit(0);
      }
      if ((shm = shmat(shmid, NULL, 0)) == (char *) -1) {
        err_printf("shmat() error: %s\n", strerror(errno));
        exit(0);
      }
      old_shm = shm;
      dbg_printf("key %d, shmid %d, shm %p\n", key, shmid, shm);
      while(!feof(fp)) {
        len = fread(buffer, 1, READ_CHUNK_SIZE, fp);
        if (len < 0) {
          err_printf("read error: %s\n", strerror(errno));
          exit(0);
        }
        memcpy(shm, buffer, len);
//        dbg_printf("shm: %s", shm);
        shm += len;
      }

      // detach shared memory
      if (shmdt(old_shm) == -1) {
        err_printf("shmdt() error: %s\n", strerror(errno));
        exit(0);
      }
    }
    fclose(fp);

    dbg_printf("writing reply to socket: %s\n", reply);
    write(OUTFD, reply, 1+strlen(reply));
  }
  return 0;
}
/* ---------------------------------------------------------------- */
