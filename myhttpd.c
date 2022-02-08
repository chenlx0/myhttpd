/*
 * @file myhttpd.c
 * @author Chen Lixiang (lixiang3608@outlook.com)
 * @brief 
 * @version 0.1
 * @date 2022-02-01
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_EVENT_NUMBER 1024
#define READ_BUF_SIZE 1024
#define FILE_BUF_SIZE 1024
#define BACKLOG 10
#define SERVER_INFO "Server: myhttpd/0.1\r\n"

/**
 * error handler
 */

void error_die(const char *s) {
    perror(s);
    exit(1);
}


// get mime type by suffix
// return mime type name length
size_t get_mime_type(char *suffix, char *dst, size_t len) {
    // a suffix to mime type name list
    static const struct {
        const char *m_suffix;
        const char *m_type_name;
        size_t len;
    } mime_types[] = {
        {"css", "text/css", 3},
        {"htm", "text/html", 3},
        {"html", "text/html", 4},
        {"jpeg", "image/jpeg", 4},
        {"jpg", "image/jpeg", 3},
        {"js", "application/javascript", 2},
        {"png", "image/png", 3},
        {"txt", "text/plain", 3},
    };

    int type_len = (int) (sizeof(mime_types) / sizeof(mime_types[0]));
    for (int i = 0; i < type_len; i++) {
        if (len == mime_types[i].len) {
            if (strncmp(suffix, mime_types[i].m_suffix, len) == 0) {
                size_t mime_name_len = strlen(mime_types[i].m_type_name);
                memcpy(dst, mime_types[i].m_type_name, mime_name_len+1);
                return mime_name_len;
            }
        }   
    }

    // set default mime_type
    size_t mime_name_len = strlen("application/octet-stream");
    memcpy(dst, "application/octet-stream", mime_name_len);
    return mime_name_len;
}

// set file describtor as non blocked
void set_nonblock(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
}

void add_epoll_fd(int epoll_fd, int fd) {
    struct epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN; // enable mode
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
        error_die("add fd to epoll failed.");
    }
    set_nonblock(fd);
}

// receive one line from socket
// return value greater than 0 for success
int recv_line(int conn_fd, char *buf, int buf_size) {
    int i = 0;
    char c = '\0';
    int n;

    while ((i < buf_size - 1) && (c != '\n')) {
        n = recv(conn_fd, &c, 1, 0);
        if (n > 0) {
            if (c == '\r') {
                n = recv(conn_fd, &c, 1, MSG_PEEK);
                if ((n > 0) && (c == '\n')) {
                    recv(conn_fd, &c, 1, 0);
                } else {
                    c = '\n';
                }
            }
            buf[i] = c;
            i++;
        } else {
            c = '\n';
        }
    }
    buf[i] = '\0';

    return i;
}

void resp_headers(int conn_fd, char *mime, size_t len) {
    char buf[128];
    send(conn_fd, SERVER_INFO, strlen(SERVER_INFO), MSG_NOSIGNAL);
    sprintf(buf, "Content-Type: %s\r\n", mime);
    send(conn_fd, buf, strlen(buf), MSG_NOSIGNAL);
    sprintf(buf, "Content-Length: %ld\r\n", len);
    send(conn_fd, buf, strlen(buf), MSG_NOSIGNAL);
    sprintf(buf, "\r\n");
    send(conn_fd, buf, strlen(buf), MSG_NOSIGNAL);
}

void resp_ok(int conn_fd) {
    const char *ok = "HTTP/1.0 200 OK\r\n";
    send(conn_fd, ok, strlen(ok), MSG_NOSIGNAL);
    resp_headers(conn_fd, "text/html", 13);
}

void resp_not_implemented(int conn_fd) {
    const char *not_implement = "HTTP/1.0 501 Not Implemented\r\n";
    const char *msg = "501 Method Not Implemented.\r\n";
    send(conn_fd, not_implement, strlen(not_implement), MSG_NOSIGNAL);
    resp_headers(conn_fd, "text/html", strlen(msg));
    send(conn_fd, msg, strlen(msg), MSG_NOSIGNAL);
}

void resp_not_found(int conn_fd) {
    const char *not_found = "HTTP/1.0 404 Not Found\r\n";
    const char *msg = "404 File not found.\r\n";
    send(conn_fd, not_found, strlen(not_found), MSG_NOSIGNAL);
    resp_headers(conn_fd, "text/html", strlen(msg));
    send(conn_fd, msg, strlen(msg), MSG_NOSIGNAL);
}

void resp_bad_request(int conn_fd) {
    const char *bad_request = "HTTP/1.0 400 Bad Request\r\n";
    const char *msg = "400 Bad Request.\r\n";
    send(conn_fd, bad_request, strlen(bad_request), MSG_NOSIGNAL);
    resp_headers(conn_fd, "text/html", strlen(msg));
    send(conn_fd, msg, strlen(msg), MSG_NOSIGNAL);
}

void serve_file(int conn_fd, char *path) {
    int f = open(path, O_RDONLY);
    if (f < 0) {
        resp_not_found(conn_fd);
        return;
    }

    char mime[64] = "text/html"; // mime type for no suffix file
    for (int i = strlen(path)-1; i > 0; i--) {
        if (path[i] == '.') {
            get_mime_type(path+i+1, mime, strlen(path+i+1));
            break;
        }
    }

    const char *ok = "HTTP/1.0 200 OK\r\n";
    ssize_t len;
    send(conn_fd, ok, strlen(ok), MSG_NOSIGNAL);
    struct stat f_stat;
    stat(path, &f_stat);
    resp_headers(conn_fd, mime, f_stat.st_size);
    char buf[FILE_BUF_SIZE];
    do {
        len = read(f, buf, FILE_BUF_SIZE);
        send(conn_fd, buf, len, 0);
    } while (len > 0);
    close(f);
}

void handle_request(int conn_fd, char *line) {
    // only support 'GET' method
    if (strncmp(line, "GET ", 4) != 0) {
        resp_not_implemented(conn_fd);
        return;
    }

    // retrive url(file path)
    char url[READ_BUF_SIZE];
    *url = '.';
    int i;
    for (i = 4; i < READ_BUF_SIZE && line[i] != ' ' && line[i] != '?'; i++) {
        url[i-3] = line[i];
    }
    url[i-3] = '\0';
    if (url[strlen(url)-1] == '/') {
        memcpy(url+i-3, "index.html", strlen("index.html")+1);
    }

    serve_file(conn_fd, url);
}

void handle_event(int epoll_fd, int sock_fd, int number, struct epoll_event* events) {
    char buf[READ_BUF_SIZE];
    for (int i = 0; i < number; i++) {
        int event_fd = events[i].data.fd;
        if (event_fd == sock_fd) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int conn_fd = accept(sock_fd, (struct sockaddr*) &client_addr, &addr_len);
            add_epoll_fd(epoll_fd, conn_fd);
        } else if (events[i].events & EPOLLIN) {
            int numchars;
            if ((numchars = recv_line(event_fd, buf, READ_BUF_SIZE)) > 0) {
                handle_request(event_fd, buf);
                // ignore left headers
                while ((numchars > 0) && strcmp("\n", buf)) {
                    numchars = recv_line(event_fd, buf, READ_BUF_SIZE);
                }
            }
            close(event_fd);
        } else {
            printf("unrecognized event!\n");
        }
    }
}

int startup(char* ip, uint16_t port) {
    // initialize sock address
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &addr.sin_addr);

    int sock_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        error_die("create socket failed.");
    }

    if (bind(sock_fd, (struct sockaddr*) &addr, sizeof(addr)) == -1) {
        error_die("bind socket failed.");
    }

    if (listen(sock_fd, BACKLOG) == -1) {
        error_die("failed to listen.");
    }

    struct epoll_event events[MAX_EVENT_NUMBER];
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        error_die("create epoll failed.");
    }
    add_epoll_fd(epoll_fd, sock_fd);

    // wait and handle new epoll event
    for (;;) {
        int ret = epoll_wait(epoll_fd, events, MAX_EVENT_NUMBER, -1);
        handle_event(epoll_fd, sock_fd, ret, events);
    }

    close(sock_fd);
    return 0;
}


int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: ./myhttpd [host] [port]\n");
        exit(0);
    }

    startup(argv[1], (uint16_t) atoi(argv[2]));

    return 0;
}
