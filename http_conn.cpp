//
// Created by qwskyz on 2022/3/19.
//


#include "http_conn.h"

// HTTP response header
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the requested file.\n";

// webserver root directory
const char *doc_root = "/home/qwskyz/Code/WebServer/resource";

int http_conn::m_user_count = 0;
int http_conn::m_epoll_fd = -1;

void http_conn::init(int sock_fd, const sockaddr_in &addr) {
    m_sock_fd = sock_fd;
    m_address = addr;

    // reuse port
    int reuse = 1;
    setsockopt(m_sock_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

    // add sock_fd to epoll
    addFd(m_epoll_fd, sock_fd, true);
    m_user_count++;

    init();
}

void http_conn::init() {
    // initial state is check the first line
    m_check_state = CHECK_STATE_REQUEST_LINE;
    m_checked_index = 0;
    m_start_line = 0;
    m_read_index = 0;
    m_write_index = 0;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;

    m_linger = false;



    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILE_NAME_LEN);
}

bool http_conn::read() {
    if (m_read_index >= READ_BUFFER_SIZE) {
        return false;
    }

    int bytes_read = 0;
    while (true) {
        bytes_read = recv(m_sock_fd, m_read_buf + m_read_index, READ_BUFFER_SIZE - m_read_index, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // it means no data
                break;
            }
            return false;
        } else if (bytes_read == 0) {
            // the client close the connection
            return false;
        }
        m_read_index += bytes_read;
    }
    std::cout << "===== read all data in one time =====" << std::endl;
    std::cout << m_read_buf << std::endl;
    return true;
}

bool http_conn::write() {
    std::cout << "===== write all data in one time =====" << std::endl;
    int temp = 0;
    int bytes_have_sent = 0;
    int bytes_to_sent = m_write_index;

    if (bytes_to_sent == 0) {
        // finish send response message
        modifyFd(m_epoll_fd, m_sock_fd, EPOLLIN);
        init();
        return true;
    }

    while (true) {
        // core readv 分散读， writev可以将多块分散的内存以并写入文件描述符中，即集中写。
        // core http应答，包括一个状态行，多个头部字段，1个空行和文档的内容，其中前3部分内容可能被Web服务器
        // core 放到一块内存中，而文档的内容通常被读入到另外一块单独的内存中（通过read函数或mmap函数）
        temp = writev(m_sock_fd, m_iv, m_iv_count);
        // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
        // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
        if (temp <= -1) {
            if (errno == EAGAIN) {
                modifyFd(m_epoll_fd, m_sock_fd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_sent -= temp;
        bytes_have_sent += temp;
        if (bytes_to_sent <= bytes_have_sent) {
            // send response success
            unmap();
            if (m_linger) {
                // core if keep-alive, http_conn has to init again
                init();
                modifyFd(m_epoll_fd, m_sock_fd, EPOLLIN);
                return true;
            } else {
                modifyFd(m_epoll_fd, m_sock_fd, EPOLLIN);
                return false;
            }
        }
    }

}

void http_conn::close_conn() {
    if (m_sock_fd != -1) {
        removeFd(m_epoll_fd, m_sock_fd);
        m_sock_fd = -1;
        m_user_count--;
    }
}

void http_conn::process() {
    // 1. parse the http request
    std::cout << "====== parse the request ======" << std::endl;
    HTTP_CODE read_ret = process_read();
    // TODO more read_ret need to be handled
    if (read_ret == NO_REQUEST) {
        modifyFd(m_epoll_fd, m_sock_fd, EPOLLIN);
        return;
    }

    // 2. create the response
    std::cout << "====== create the response ======" << std::endl;
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    modifyFd(m_epoll_fd, m_sock_fd, EPOLLOUT);
}

// main state machine, parse the request
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_state = LINE_OK;
    HTTP_CODE http_code = NO_REQUEST;
    char *curr_line = 0;

    while ((m_check_state == CHECK_STATE_CONTENT && line_state == LINE_OK) ||
           (line_state = parse_line()) == LINE_OK) {
        curr_line = get_line();
        m_start_line = m_checked_index;
        std::cout << "get 1 http request line: " << curr_line << std::endl;

        switch (m_check_state) {
            case CHECK_STATE_REQUEST_LINE: {
                http_code = parse_request_line(curr_line);
                // TODO need to think more condition
                if (http_code == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                http_code = parse_request_headers(curr_line);
                if (http_code == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if (http_code == GET_REQUEST) {
                    return do_get_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                http_code = parse_request_content(curr_line);
                if (http_code == GET_REQUEST) {
                    return do_get_request();
                }
                line_state = LINE_OPEN;
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

bool http_conn::process_write(HTTP_CODE read_ret) {
    switch (read_ret) {
        case INTERNAL_ERROR: {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_500_form)) {
                return false;
            }
            break;
        }
        case BAD_REQUEST: {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form)) {
                return false;
            }
            break;
        }
        case NO_RESOURCE: {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_404_form)) {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST: {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form)) {
                return false;
            }
            break;
        }
        case FILE_REQUEST: {
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_index;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        }
        default: {
            return false;
        }
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_index;
    m_iv_count = 1;
    return true;
}

// parse request line, get the http method, target url and http version
http_conn::HTTP_CODE http_conn::parse_request_line(char *request) {
    // GET /index.html HTTP/1.1
    m_url = strpbrk(request, " \t");
    if (!m_url) {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char *method = request;
    // TODO need to support more method
    // GET\0/index.html HTTP/1.1
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else {
        return BAD_REQUEST;
    }

    // /index.html HTTP/1.1
    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    // http://192.168.0.1:8888/index.html
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    // core turn the m_check_state
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_request_headers(char *request) {
    if (request[0] == '\0') {
        if (m_content_length != 0) {
            // TODO turn m_check_state
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // TODO need to support other method
        return GET_REQUEST;
    } else if (strncasecmp(request, "Connection:", 11) == 0) {
        // Connection: keep-alive
        request += 11;
        request += strspn(request, " \t");
        if (strcasecmp(request, "keep-alive") == 0) {
            // core keep-alive flag
            m_linger = true;
        }
    } else if (strncasecmp(request, "Content-Length:", 15) == 0) {
        // Content-Length头部字段
        request += 15;
        request += strspn(request, " \t");
        m_content_length = atol(request);
    } else if (strncasecmp(request, "Host:", 5) == 0) {
        // Host头部字段
        request += 5;
        request += strspn(request, " \t");
        m_host = request;
    } else {
        // TODO support more http header
        std::cout << "oop! unknown header " << request << std::endl;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_request_content(char *request) {
    if (m_read_index >= (m_content_length + m_checked_index)) {
        request[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}


http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for (; m_checked_index < m_read_index; m_checked_index++) {
        temp = m_read_buf[m_checked_index];
        if (temp == '\r') {
            if ((m_checked_index + 1) == m_read_index) {
                return LINE_OPEN;
            } else if (m_read_buf[m_checked_index + 1] == '\n') {
                // assign '\r\n' to '\0\0'
                m_read_buf[m_checked_index++] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if (m_checked_index > 1 && m_read_buf[m_checked_index - 1] == '\r') {
                m_read_buf[m_checked_index - 1] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

http_conn::HTTP_CODE http_conn::do_get_request() {
    // /home/qwskyz/webserver/resources
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILE_NAME_LEN - len - 1);
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }

    if (!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }

    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }

    // open file with read-only mode
    int fd = open(m_real_file, O_RDONLY);
    // TODO why
    m_file_address = (char *) mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

bool http_conn::add_status_line(int status, const char *title) {
    // TODO need to support HTTP/2.0
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_length) {
    add_content_length(content_length);
    add_content_type();
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_length) {
    return add_response("Content-Length: %d\r\n", content_length);
}

bool http_conn::add_linger() {
    return add_response("Connection: %s\r\n", m_linger ? "keep-alive" : "close");
}

bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}

bool http_conn::add_content_type() {
    // TODO need to support more content_type
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_content(const char *content) {
    return add_response("%s", content);
}

bool http_conn::add_response(const char *format, ...) {
    if (m_write_index >= WRITE_BUFFER_SIZE) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_index, WRITE_BUFFER_SIZE - 1 - m_write_index, format, arg_list);
    if (len > WRITE_BUFFER_SIZE - 1 - m_write_index) {
        return false;
    }
    m_write_index += len;
    va_end(arg_list);
    return true;
}

void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}
