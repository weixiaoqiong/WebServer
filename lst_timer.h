#ifndef LST_TIMER
#define LST_TIMER

#include <stdio.h>
#include <arpa/inet.h>
#include <time.h>

class http_conn;

class util_timer {
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire;
    void (*cb_func) ( http_conn* );
    http_conn* user_data;
    util_timer* prev;
    util_timer* next;
};

class sort_timer_list {
public:
    sort_timer_list() : head(NULL), tail(NULL) {}

    ~sort_timer_list() {
        util_timer* cur = head;
        while(cur) {
            head = head->next;
            delete cur;
            cur = head;
        }
    }

    void add_timer(util_timer* timer) {
        if(!timer)
            return;
        if(!head) {
            head = tail = timer;
            return;
        }
        if(timer->expire < head->expire) {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        add_timer(timer, head);
    }

    void adjust_timer(util_timer* timer) {
        if(!timer || !timer->next)
            return;
        util_timer* tmp = timer->next;
        if(tmp->expire > timer->expire) {
            return;
        }
        if(timer == head) {
            head = head->next;
            head->prev = NULL;
            timer->next = NULL;
            add_timer(timer, head);
        }else {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer(timer, timer->next);
        }
    }

    void del_timer(util_timer* timer) {
        if(!timer)
            return;
        if(timer == head && timer == tail) {
            delete timer;
            head = tail = NULL;
            return;
        }
        if(timer == head) {
            head = head->next;
            head->prev = NULL;
            delete timer;
            return;
        }
        if(timer == tail) {
            tail = tail->prev;
            tail->next = NULL;
            delete timer;
            return;
        }
        timer->next->prev = timer->prev;
        timer->prev->next = timer->next;
        delete timer;
    }

    void tick() {
        if(!head)
            return;
        printf("time tick\n");
        time_t cur = time(NULL);
        util_timer* tmp = head;
        while(tmp) {
            if(cur < tmp->expire) {
                break;
            }
            printf("%d\n", tmp->user_data->m_sockfd);
            tmp->cb_func(tmp->user_data);
            head = head->next;
            if(head)
                head->prev = NULL;
            delete tmp;
            tmp = head;
        }
    }

private:
    void add_timer(util_timer* timer, util_timer* lst_head) {
        util_timer* cur = lst_head;
        util_timer* nxt = cur->next;
        while(nxt) {
            if(nxt->expire > timer->expire) {
                cur->next = timer;
                timer->prev = cur;
                timer->next = nxt;
                nxt->prev = timer;
                break;
            }
            cur = nxt;
            nxt = cur->next;
        }
        if(!nxt) {
            cur->next = timer;
            timer->prev = cur;
            timer->next = NULL;
            tail = timer;
        }
    }

    util_timer *head;
    util_timer *tail;
};

#endif