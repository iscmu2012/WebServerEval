#include <stdio.h>
#include <stdlib.h>

void hello()
{
    printf("HTTP/1.0 200 OK\r\n");
    printf("Content-type: text/html\r\n");
    printf("\r\n");
    printf("Hello world!<br>\r\n");
}
