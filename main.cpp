#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/epoll.h>
#include <errno.h>
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
#include "lst_timer.h"

#define MAX_FD 65535
#define MAX_EVENT_NUMBER 10000
#define TIMESLOT 5

static int pipefd[2];
static sort_timer_list timer_list;

extern void addfd(int epollfd, int fd, bool oneshot, bool et);

extern void removefd(int epollfd, int fd);

extern void modfd(int epollfd, int fd, int ev);

extern void setnonblocking(int sockfd);

void sig_handler(int sig) {
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

void addsig(int sig_num) {
    struct sigaction act;
    bzero(&act, sizeof(act));
    act.sa_handler = sig_handler;
    act.sa_flags |= SA_RESTART;
    sigfillset(&act.sa_mask);
    sigaction(sig_num, &act, NULL);
}

void cb_func(http_conn* user_data) {
    if(user_data->m_sockfd != -1) {
        printf("close fd %d\n", user_data->m_sockfd);
        removefd(http_conn::m_epollfd, user_data->m_sockfd);
        user_data->m_sockfd = -1;
        http_conn::m_user_count--;
    }
}

void timer_handler() {
    timer_list.tick();
    alarm(TIMESLOT);
}

int main(int argc, char* argv[]) {
    if(argc <= 1) {
        printf("请按照如下格式输入： %s port_number\n", basename(argv[0]));
        exit(-1);
    }

    int port = atoi(argv[1]);

    threadpool<http_conn>* pool = NULL;
    try{
        pool = new threadpool<http_conn>;
    }catch(...) {
        exit(-1);
    }

    http_conn* users = new http_conn[MAX_FD];

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);

    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    bind(listenfd, (struct sockaddr*)&address, sizeof(address));

    listen(listenfd, 5);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    addfd(epollfd, listenfd, false, false);

    http_conn::m_epollfd = epollfd;

    socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false, true);

    addsig(SIGALRM);
    addsig(SIGTERM);
    bool stop_server = false;
    bool timeout = false;
    alarm(TIMESLOT);

    while(!stop_server) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);

        if(number == -1 && errno != EINTR) {
            printf("epoll failure\n");
            break;
        }

        for(int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;

            if(sockfd == listenfd) {
                struct sockaddr_in client_address;
                socklen_t client_addlen = sizeof(client_address);
                int connfd = accept(sockfd, (struct sockaddr*)&client_address, &client_addlen);

                if(http_conn::m_user_count >= MAX_FD) {
                    close(connfd);
                    continue;
                }
                users[connfd].init(connfd, client_address);

                util_timer* timer = new util_timer;
                timer->user_data = &users[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                users[connfd].timer = timer;
                timer_list.add_timer(timer);
            }else if((sockfd == pipefd[0]) && ((events[i].events & EPOLLIN))) {
                char signals[1024];
                int ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if(ret == -1) {
                    continue;
                }else if(ret == 0) {
                    continue;
                }else {
                    for(int i = 0; i < ret; i++) {
                        switch (signals[i]) {
                            case SIGALRM: {
                                timeout = true;
                                break;
                            }
                            case SIGTERM: {
                                stop_server = true;
                                break;
                            }
                        }
                    }
                }
            }else if(events[i].events & (EPOLLRDHUP | EPOLLERR | EPOLLERR)) {
                users[sockfd].close_conn();
            }else if(events[i].events & EPOLLIN) {
                if(users[sockfd].read()) {
                    if(users[sockfd].timer) {
                        time_t cur = time(NULL);
                        users[sockfd].timer->expire = cur + 3 * TIMESLOT;
                        printf("adjust timer once\n");
                        timer_list.adjust_timer(users[sockfd].timer);
                    }
                    pool->append(users + sockfd);
                }else {
                    users[sockfd].close_conn();
                    if(users[sockfd].timer) {
                        timer_list.del_timer(users[sockfd].timer);
                    }
                }
            }else if(events[i].events & EPOLLOUT) {
                if(!users[sockfd].write()) {
                    users[sockfd].close_conn();
                    if(users[sockfd].timer) {
                        timer_list.del_timer(users[sockfd].timer);
                    }
                }
            }
        }
        if(timeout) {
            timer_handler();
            timeout = false;
        }
    }

    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] users;
    delete pool;

    return 0;
}