#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <string.h>

#include <errno.h>
#include <string.h>

#define PORT 8888
#define BACK_LOG 10
#define EPOLL_QUEUE_LEN 10
#define MAX_EPOLL_EVENTS 10
#define WAIT_SEC 2
#define MAX_LEN_MSG 10
#define MAX_CONNECTIONS 100
#define MAX_QUEUE_MSGS 100

struct context {
    int fd;
    int writable;
    int deleted;
};

struct context_node {
    struct context *data;
    struct context_node *next;
};

struct messages {
    char *data;
    int author;
    struct context_node *start_client;
};

struct messages_node {
    struct messages *data;
    struct messages_node *next;
};

int init_listener();
int drop_client(int epfd, struct context *cont);
void setnonblocking(int sock);

int main() {
    int listener;
    struct epoll_event events[MAX_EPOLL_EVENTS];
    int epfd;

    epfd = epoll_create(EPOLL_QUEUE_LEN);
    listener = init_listener();
    
    setnonblocking(listener);

    struct epoll_event server_event;
    server_event.events = EPOLLIN | EPOLLET;
    server_event.data.ptr = malloc(sizeof(struct context));
    ((struct context *) server_event.data.ptr)->fd = listener;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listener, &server_event) == -1) {
        printf("ERROR: epoll add");
        exit(1);
    }

    //TODO: реализовать список (новые в начало)
    struct context_node *context_node_head = NULL;

    //TODO: реализовать очередь (новые в конец), сейчас реализовано как список,
    // но это не тру, т.к. начинаем отправлять с головы, но начинать надо с старых.
    //+ удалять сообщения, где не осталось клиентов - легче
    struct messages_node *messages_node_head = NULL;

    for(;;) {
        int nfds = epoll_wait(epfd, events, MAX_EPOLL_EVENTS, WAIT_SEC * 1000);

        for (int i = 0; i < nfds; ++i) {
            struct context *cont = (struct context *) events[i].data.ptr;

            if (cont->fd == listener) {
                struct sockaddr_in cliaddr;
                socklen_t addr_size = sizeof cliaddr;

                //сделать в цикле пока не вернет -1 и errno == EAGAIN
                int client_socket = accept(listener, (struct sockaddr*) &cliaddr, &addr_size);
                if (client_socket < 0) {
                    printf("ERROR: accept");
                    exit(1);
                }

                printf("%d: ADD\n", client_socket);
 
                setnonblocking(client_socket);
                
                struct epoll_event ee;
                ee.events = EPOLLOUT | EPOLLIN | EPOLLRDHUP | EPOLLET;
                
                struct context_node * old_context_head = context_node_head;
                context_node_head = (struct context_node *) malloc(sizeof(struct context_node));
                context_node_head->next = old_context_head;
                context_node_head->data = malloc(sizeof(struct context));
                context_node_head->data->fd = client_socket;
                context_node_head->data->writable = 0;
                context_node_head->data->deleted = 0;
                ee.data.ptr = context_node_head->data;

                if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_socket, &ee) == -1) {
                    printf("ERROR: epoll add");
                    shutdown(client_socket, SHUT_RDWR);
                }

                continue;
            }

            if (events[i].events & EPOLLRDHUP) {
                printf("%d: EPOLLRDHUP \n", cont->fd);
                drop_client(epfd, cont);

                continue;
            }

            if (events[i].events & ~(EPOLLOUT | EPOLLIN | EPOLLRDHUP)) {
                printf("%d: EVENTS = %d \n", cont->fd, events[i].events);
            } 

            if (events[i].events & EPOLLOUT) {
                printf("%d: EPOLLOUT \n", cont->fd);

                cont->writable = 1;
            }

            if (events[i].events & EPOLLIN) {
                printf("%d: EPOLLIN \n", cont->fd);

                struct messages_node * old_messages_head = messages_node_head;
                messages_node_head = (struct messages_node *) malloc(sizeof(struct messages_node));
                messages_node_head->next = old_messages_head;
                messages_node_head->data = malloc(sizeof(struct messages));

                messages_node_head->data->data = (char *) malloc(MAX_LEN_MSG * sizeof(char));
                messages_node_head->data->author = cont->fd;
                messages_node_head->data->start_client = context_node_head;

                int count = recv(cont->fd, messages_node_head->data->data, MAX_LEN_MSG, 0);
                messages_node_head->data->data[count] = '\0'; //не уверен что z-строка считана

                if (count == 0) {
                    drop_client(epfd, cont);
                } else {
                    printf("recieve: len = %d; msg = [%s]\n", count, messages_node_head->data->data);
                }
            } 

            struct messages_node *ptr_msg = messages_node_head;
            while (ptr_msg != NULL) {
                struct context_node *ptr_cont = ptr_msg->data->start_client;

                while (ptr_cont != NULL) {
                    if (ptr_cont->data->deleted == 0 && 
                        ptr_cont->data->writable == 1 && 
                        ptr_cont->data->fd != ptr_msg->data->author)
                    {
                        send(ptr_cont->data->fd, ptr_msg->data->data, strlen(ptr_msg->data->data), 0);                    
                    }

                    ptr_cont = ptr_cont->next;
                    ptr_msg->data->start_client = ptr_cont;
                } 

                ptr_msg = ptr_msg->next;
            }

            //удалить узлы-сообщений, у сообщения которых кончились адресаты ptr_msg->data->start_client = NULL
            
            //удалить узлы-контексты, контексты которых помечены как удаленные, и не используются в качестве головного ноды в сообщениях 
        }
    }

    shutdown(listener, SHUT_RDWR);

    exit(0);
}

void setnonblocking(int sock) {
    int opts;
    opts = fcntl(sock, F_GETFL);
    
    if (opts == -1) {
        printf("ERROR: fcntl(F_GETFL)\n");
        exit(1);
    }

    opts |= O_NONBLOCK;

    if (fcntl(sock, F_SETFL, opts) == -1) {
        printf("ERROR: fcntl(F_SETFL)\n");
        exit(1);
    }
}

int init_listener() {
    int listener;
    listener = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listener, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        printf("ERROR bind\n");
        exit(1);
    }

    if (listen(listener, BACK_LOG) < 0) {
        printf("ERROR: Socket listen\n");
        exit(1);
    }

    return listener;
}

int drop_client(int epfd, struct context *cont){
    cont->deleted = 1;

    shutdown(cont->fd, SHUT_RDWR);

    int status = epoll_ctl(epfd, EPOLL_CTL_DEL, cont->fd, NULL);
    if (status == -1) {
        printf("%d: ERROR epoll del\n", cont->fd);
    }

    return status;
}
