/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}

void doit(int fd) {
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* 클라이언트의 요청 라인 읽기. */
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  /* GET이 아닌 요청을 보낼 시 전송할 에러 메세지와 번호 */
  if(strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this mathod");
    return ;
  }
  /* 요청 헤더를 읽고 무시. 그 이유는 헤더를 읽어야 다음의 데이터를 처리할 수 있기 때문에 또 헤더의 대부분의 내용은 무시해도 되는 내용이라 */
  read_requesthdrs(&rio);

  /* 정적 콘텐츠인지, 동적 콘텐츠인지 판단 */
  is_static = parse_uri(uri, filename, cgiargs);
  /* filename 오류 */
  if (stat(filename, &sbuf) < 0 ) {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return ;
  }

  /* 정적 콘텐츠 */
  if (is_static) {
    /* 보통 파일인지 그리고 owner가 파일을 읽을 수 있는지 검사*/
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return ;
    }
    serve_static(fd, filename, sbuf.st_size);
  }
  /* 동적 콘텐츠 */
  else {
    /* 이 파일이 보통 파일인지 그리고 owner가 파일을 실행을 할 수 있는지*/
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return ;
    }
    serve_dynamic(fd, filename, cgiargs);
  }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXLINE];

  /* Build HTTP reaponse body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web Server<\em>\r\n", body);

  /* Print the HTTP response (body에 있는 HTTP와 관련된 내용들) */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return ;
}

int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;

  /* cgi-bin이 없으면 정적 콘텐츠*/
  if (!strstr(uri, "cgi-bin")) {
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if (uri[strlen(uri) - 1] == '/') {
      strcat(filename, "home.html");
    }
    return 1;
  }
  /* cgi-bin이 있으면 동적 콘텐츠*/
  else {
    ptr = strchr(uri, '?');
    /* cgiarg가 있는 경우*/
    if (ptr) {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';
    }
    /* cgiargs가 없는 경우*/
    else {
      strcpy(cgiargs, "");
    }
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

void serve_static(int fd, char *filename, int filesize) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXLINE];

  /* 파일 타입 결정하기 */
  get_filetype(filename, filetype);
  /* HTTP 응답 전달하기 */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%scontent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);

  srcfd = Open(filename, O_RDONLY, 0);
  /* 파일 크기 만큼 메모리 동적 할당 */
  srcp = (char *)malloc(filesize);
  if (srcp == NULL) {
    Close(srcfd);
    return ;
  }
  /* 디스크 파일에서 파일 내용 읽어오기. srcp로 파일의 내용을 복사하기.*/
  rio_readn(srcfd, srcp, filesize);
  Close(srcfd);
  /* 파일 내용을 클라이언트로 전달 */
  rio_writen(fd, srcp, filesize);
  /* 동적 메모리 할당 해제 */
  free(srcp);
}

void get_filetype(char *filename, char *filetype) {
  if (strstr(filename, ".html")) {
    strcpy(filetype, "text/html");
  }
  else if (strstr(filename, ".gif")) {
    strcpy(filetype, "image/gif");
  }
  else if (strstr(filename, ".png")) {
    strcpy(filetype, "image/png");
  }
  else if (strstr(filename, ".jpg")) {
    strcpy(filetype, "image/jpeg");
  }
  else if (strstr(filename, ".mp4")) {
    strcpy(filetype, "video/mp4");
  }
  else {
    strcpy(filetype, "text/plain");
  }
}

void serve_dynamic(int fd, char *filename, char *cgiargs) {
  char buf[MAXLINE], *emptylist[] = { NULL };

  /* HTTP 응답 헤더 전송 */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  /* 자식 프로세스 생성 */
  if (Fork() == 0) {
    /* 환경 변수를 설정 */
    setenv("QUERY_STRING", cgiargs, 1);
    /* 표준 출력을 클라이언트 소켓으로 리다이렉트*/
    dup2(fd, STDOUT_FILENO);
    /* CGI 프로그램 실행 */
    Execve(filename, emptylist, environ);
  }
  /* 부모 자식의 종료를 기다림. */
  Wait(NULL);
}