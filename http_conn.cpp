class util_timer;

#include "http_conn.h"

const char* doc_root = "/home/chaoyi/webserver/resources";

const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad  syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

void setnonblocking(int sockfd) {
    int flag = fcntl(sockfd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(sockfd, F_SETFL, flag);
}

void addfd(int epollfd, int fd, bool oneshot, bool et) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if(oneshot) {
        event.events |= EPOLLONESHOT;
    }
    if(et) {
        event.events |= EPOLLET;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
}

void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

void http_conn::close_conn() {
    if(m_sockfd != -1) {
        printf("close fd %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

void http_conn::init(int sockfd, const sockaddr_in& addr) {
    m_sockfd = sockfd;
    m_address = addr;

    int reuse = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    addfd(m_epollfd, m_sockfd, true, true);
    m_user_count++;
    init();
}

void http_conn::init() {
    m_read_idx = 0;
    m_checked_idx = 0;
    m_start_line = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_url = 0;
    m_version = 0; 
    m_method = GET;
    m_host = 0;
    m_linger = false;
    m_write_idx = 0;

    bzero(m_read_buffer, READ_BUFFER_SIZE);
    bzero(m_write_buffer, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}

bool http_conn::read() {
    if(m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }

    int readbytes;
    while(true) {
        readbytes = recv(m_sockfd, m_read_buffer + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if(readbytes == -1) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return false;
        }else if(readbytes == 0) {
            return false;
        }
        m_read_idx += readbytes;
    }
    return true;
}

http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char* text = 0;
    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK)) {
        
        text = getline();
        m_start_line = m_checked_idx;
        //printf("get 1 http line: %s\n", text);

        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }

            case CHECK_STATE_HEADER: {
                ret = parse_headers(text);
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }else if(ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }

            case CHECK_STATE_CONTENT: {
                ret = parse_content(text);
                if(ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
                
            default: {
                return INTERNAL_ERROR;
            } 
        }
    }
    return NO_REQUEST;;
}

http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for(; m_checked_idx < m_read_idx; m_checked_idx++) {
        temp = m_read_buffer[m_checked_idx];
        if(temp == '\r') {
            if(m_checked_idx + 1 == m_read_idx) {
                return LINE_OPEN;
            }else if(m_read_buffer[m_checked_idx + 1] == '\n') {
                m_read_buffer[m_checked_idx++] = '\0';
                m_read_buffer[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }else if(temp == '\n') {
            if(m_checked_idx > 1 && m_read_buffer[m_checked_idx - 1] == '\r') {
                m_read_buffer[m_checked_idx - 1] = '\0';
                m_read_buffer[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    m_url = strpbrk(text, " \t");

    if(!m_url) {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';

    char* method = text;
    if(strcasecmp(method, "GET") == 0) {
        m_method = GET;
    }else {
        return BAD_REQUEST;
    }

    char* m_version = strpbrk(m_url, " \t");
    if(!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';

    if(strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    if(strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if(!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char* text) {
    if(text[0] == '\0') {
        if(m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }else if(strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    }else if(strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }else if(strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atoi(text);
    }

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char* text) {
    if(m_checked_idx + m_content_length <= m_read_idx) {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    if(stat(m_real_file, &m_file_stat) == -1) {
        return NO_RESOURCE;
    }

    if(!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }

    if(S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }

    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void http_conn::unmap() {
    if(m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool http_conn::write() {
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = 0;
    for(int i = 0; i < m_iv_count; i++) 
        bytes_to_send += m_iv[i].iov_len;

    if(bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while(1) {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if(temp == -1) {
            if(errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_have_send += temp;
        if(bytes_have_send >= bytes_to_send) {
            unmap();
            if(m_linger) {
                init();
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return true;
            }else {
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return false;
            }
        }
    }
}

bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)) {
                return false;
            }
            break;
        
        case BAD_REQUEST:
            add_status_line(400, error_400_form);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form)) {
                return false;
            }
            break;

        case NO_RESOURCE:
            add_status_line(404, error_404_form);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form)) {
                return false;
            }
            break;
        
        case FORBIDDEN_REQUEST:
            add_status_line(403, error_403_form);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form)) {
                return false;
            }
            break;

        case FILE_REQUEST:
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buffer;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;

        default:
            return false;
    }

    m_iv[0].iov_base = m_write_buffer;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

bool http_conn::add_response(const char* format, ...) {
    if(m_write_idx >= WRITE_BUFFER_SIZE) 
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buffer + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if(len >= WRITE_BUFFER_SIZE - 1 - m_write_idx)
        return false;
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char* title) {
    return add_response("HTTP/1.1 %d %s\r\n", status, title);
}

bool http_conn::add_headers(int content_length) {
    add_content_type();
    add_content_length(content_length);
    add_linger();
    add_blankline();
}

bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_content(const char* content) {
    return add_response("%s", content);
}

bool http_conn::add_content_type() {
    return add_response("Content-Type: %s\r\n", "text/html");
}

bool http_conn::add_linger() {
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blankline() {
    return add_response("%s", "\r\n");
}

void http_conn::process() {
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    bool write_ret = process_write(read_ret);
    if(!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}