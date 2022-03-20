//
// Created by qwskyz on 2022/3/17.
//
#include <iostream>
#include <libgen.h>
#include <cstdlib>
#include <unistd.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <cassert>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include "threadpool.h"
#include "http_conn.h"
#include "util.h"

#define MAX_FD 65535 // max number of  file description
#define MAX_EVENT_NUMBER 10000 // max number of listening events

void addSig(int sig, void (handler)(int));

int main(int argc, char *argv[]) {
    // 1 handle argv
    if (argc <= 1) {
        std::cout << "Please run as: " << basename(argv[0]) << " {port_number}" << std::endl;
        exit(-1);
    }

    int port = atoi(argv[1]);

    // 2 add signal action
    // TODO why need to add signal action
    addSig(SIGPIPE, SIG_IGN);

    // 3 create threadPool
    threadPool<http_conn> *thread_pool = nullptr;
    try {
        thread_pool = new threadPool<http_conn>;
    } catch (...) {
        std::cout << "create threadPoll failed" << std::endl;
        return 1;
    }

    http_conn *users = new http_conn[MAX_FD];

    // 4 create listen socket and set reuse port
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        exit(-1);
    }
    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

    // 5 bind and listen port
    struct sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port);
    int ret;
    ret = bind(listen_fd, (struct sockaddr *) &server_address, sizeof(server_address));
    if (ret == -1) {
        perror("bind");
        exit(-1);
    }
    // TODO why set 5
    ret = listen(listen_fd, 5);
    if (ret == -1) {
        perror("listen");
        exit(-1);
    }

    // 6 create epoll fd and event array
    epoll_event events[MAX_EVENT_NUMBER];
    int epoll_fd = epoll_create(5);
    addFd(epoll_fd, listen_fd, false);
    // core all the http connect fd use the same epoll_fd
    http_conn::m_epoll_fd = epoll_fd;

    while (true) {
        int num = epoll_wait(epoll_fd, events, MAX_EVENT_NUMBER, -1);
        // TODO what is EINTR
        if (num < 0 && errno != EINTR) {
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < num; i++) {
            int sock_fd = events[i].data.fd;
            if (sock_fd == listen_fd) {
                // it means the client has connected
                struct sockaddr_in client_address{};
                socklen_t client_address_length = sizeof(client_address);
                // 7.1 accept the client connect
                int conn_fd = accept(listen_fd, (struct sockaddr *) &client_address, &client_address_length);

                if (conn_fd < 0) {
                    perror("accept");
                    continue;
                }

                // core check the total connected socket fd
                if (http_conn::m_user_count > MAX_FD) {
                    // TODO send a message to client
                    close(conn_fd);
                    continue;
                }
                // TODO what is the range of return value
                // core create a new http_conn
                users[conn_fd].init(conn_fd, client_address);
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 7.2 handle the accept socket
                // core remember to close the fd
                users[sock_fd].close_conn();
            } else if (events[i].events & EPOLLIN) {
                // read all data in one time
                if (users[sock_fd].read()) {
                    // core users is the name of array, and users point to the first element of array
                    thread_pool->append(users + sock_fd);
                } else {
                    users[sock_fd].close_conn();
                }
            } else if (events[i].events & EPOLLOUT) {
                // write all data in one time
                if (!users[sock_fd].write()) {
                    users[sock_fd].close_conn();
                }
            }
        }
    }

    close(epoll_fd);
    close(listen_fd);
    delete[] users;
    delete thread_pool;
    return 0;
}

void addSig(int sig, void (handler)(int)) {
    struct sigaction sa{};
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}
