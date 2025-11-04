/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
/* $begin adder */
#include "../csapp.h"

int main(void)
{
  char *buf, *p;
  char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
  char query_string_copy[MAXLINE]; // <<< 1. 복사본을 담을 버퍼 생성
  int n1 = 0, n2 = 0;

  /* Extract the two arguments */
  if ((buf = getenv("QUERY_STRING")) != NULL)
  {
    strcpy(query_string_copy, buf);  // 원본을 복사본으로 복사
    p = strchr(query_string_copy, '&');
    *p = '\0';
    strcpy(arg1, query_string_copy);
    strcpy(arg2, p + 1);
    n1 = atoi(strchr(arg1, '=') + 1);
    n2 = atoi(strchr(arg2, '=') + 1);
  }

  /* Make the response body */
  sprintf(content, "QUERY_STRING: %s & %s\r\n<p>", arg1, arg2);
  sprintf(content + strlen(content), "Welcome to add.com: ");
  sprintf(content + strlen(content), "THE Internet addition portal.\r\n<p>");
  sprintf(content + strlen(content), "The answer is: %d + %d = %d\r\n<p>",
          n1, n2, n1 + n2);
  sprintf(content + strlen(content), "Thanks for visiting!\r\n");

  /* Generate the HTTP response */
  printf("Content-type: text/html\r\n");
  printf("Content-length: %d\r\n\r\n", (int)strlen(content));
  printf("%s", content);
  fflush(stdout);

  exit(0);
}
/* $end adder */