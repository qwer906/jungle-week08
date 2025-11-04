#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

    void doit(int fd);
    void read_requesthdrs(rio_t *rp);
    int read_header_until_blank(rio_t *rp, char *raw_header, size_t rawcap, char *host_hdr, size_t hostcap);
    void parse_uri(char *uri, char *host, char *path, char *port, char *host_hdr);
    void Rebuild_request(char *host, char *path, char *port, char *raw_header, char *host_hdr, int serverfd);
    void clienterror(int fd, char *filename, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{ 
  Signal(SIGPIPE, SIG_IGN);
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  if(argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while(1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s %s)\n", hostname, port);
    // 클라이언트 요청 처리
    doit(connfd);
    Close(connfd);
  }
}

void doit(int fd) {
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char host[MAXLINE], path[MAXLINE], port[16] = "80";
  rio_t rio;

  // 1단계 : 라인 요청
  Rio_readinitb(&rio, fd);
  // 파일을 열었는데 한 줄도 없으면 종료
  if (Rio_readlineb(&rio, buf, MAXLINE) <= 0) {
    return;
  }
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  // method는 get만 허용
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not implemented", "Server does not implement this method.");
    return;
  }
  

  // 2단계 : 헤더를 빈 줄 끝까지 읽기
  char raw_header[MAXLINE * 4];   // 헤더 원본
  char host_hdr[MAXLINE];     // 호스트 안전하게 ",,,\r\n"까지 저장
  if (read_header_until_blank(&rio, raw_header, sizeof(raw_header), host_hdr, sizeof(host_hdr)) < 0) {
    return ;
  }

  // 3단계 : parse_uri
  parse_uri(uri, host, path, port, host_hdr);

  // 4단계 : 요청을 재조립하고 원 서버에 전송을 하고 응답을 클라이언트하네 보내기
  // 원 서버의 소켓 열기, 원 서버의 입장에서는 proxy가 클라이언트임
  int serverfd = Open_clientfd(host, port);
  if (serverfd < 0) {
    clienterror(fd, host, "502", "Bad Gateway", "Failed to connect to origin");
    return ;
  }
  // 요청 라인 재작성
  Rebuild_request(host, path, port, raw_header, host_hdr, serverfd);
  // 서버에 보내기
  rio_t srio;
  Rio_readinitb(&srio, serverfd);
  char rbuf[MAXLINE];
  ssize_t rn;
  while ((rn = Rio_readnb(&srio, rbuf, sizeof(rbuf))) > 0) {
      Rio_writen(fd, rbuf, rn);   // 서버에서 읽은 걸 클라이언트로!
  }
  Close(serverfd);
}

/* 요청 헤더를 \r\n(빈칸)까지 읽는다.
   리턴값 :0(성공), -1(EOF, 오류)*/
int read_header_until_blank(rio_t *rp, char *raw_header, size_t rawcap, char *host_hdr, size_t hostcap) {
  // 버퍼
  char line[MAXLINE];

  // 이전에 남아있는 값들 초기화
  if (raw_header){
    raw_header[0] = '\0';
  }
  if (host_hdr) {
    host_hdr[0] = '\0';
  }

  while (1) {
    ssize_t n = Rio_readlineb(rp, line, MAXLINE);
    // 연결 끊김이나 오류
    if (n <= 0) {
      return -1;
    }
    // 끝까지 다 읽음
    if (!strcmp(line, "\r\n")) {
      break;
    }
    // host는 첫번쨰 라인만 저장
    if(!strncasecmp(line, "Host:", 5) && host_hdr && hostcap > 0 && host_hdr[0] == '\0'){
      snprintf(host_hdr, hostcap, "%s", line);
    }
    // 원문 누적
    if(raw_header && rawcap) {
      size_t remain = rawcap - 1 - strlen(raw_header);
      strncat(raw_header, line, remain);
    }
  }
  return 0;
}

void parse_uri(char *uri, char *host, char *path, char *port, char *host_hdr) {
  host[0] = '\0';
  path[0] = '\0';
  port[0] = '\0';
  // path의 기본값은 /
  strcpy(path, "/");

  // 상대경로 ("/index.html")인 경우 : host는 비워두고 path만 채움
  if(uri[0] == '/') {
    strncpy(path, uri, MAXLINE - 1);
    path[MAXLINE - 1] = '\0';
    strcpy(port, "80");

    // ---- 추가 시작 ----
    // host_hdr에서 Host: 헤더를 파싱
    if (host_hdr && host_hdr[0] != '\0') {
        const char *p = host_hdr;
        if (!strncasecmp(p, "Host:", 5)) p += 5;
        while (*p == ' ' || *p == '\t') p++;  // 공백 스킵

        char temp[MAXLINE];
        size_t i = 0;
        while (*p && *p != '\r' && *p != '\n' && i < sizeof(temp) - 1) {
            temp[i++] = *p++;
        }
        temp[i] = '\0';

        // host[:port] 분리
        char *colon = strchr(temp, ':');
        if (colon) {
            *colon = '\0';
            snprintf(host, MAXLINE, "%s", temp);
            snprintf(port, 16, "%s", colon + 1);
            if (port[0] == '\0') strcpy(port, "80");
        } else {
            snprintf(host, MAXLINE, "%s", temp);
        }
    } else {
        // Host 헤더조차 없는 경우
        fprintf(stderr, "[Warning] Host header missing in relative URI request.\n");
        strcpy(host, "localhost"); // fallback (임시)
    }
    // ---- 추가 끝 ----
    fprintf(stderr, "[DBG] host='%s' port='%s' path='%s'\n", host, port, path);
    fflush(stderr);
    return ;
  }
  // http:// 제거
  const char *u = uri;
  if (!strncasecmp(u, "http://", 7)) {
    u += 7;
  }

  //host+port읽고 path 나누기
  // 이거 slash는 포인터를 의미함. 그래서 path가 있으면 slash가 '/'이걸 가지고 안 가지면 NULL임
  const char *slash = strchr(u, '/');
  char hostport[MAXLINE];
  size_t len = 0;
  // path가 아무것도 없으면 (www.example.com or www.example.com:8080)
  if (slash == NULL){
    strcpy(hostport, u);
    // port 존재 유무 확인
    const char *colon = strchr(hostport, ':');
    // port 없으면
    if (colon == NULL) {
      strcpy(host, hostport);
      strcpy(port, "80");
    }
    // port 있으면
    else {
      len = colon - hostport;
      strncpy(host, hostport, len);
      host[len] = '\0';
      if (*(colon + 1) == '\0'){
        strcpy(port, "80");
      }
      else {
        strcpy(port, colon + 1);
      }
    }
  }
  // path 존재
  else {
    // path 먼저 채우기
    len = slash - u;
    strncpy(path, slash, MAXLINE - 1);
    path[MAXLINE - 1] = '\0';
    // hostport 채우기
    strncpy(hostport, u, len);
    hostport[len] = '\0';
    // : 기준으로 나누기
    const char *colon = strchr(hostport, ':');
    // port 존재 안함
    if (colon == NULL) {
      strcpy(host,hostport);
      strcpy(port, "80");
    }
    // port 존재
    else {
      len = colon - hostport;
      strncpy(host, hostport, len);
      host[len]= '\0';
      if (*(colon + 1) == '\0'){
        strcpy(port, "80");
      }
      else {
        strcpy(port, colon + 1);
      }
    }
  }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXLINE];

  /* Build HTTP reaponse body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web Server</em>\r\n", body);

  /* Print the HTTP response (body에 있는 HTTP와 관련된 내용들) */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

/*
 * Rebuild_request - 프록시가 원 서버로 보낼 요청을 재조립합니다.
 * (버퍼 오버플로우 수정됨)
 */
void Rebuild_request(char *host, char *path, char *port, char *raw_header, char *host_hdr, int serverfd) {
  
  // char buf[MAXLINE * 4];  // [💥 문제] 이 버퍼는 너무 작습니다.
  char buf[MAX_OBJECT_SIZE]; // [💡 해결] 버퍼 크기를 넉넉하게 늘립니다.
  
  int n = 0;
  // 필수 헤더 4개 적기
  n += sprintf(buf + n, "GET %s HTTP/1.0\r\n", path);
  
  // 비표준 포트가 아닐 경우 Host 헤더에 포트 번호를 포함하지 않습니다.
  if (!strcmp(port, "80"))
        n += sprintf(buf + n, "Host: %s\r\n", host);
    else
        n += sprintf(buf + n, "Host: %s:%s\r\n", host, port);
  
  n += sprintf(buf + n, "%s", user_agent_hdr);
  n += sprintf(buf + n, "Connection: close\r\n");
  n += sprintf(buf + n, "Proxy-Connection: close\r\n");

  // 원본 헤더를 '\r\n'을 기준으로 쪼개기
  // strtok은 원본(raw_header)을 수정하므로 주의해야 하지만,
  // 이 함수가 끝난 뒤 raw_header를 다시 쓰지 않으므로 여기선 괜찮습니다.
  char *line = strtok(raw_header, "\r\n");
  while (line != NULL) {
    // 필수 헤더 4개(및 Accept-Encoding)는 건너뛰고 나머지 헤더만 추가합니다.
    if(strncasecmp(line, "Host:", 5) && 
       strncasecmp(line, "User-Agent:", 11) && 
       strncasecmp(line, "Connection:", 11) && 
       strncasecmp(line, "Proxy-Connection:", 17) &&
       strncasecmp(line, "Accept-Encoding:", 16)){
      
      // 버퍼에 여유가 있는지 확인하는 것이 더 좋지만, 
      // MAX_OBJECT_SIZE로 설정했기 때문에 웬만한 요청은 오버플로우가 나지 않습니다.
      n += sprintf(buf + n, "%s\r\n", line);
    }
    // 다음 줄로
    line = strtok(NULL, "\r\n");
  }
  
  // 마지막 빈 줄 추가
  n += sprintf(buf + n, "\r\n");
  
  // 완성된 요청 헤더를 원 서버(tiny)로 전송
  Rio_writen(serverfd, buf, n);
}
