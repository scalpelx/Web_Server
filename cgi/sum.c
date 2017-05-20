#include <stdlib.h>
#include "../common.h"

int main() {
    int n1 = 0, n2 = 0;
    char *buf, *p, arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
    if ((buf = getenv("QUERY_STRING")) != NULL) {
        p = strchr(buf, '&');
        strcpy(arg1, buf);
        strcpy(arg2, p + 1);
        n1 = atoi(arg1);
        n2 = atoi(arg2);
    }
    sprintf(content, "QUERY_STRING=%s", buf);
    sprintf(content, "<p>The answer is: %d + %d = %d\r\n<p>", n1, n2, n1 + n2);
    sprintf(content, "%sThanks for visiting!\r\n", content);
    printf("Connection: close\r\n");
    printf("Content-length: %d\r\n", strlen(content));
    printf("Content-type: text/html\r\n\r\n");
    printf("%s", content);
    fflush(stdout);
    return 0;
}

