/*
 * generate_text.c Generates random (printable) text for the user.
 * */
#include "csapp.h"
#define MAXGENERATE 65536

int main(void) {
	char content[MAXLINE], *env, error[MAXLINE];
	char msg[MAXGENERATE];	/* Maximum generated message */
	int size, i;

	env = getenv("QUERY_STRING");

	/* Make the response body */
	sprintf(content, "Welcome to the online text generator: \r\n");
	sprintf(content, "%sThis is a text.\r\n<p>", content);

	if (env == NULL) {
		sprintf(error, "Can't scan buffer!<br/>Usage: generate_tex?n=X<br/>\r\n");
		goto error;
	}
	if (sscanf(env, "n=%d\n", &size) != 1) {
		sprintf(error, "Can't scan buffer!<br/>Usage: generate_tex?n=X<br/>\r\n");
		goto error;
	} else {
		if ((size < 0) || (size > (MAXGENERATE - 1))) {
		sprintf(error, "Size must be between %d and %d", 0, MAXGENERATE -1);
		goto error;
		}
	}

	/* Generate text and null terminate it */
	for (i = 0; i < size; i++) {
		msg[i] = (unsigned char) (33  + (i % 93));
	}
	msg[size] = '\0';

	/* Generate the HTTP response */
	printf("Content-length: %u\r\n",
				 (unsigned) (strlen(content) + size));
	printf("Content-type: text/html\r\n\r\n");
	printf("%s", content);
	printf("%s\r\n", msg);
	fflush(stdout);
	return 0;

	error:
	printf("Content-length: %u\r\n",
					(unsigned) (strlen(content)+ strlen(error)));
	printf("Content-type: text/html\r\n\r\n");
	printf("%s", content);
	printf("%s", error);
	fflush(stdout);
	return 1;
}
