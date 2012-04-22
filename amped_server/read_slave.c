#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/time.h>

volatile int junkTouchingInt;

#define OUTFD 0

#ifdef __FreeBSD__
char dumbBuffer[READBLOCKSIZE];
#endif

/* ---------------------------------------------------------------- */
static char *
Touch(int fd, int startPos, int len)
{
  char *addr;
  int i;

  if (lseek(fd, startPos, SEEK_SET) < 0)
    return("lseek failed");

#ifdef __FreeBSD__
  if (read(fd, dumbBuffer, len) != len)
    return("short read");
  return("done");
#else
  addr = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, startPos);
  if (!addr)
    return("mmap failed");

  for (i = 0; i < len; i+= 1024)
    junkTouchingInt += addr[i];

  if (munmap(addr, len) < 0)
    return("unmap failed");
#endif /* FreeBSD */
  return("done");
}
/* ---------------------------------------------------------------- */
void main(int argc, char *argv[])
{
  char request[1024];
  int sendReply = FALSE;
  char *reply = "done";
  int byteStart;
  int readLen;
  char *filename;
  char *temp;
  int fd;
  fd_set rfdset;
  struct timeval selectTimeout;

  if ((argc > 2) && (chdir(argv[2]) < 0)) {
    fprintf(stderr, "failed to switch to slave dir\n");
    exit(-1);
  }

  FD_ZERO(&rfdset);
  selectTimeout.tv_sec = atoi(argv[1]);
  selectTimeout.tv_usec = 0;

  while (1) {
    int ret;
    if (sendReply) {
      write(OUTFD, reply, 1+strlen(reply));
    }
    sendReply = TRUE;
    reply = "done";

    if (selectTimeout.tv_sec) {
      FD_SET(0, &rfdset);
      if (!select(1, &rfdset, NULL, NULL, &selectTimeout)) {
	/* time limit expired - tell master we're idle */
	ret = write(OUTFD, "idle", 5);
	if (ret != 5) {
	  fprintf(stderr, "read slave couldn't write idle - %d\n", ret);
	  perror("write");
	  exit(0);
	}
	sendReply = FALSE;
	continue;
      }
    }

    ret = read(0, request, sizeof(request));
    if (ret == 0)		/* master closed connection */
      exit(0);
    if (ret < 0) {
      fprintf(stderr, "read slave had negative\n");
      exit(0);
    }

    //TODO use sscanf();

    byteStart = atoi(request);
    temp = request;
    while (*temp != ' ')
      temp++;
    temp++;
    readLen = atoi(temp);
    while (*temp != ' ')
      temp++;
    temp++;
    filename = temp;

    fd = open(filename, O_RDONLY);
    if (fd < 0)
      reply = "fd failed";
    else
      reply = Touch(fd, byteStart, readLen);
    close(fd);

  }
}
/* ---------------------------------------------------------------- */
