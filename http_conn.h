#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <sys/epoll.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <sys/uio.h>

class util_timer;

class http_conn {
public:
    static int m_epollfd;
    static int m_user_count;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 2048;
    static const int FILENAME_LEN = 200;
    int m_sockfd;
    util_timer* timer;

    enum METHOD { GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT };

    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };

    enum HTTP_CODE { NO_REQUEST = 0, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTON };

    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

    http_conn() {}
    ~http_conn() {}

    void process();
    void init(int sockfd, const sockaddr_in& addr);
    void close_conn();
    bool read();
    bool write();

    HTTP_CODE process_read();
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    bool process_write(HTTP_CODE ret);

    LINE_STATUS parse_line();

private:
    sockaddr_in m_address;
    char m_read_buffer[READ_BUFFER_SIZE];
    char m_write_buffer[WRITE_BUFFER_SIZE];
    int m_read_idx;
    int m_write_idx;

    int m_checked_idx;
    int m_start_line;
    int m_content_length;

    CHECK_STATE m_check_state;

    char* m_url;
    char* m_version;
    METHOD m_method;
    char* m_host;
    bool m_linger;
    char m_real_file[FILENAME_LEN];
    struct stat m_file_stat;
    char* m_file_address;
    struct iovec m_iv[2];
    int m_iv_count;

    void init();
    char* getline() {
        return m_read_buffer + m_start_line;
    }
    HTTP_CODE do_request();

    void unmap();
    bool add_response(const char* format, ...);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content(const char* content);
    bool add_content_type();
    bool add_content_length(int content_len);
    bool add_linger();
    bool add_blankline();
};

#endif