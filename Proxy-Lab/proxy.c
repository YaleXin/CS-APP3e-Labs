#include <stdio.h>
#include "csapp.h"
#include "sbuf.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define N_THREADS 4
#define SBUF_SIZE 16

void doit(int fd);
void clienterror(int fd, char *cause, char *errnum, 
         char *shortmsg, char *longmsg);
void read_and_send_requesthdrs(rio_t* rp, int server_fd);
void parse_header(char* buf, char* key, char* value);
void get_target_server_info(char* uri, char* target_host, char* target_port);
void get_origin_uri(char* old_uri, char* new_uri);
void *thread_handle(void *vargp);
/* You won't lose style points for including this long line in your code */
static const char* user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

sbuf_t sbuf;

int main(int argc, char** argv) {
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // argv[1] 是代理的工作端口
    listenfd = Open_listenfd(argv[1]);
    
    // 初始化缓冲区
    sbuf_init(&sbuf, SBUF_SIZE);
    // 创建工作线程组
    pthread_t tid;
    for(int i = 0; i < N_THREADS; i++){
        Pthread_create(&tid, NULL, thread_handle, NULL);
    }

    while (1) {
        clientlen = sizeof(clientaddr);
        // 等待客户端发起连接
        connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA*)&clientaddr, clientlen, hostname, MAXLINE,
            port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        // doit(connfd);
        sbuf_insert(&sbuf, connfd);
    }
}

void *thread_handle(void *vargp){
    Pthread_detach(pthread_self());
    while(1){
        int connfd = sbuf_remove(&sbuf);
        doit(connfd);
        Close(connfd);
    }
}

void doit(int cliend_fd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    rio_t client_rio;

    /* Read request line and headers */
    Rio_readinitb(&client_rio, cliend_fd);
    if (!Rio_readlineb(&client_rio, buf, MAXLINE))  //line:netp:doit:readrequest
        return;
    printf("client buf = %s", buf);
    // 解析请求行，转为三个参数
    sscanf(buf, "%s %s %s", method, uri, version);       //line:netp:doit:parserequest
    if (strcasecmp(method, "GET")) {                     //line:netp:doit:beginrequesterr
        clienterror(cliend_fd, method, "501", "Not Implemented",
            "Tiny does not implement this method");
        return;
    }                                                    //line:netp:doit:endrequesterr
    char target_host[100], target_port[6];
    get_target_server_info(uri, target_host, target_port);

    // 和目标服务器建立连接
    int server_fd = Open_clientfd(target_host, target_port);
    printf("server_fd = %d\n", server_fd);

    // 先发送请求行 
    char new_uri[MAXLINE];
    get_origin_uri(uri, new_uri);
    printf("client's target_host : %s, target_port : %s target_uri : %s\n", target_host, target_port, new_uri);
    sprintf(buf, "%s %s %s\n", method, new_uri, "HTTP/1.0");
    Rio_writen(server_fd, buf, strlen(buf));

    rio_t server_rio;
    Rio_readinitb(&server_rio, server_fd);
    // 再发送请求报头 
    read_and_send_requesthdrs(&client_rio, server_fd);
    

    // 读取目标服务器返回的内容
    char rsp_buf[MAXLINE];
    // 由于响应包可能同时有文本和二进制，因此应该用 Rio_readnb
    while(Rio_readnb(&server_rio, rsp_buf, sizeof(rsp_buf))){
        printf("rsp_buf = %s\n", rsp_buf);
        // 发送目标服务返回的响应给客户端
        Rio_writen(cliend_fd, rsp_buf, sizeof(rsp_buf));
    } 
    
    Close(server_fd);
}

// 将包装过的请求uri解析出原始的uri
void get_origin_uri(char* old_uri, char* new_uri) {
    int old_uri_idx = 0;
    // 跳过协议头
    while (old_uri[old_uri_idx] != '/') {
        old_uri_idx++;
    }
    // 跳过两个斜杠
    old_uri_idx += 2;
    // 跳过域名
    while (old_uri[old_uri_idx] != ':') {
        old_uri_idx++;
    }
    // 跳过冒号
    old_uri_idx++;
    // 跳过端口
    while (old_uri[old_uri_idx] >= '0' && old_uri[old_uri_idx] <= '9') {
        old_uri_idx++;
    }
    strcpy(new_uri, old_uri + old_uri_idx);
}

// 将包装过的请求uri解析出host和port
void get_target_server_info(char* uri, char* target_host, char* target_port) {
    int idx = 0;
    while (uri[idx] != ':') {
        idx++;
    }
    // 跳过第一个冒号和协议后面的两个斜杠
    idx += 3;
    int host_idx = 0;
    while (uri[idx] != ':') {
        target_host[host_idx++] = uri[idx++];
    }
    target_host[host_idx] = '\0';
    // 跳过第二个冒号
    idx++;
    int port_idx = 0;
    while (uri[idx] >= '0' && uri[idx] <= '9') {
        target_port[port_idx++] = uri[idx++];
    }
    target_port[port_idx] = '\0';
}

// 解析header中的key和对应的value
void parse_header(char* buf, char* key, char* value) {
    int buf_idx = 0;
    while (buf[buf_idx] != ':') {
        key[buf_idx] = buf[buf_idx];
        buf_idx++;
    }
    // 跳过冒号和空格
    buf_idx++;
    while (buf[buf_idx] == ' ')buf_idx++;
    strcpy(value, buf + buf_idx);
}
void read_and_send_requesthdrs(rio_t* rp, int server_fd) {

    char buf[MAXLINE];
    // char key[MAXLINE], value[MAXLINE];
    // 按行读取，直到遇到 \r\n
    while (strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm
        Rio_readlineb(rp, buf, MAXLINE);
        // 发送内容到目标服务器
        Rio_writen(server_fd, buf, strlen(buf));
        // parse_header(buf, key, value);
        printf("%s", buf);
    }
    Rio_writen(server_fd, (void*)user_agent_hdr, strlen(user_agent_hdr));
    Rio_writen(server_fd, "\r\n", strlen("\r\n"));
    return;
}

void clienterror(int fd, char* cause, char* errnum,
    char* shortmsg, char* longmsg) {
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}