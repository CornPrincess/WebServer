//
// Created by qwskyz on 2022/3/19.
//

#ifndef WEBSERVER_URIL_H
#define WEBSERVER_URIL_H
void addFd(int epoll_fd, int fd, bool one_shot);
void removeFd(int epoll_fd, int fd);
void modifyFd(int epoll_fd, int fd, int ev);
void setSockNonBlocking(int fd);
#endif //WEBSERVER_URIL_H
