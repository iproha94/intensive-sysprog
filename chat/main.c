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
    struct message_node *start_message;
};

struct context_node {
    struct context *data;
    struct context_node *next;
};

struct message {
    char *data;
    int author;
    int receivers;
};

struct message_node {
    struct message *data;
    struct message_node *next;
};

int init_listener();
int drop_client(int epfd, struct context *cont);
void setnonblocking(int sock);
struct message_node *add_msg_to_queue(struct message_node *message_node_head,
									  struct message *msg);
struct context_node *add_context_to_list(struct context_node *context_node_head,
										 struct context *cont);
struct message_node *delete_unnecessary_messages(struct message_node *message_node_head);

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

    //list (new to begin)
    struct context_node *context_node_head = NULL;

    //queue (new to end)
    struct message_node *message_node_head = NULL;

    for(;;) {
        int nfds = epoll_wait(epfd, events, MAX_EPOLL_EVENTS, WAIT_SEC * 1000);

        for (int i = 0; i < nfds; ++i) {
            struct context *cont = (struct context *) events[i].data.ptr;

            if (cont->fd == listener) {
                struct sockaddr_in cliaddr;
                socklen_t addr_size = sizeof cliaddr;

                //TODO: do accept while ret -1 or errno == EAGAIN
                int client_socket = accept(listener, (struct sockaddr*) &cliaddr, &addr_size);
                if (client_socket < 0) {
                    printf("ERROR: accept");
                    exit(1);
                }

                printf("%d: ADD\n", client_socket);
 
                setnonblocking(client_socket);
                
                struct epoll_event ee;
                ee.events = EPOLLOUT | EPOLLIN | EPOLLRDHUP | EPOLLET;

                struct context *cont = (struct context *) malloc(sizeof(struct context));
                cont->fd = client_socket;
                cont->writable = 0;
                cont->deleted = 0;
                cont->start_message = NULL;

                context_node_head = add_context_to_list(context_node_head, cont);
                
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

                struct message *msg = (struct message *) malloc(sizeof(struct message));
                msg->data = (char *) malloc(MAX_LEN_MSG * sizeof(char));
                msg->author = cont->fd;
                msg->receivers = 0;

                //TODO: while ret -1 or ERRNO == EAGAIN
                int count = 0;
                count += recv(cont->fd, msg->data, MAX_LEN_MSG, 0);

                if (count == -1 || count == 0) {
                    drop_client(epfd, cont);
                } else {
	                msg->data[count] = '\0';

                    printf("recieve: len = %d; msg = [%s]\n", count, msg->data);
                
                    struct message_node *message_node_tail = add_msg_to_queue(message_node_head, msg);
                    if (message_node_head == NULL){
                    	message_node_head = message_node_tail;
                    }

                    struct context_node *ptr_cont = context_node_head;
                    while (ptr_cont != NULL) {
                        if (ptr_cont->data->start_message == NULL) {
                        	ptr_cont->data->start_message = message_node_tail;
                        }

                        struct message_node *msg_node_temp = ptr_cont->data->start_message;
                        while (msg_node_temp != NULL) {
	                        ++(msg_node_temp->data->receivers);

                        	msg_node_temp = msg_node_temp->next;
                        }

                        ptr_cont = ptr_cont->next;
                    } 
                }                
            } 

            struct context_node *ptr_cont = context_node_head;
            while (ptr_cont != NULL) {
                struct message_node *ptr_msg = ptr_cont->data->start_message;

                while (ptr_msg != NULL) {
                	if (ptr_cont->data->deleted) {
                		--(ptr_msg->data->receivers);
                	}

                	if (ptr_cont->data->deleted == 0 && 
                	    ptr_cont->data->writable == 1 && 
                	    ptr_cont->data->fd != ptr_msg->data->author)
                	{
                		//TODO: while ret -1 or ERRNO == EAGAIN
                	    send(ptr_cont->data->fd, ptr_msg->data->data, strlen(ptr_msg->data->data), 0);                    
                	    //TODO: break if ret -1 or ERRNO == EAGAIN

                		--(ptr_msg->data->receivers);
                	}

                	ptr_msg = ptr_msg->next;
                	ptr_cont->data->start_message = ptr_msg;
                }

                ptr_cont = ptr_cont->next;
            } 

            message_node_head = delete_unnecessary_messages(message_node_head);

            //TODO: delete contextes, which .deleted != 0 
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

struct message_node *add_msg_to_queue(struct message_node *message_node_head,
									  struct message *msg) 
{
	struct message_node * new_message_tail = (struct message_node *) malloc(sizeof(struct message_node));
	new_message_tail->next = NULL;
	new_message_tail->data = msg;

	if (message_node_head == NULL){
		return new_message_tail;
	} 

	struct message_node * now_message_tail = message_node_head;
	while (now_message_tail->next != NULL) {
		now_message_tail = now_message_tail->next;
	}
	now_message_tail->next = new_message_tail;

	return new_message_tail;
}

struct context_node *add_context_to_list(struct context_node *context_node_head,
										 struct context *cont)
{
	struct context_node * new_context_head = (struct context_node *) malloc(sizeof(struct context_node));
	new_context_head->next = context_node_head;
	new_context_head->data = cont;

	return new_context_head;
}

struct message_node *delete_unnecessary_messages(struct message_node *message_node_head) {
	if (message_node_head != NULL &&
		message_node_head->data->receivers == 0)
	{
		free(message_node_head->data->data);
		free(message_node_head->data);

		struct message_node *new_message_node_head = message_node_head->next;
		free(message_node_head);

		return delete_unnecessary_messages(new_message_node_head);
	}

	return message_node_head;
}