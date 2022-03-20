//
// Created by qwskyz on 2022/3/19.
//

#ifndef WEBSERVER_HTTP_CONN_H
#define WEBSERVER_HTTP_CONN_H

#include <sys/stat.h>
#include <netinet/in.h>
#include <iostream>
#include <sys/epoll.h>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/uio.h>
#include <cstdarg>
#include "util.h"

class http_conn {
public:
    http_conn() = default;
    ~http_conn() = default;

public:
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    static const int FILE_NAME_LEN = 1000;

    // http method, support GET temporary
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};

    // the state of parsing the client request
    enum CHECK_STATE {CHECK_STATE_REQUEST_LINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};

    /*
     * NO_REQUEST
     * GET_REQUEST
     * BAD_REQUEST
     * NO_RESOURCE
     * FORBIDDEN_REQUEST
     * FILE_REQUEST
     * INTERNAL_ERROR
     * CLOSED_CONNECTION
     */
    enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};

    enum LINE_STATUS {LINE_OK = 0, LINE_BAD, LINE_OPEN};

public:
    static int m_epoll_fd;
    static int m_user_count;

private:
    int m_sock_fd;
    sockaddr_in m_address;

    // read buffer
    char m_read_buf[READ_BUFFER_SIZE];
    // the last be read index in read buffer
    int m_read_index;
    // the index of current checked byte
    int m_checked_index;
    // the current checked line number
    int m_start_line;

    // the state of the main state machine
    CHECK_STATE m_check_state;

    METHOD m_method;
    char* m_url;
    char* m_version;
    char* m_host;
    int m_content_length;
    bool m_linger;

    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_index;
    char m_real_file[FILE_NAME_LEN];
    struct stat m_file_stat;
    // the address of the mmap of user requested file
    char* m_file_address;
    struct iovec m_iv[2];                   // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    int m_iv_count;

public:
    void init(int sock_fd, const sockaddr_in & addr);
    void close_conn();
    // nonblocking read
    bool read();
    // nonblocking write
    bool write();
    // called by work thread, business handle code
    void process();

private:
    // parse http request
    HTTP_CODE process_read();
    HTTP_CODE parse_request_line(char * request);
    HTTP_CODE parse_request_headers(char * request);
    HTTP_CODE parse_request_content(char * request);
    HTTP_CODE do_get_request();
    char* get_line() {return m_read_buf + m_start_line;}
    LINE_STATUS parse_line();

    // create the response
    bool process_write(HTTP_CODE read_ret);
    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type();
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();

    // init the parse member
    void init();
};
#endif //WEBSERVER_HTTP_CONN_H
