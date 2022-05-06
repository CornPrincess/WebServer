//
// Created by qwskyz on 2022/3/19.
//

#include "util.h"
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>

// add the file description to epoll
void addFd(int epoll_fd, int fd, bool one_shot) {
    epoll_event even{};
    even.data.fd = fd;
    even.events = EPOLLIN | EPOLLRDHUP;
    // 只监听一次事件，当监听完这次事件之后，如果还需要继续监听这个socket的话，需要再次把
    // 这个socket加入到EPOLL队列里
    if (one_shot) {
        even.events |= EPOLLONESHOT;
    }
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &even);
    // core epoll ET mode, set fd nonblocking
    setSockNonBlocking(fd);
}

// remove fd from epoll
void removeFd(int epoll_fd, int fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    // core remember to close fd
    close(fd);
}

void modifyFd(int epoll_fd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
}

void setSockNonBlocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
}