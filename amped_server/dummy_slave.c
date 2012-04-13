#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <pwd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "common.h"

#define OUTFD 0

/* ---------------------------------------------------------------- */
int main(int argc, char *argv[])
{
  int sendReply = 0;
  char *reply = "done";
  char request[MAX_LINE];
  dbg_printf("Dummy slave started.\n");

  while (1) {
    int ret;
    if (sendReply) {
      dbg_printf("writing reply to socket: %s\n", reply);
      write(OUTFD, reply, 1+strlen(reply));
      dbg_printf("wrote reply to socket: %s\n", reply);
      sendReply = 0;
    }
    sendReply = 1;
    reply = "done";

    ret = read(0, request, MAX_LINE);
    dbg_printf("read request: %s\n", request);
    if (ret == 0)	{	/* master closed connection */
      dbg_printf("read slave returned 0.\n");
      exit(0);
    }
    if (ret < 0) {
      dbg_printf("read slave had negative return value.\n");
      exit(0);
    }
  }
  return 0;
}
/* ---------------------------------------------------------------- */
