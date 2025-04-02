/*
# Copyright 2025 University of Kentucky
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
*/

/*
References used: 
- Lecture 4 slides: socket programming basics
- Linux documentation (man7.org)
- "How to use epoll?" (https://rg4.net/archives/375.html)
*/

/* 
Please specify the group members here

# Student #1: Jacob Schuetter
# Student #2: AJ Fluty
# Student #3: 

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>

#define MAX_EVENTS 64
#define MESSAGE_SIZE 16
#define DEFAULT_CLIENT_THREADS 4

char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 1000000;

/*
 * This structure is used to store per-thread data in the client
 */
typedef struct {
    int epoll_fd;        /* File descriptor for the epoll instance, used for monitoring events on the socket. */
    int socket_fd;       /* File descriptor for the client socket connected to the server. */
    long long total_rtt; /* Accumulated Round-Trip Time (RTT) for all messages sent and received (in microseconds). */
    long total_messages; /* Total number of messages sent and received. */
    float request_rate;  /* Computed request rate (requests per second) based on RTT and total messages. */
    //Added for PA2: 
    long tx_cnt, rx_cnt; /* Total number of messages sent and received (respectively) by the thread */
    long lost_pkt_cnt;   /* Number of packets lost (equivalent to tx_cnt - rx_cnt) */
    struct sockaddr_in server_addr;
    int server_addr_len;
} client_thread_data_t;

/*
 * This function runs in a separate client thread to handle communication with the server
 */
void *client_thread_func(void *arg) {
    client_thread_data_t *data = (client_thread_data_t *)arg;
    // struct epoll_event event, events[MAX_EVENTS];
    char send_buf[MESSAGE_SIZE] = "ABCDEFGHIJKMLNOP"; /* Send 16-Bytes message every time */
    char recv_buf[MESSAGE_SIZE];
    struct timeval start, end;

    //Initialize count values
    data->tx_cnt = 0;
    data->rx_cnt = 0;
    data->total_rtt = 0;
    data->total_messages = 0;

    //Debug: put socket ID in message
    // send_buf[0] = (char)data->socket_fd;
    // printf("%s\n", send_buf);

    // char id[5];
    // sprintf(id, "%d", data->socket_fd);
    // send_buf[0] = id[0];
    // if (id[1] != '\0') send_buf[1] = id[1];

    // printf("%s\n", send_buf);
    // printf("test: %s; %d\n", send_buf, data->socket_fd);

    // event.data.fd = data->socket_fd;
    // event.events = EPOLLIN;
    // if (epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, data->socket_fd, &event) < 0) {
    //     perror("Failed to add socket to epoll");
    //     return NULL;
    // }

    for (int i = 0; i < num_requests; i++) {
        //Clear buffers
        memset(recv_buf, '\0', sizeof(recv_buf));
        // memset(send_buf, '\0', sizeof(send_buf));

        gettimeofday(&start, NULL); // record start time

        // send message to server
        // if (sendto(data->socket_fd, send_buf, MESSAGE_SIZE, 0, (struct sockaddr *) &(data->server_addr), sizeof(data->server_addr)) < 0) {
        if (send(data->socket_fd, send_buf, MESSAGE_SIZE, 0) < 0) {
            perror("Write failed");
            continue;
        }
        data->tx_cnt++; //Increment packets sent count
        // printf("%d: %ld/%ld\n", data->socket_fd, data->tx_cnt, data->rx_cnt);
        

        // wait for response
        // int event_count = epoll_wait(data->epoll_fd, events, MAX_EVENTS, -1);
        // if (event_count < 0) {
        //     perror("Epoll wait failed");
        //     continue;
        // }

        // read response from server
        // if (recv(data->socket_fd, recv_buf, MESSAGE_SIZE, 0, (struct sockaddr *) &(data->server_addr), &(data->server_addr_len)) < 0) {
        if (recv(data->socket_fd, recv_buf, MESSAGE_SIZE, 0) < 0) {
            perror("ACK failed");
            continue;
        }
        // } else if (strcmp(send_buf, recv_buf) != 0) {
        //     printf("Read failed - wrong response - \nsent: %s\nrecd: %s\n", send_buf, recv_buf);
        //     // continue;
        // }
        // printf("Sent: %s\nRecd: %s, Total: %ld\n", send_buf, recv_buf, data->total_messages);
        data->rx_cnt++; //Increment packets received count
        // printf("%d: %ld/%ld\n", data->socket_fd, data->tx_cnt, data->rx_cnt);
        
        // printf("%s - %ld/%ld\n", recv_buf, data->tx_cnt, data->rx_cnt);
        // printf("%d - %ld/%ld\n", data->socket_fd, data->tx_cnt, data->rx_cnt);

        gettimeofday(&end, NULL); // record end time

        // compute RTT
        long long rtt = (end.tv_sec - start.tv_sec) * 1000000LL + (end.tv_usec - start.tv_usec);
        data->total_rtt += rtt;
        data->total_messages++;
    }
    // printf("%d completed requests\n", data->socket_fd);
    
    // compute request rate
    data->request_rate = (float)data->total_messages / (data->total_rtt / 1000000.0);
    // compute lost packet count
    data->lost_pkt_cnt = data->tx_cnt - data->rx_cnt;
	
    printf("%d exiting...\n", data->socket_fd);
    
    return NULL;
}

/*
 * This function orchestrates multiple client threads to send requests to a server,
 * collect performance data of each threads, and compute aggregated metrics of all threads.
 */
void run_client() {
    pthread_t threads[num_client_threads];
    client_thread_data_t thread_data[num_client_threads];
    struct sockaddr_in server_addr;
    int SOCK_TIMEOUT_SEC = 1; //Socket timeout value

    const int MAX_RETRIES = 5; //Max number of times to retry creating epoll instance, socket, or connecting socket
    //Store host info in server_addr
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = server_port;

    //Create epoll instance, socket & socket connection for each thread
    for (int i = 0; i < num_client_threads; i++) {
        int retryCount = 0;
        // //Create epoll instance
        // int c = thread_data[i].epoll_fd = epoll_create1(0);
        // while (c < 0 && retryCount++ <= MAX_RETRIES) {
        //     c = thread_data[i].epoll_fd = epoll_create1(0);
        //     printf("Epoll instance %i failed. Retrying (%i remaining)\n", i, MAX_RETRIES-retryCount);
        // }
        // if (retryCount > MAX_RETRIES) {
        //     perror("Max epoll retries exceeded");
        //     exit(-1);
        // }
        // retryCount = 0;

        //Create socket
        int s = thread_data[i].socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        while (s < 0 && retryCount++ <= MAX_RETRIES) {
            s = thread_data[i].socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            printf("Socket init %i failed. Retrying (%i remaining)\n", i, MAX_RETRIES-retryCount);
        }
        if (retryCount > MAX_RETRIES) {
            perror("Max socket retries exceeded");
            exit(-1);
        }
        retryCount = 0;

        //Set socket timeout option
        struct timeval timeout;
        timeout.tv_sec = SOCK_TIMEOUT_SEC; //Timeout value
        timeout.tv_usec = 0;
        setsockopt(thread_data[i].socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)); 

        //Debug: print socket fd
        // printf("Thread %d: socket %d\n", i, thread_data[i].socket_fd);
        
        // Provide server addr info
        // thread_data[i].server_addr = server_addr;
        // thread_data[i].server_addr_len = sizeof(server_addr);

        //Connect sockets
       	int w = connect(thread_data[i].socket_fd, (struct sockaddr *) &server_addr, sizeof(server_addr));
	    while (w < 0 && retryCount++ <= MAX_RETRIES) {
		    w = connect(thread_data[i].socket_fd, (struct sockaddr *) &server_addr, sizeof(server_addr));
            printf("Socket connection %i failed. Retrying (%i remaining)\n", i, MAX_RETRIES-retryCount);
        }
        if (retryCount > MAX_RETRIES) {
            perror("Max socket connect retries exceeded");
            exit(-1);
        }
        printf("%d (%d)\n", i, thread_data[i].socket_fd);
    }
    
    // Hint: use thread_data to save the created socket and epoll instance for each thread
    // You will pass the thread_data to pthread_create() as below
    for (int i = 0; i < num_client_threads; i++) {
        pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
    }

    //Aggregate request data
    long long total_rtt = 0;
    long total_messages = 0;
    float total_request_rate = 0;
    long total_packets_lost = 0;
    for (int i = 0; i < num_client_threads; i++) {
        int pj = pthread_join(threads[i], NULL); //Ignores retval of individual threads (2nd arg is NULL)
        if (pj != 0) {
             perror("Error when joining thread"); 
             exit (-1); 
        }
        total_rtt += thread_data[i].total_rtt;
        total_messages += thread_data[i].total_messages;
        total_request_rate += thread_data[i].request_rate;
        total_packets_lost += thread_data[i].lost_pkt_cnt;

        printf("%d (%d) returned\n", i, thread_data[i].socket_fd);
        

        //Close socket & epoll instance
        close(thread_data[i].socket_fd);
        // close(thread_data[i].epoll_fd);
        // printf("%d/%d returned.\n", i, num_client_threads);
        // printf("%d returned\n", i);
    }

    printf("Average RTT: %lld us\n", total_rtt / total_messages);
    printf("Total Request Rate: %f messages/s\n", total_request_rate);
    printf("Total packets lost: %ld\n", total_packets_lost);

    fflush(stdout);
}

void run_server() {
    int QUEUE_SIZE = 10; //Define server listen queue size
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    char recv_buf[MESSAGE_SIZE];
    char send_buf[MESSAGE_SIZE];

    // //Clear buffers
    // memset(recv_buf, '\0', sizeof(recv_buf));
    // memset(send_buf, '\0', sizeof(send_buf));

    //Create server IP addr structure
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = server_port;

    //Create listen socket
    int on = 1;
    int listen_socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    setsockopt(listen_socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on)); //Set options for listen socket to allow reusing local port number
    int b, l; //Variables for ret values of bind & listen calls
    if (listen_socket_fd < 0) {
        perror("Listen socket creation failed");
        exit(-1);
    }
    b = bind(listen_socket_fd, (struct sockaddr *) &server_addr, sizeof(server_addr));
    if (b < 0) {
        perror("Listen socket bind failed"); 
        exit(-1);
    }
    /* Cannot "listen" to a UDP socket */
    // l = listen(listen_socket_fd, QUEUE_SIZE);
    // if (l < 0) {
    //     perror("Listen socket listen failed");
    //     exit(-1);
    // }

    //Initialize epoll
    // int server_epoll_fd = epoll_create1(0);

    // //Add listen socket to epoll
    // struct epoll_event listener_event;
    // listener_event.data.fd = listen_socket_fd;
    // listener_event.events = EPOLLIN;
    // int c = epoll_ctl(server_epoll_fd, EPOLL_CTL_ADD, listen_socket_fd, (struct epoll_event *) &listener_event);
    // if (c < 0) {
    //         perror("Failed to add listener to epoll");
    //         exit(-1);
    // }

    /* Server's run-to-completion listener_event loop */
    int accept_socket_fd;
    while (1) {
        //Clear buffers
        memset(recv_buf, '\0', sizeof(recv_buf));
        memset(send_buf, '\0', sizeof(send_buf));
        //Receive incoming packet
        int r = recvfrom(listen_socket_fd, recv_buf, MESSAGE_SIZE, 0, (struct sockaddr *) &client_addr, &client_addr_len);
        if (r < 0) {
            perror("Error receiving packet");
            break;
        }
        printf("Message: %s; len %d; src %d:%d\n", recv_buf, r, client_addr.sin_addr.s_addr, client_addr.sin_port);
        
        strcpy(send_buf, recv_buf);

        //Echo packet back
        int s = sendto(listen_socket_fd, send_buf, MESSAGE_SIZE, 0, (struct sockaddr *) &client_addr, sizeof(client_addr));
        if (s < 0) {
            perror("Error echoing packet");
            break;
        }


        //Wait for epoll listener_event
        // struct epoll_event events[MAX_EVENTS]; //Store events
        // int numEvents = epoll_wait(server_epoll_fd, events, MAX_EVENTS, 0);
        //Handle epoll events sequentially
        // for (int i = 0; i < numEvents; i++) {
        //     //Check for errors in epoll events
        //     if ((events[i].events & EPOLLERR) ){
        //         //Close FDs when closed (or error occurs)
        //         // perror("EPOLLERR on fd");
        //         close(events[i].data.fd);
        //         continue;
        //     }
        //     else if (events[i].data.fd == listen_socket_fd) {
        //         //Notification on listen socket => incoming packet
        //         if (read(events[i].data.fd, buf, MESSAGE_SIZE) < 0) { //Read message
        //             perror("Read error");
        //             exit(-1);
        //         } 
        //         if (write(events[i].data.fd, buf, MESSAGE_SIZE) < 0) { //Echo message back
        //             perror("Write error");
        //             exit(-1);
        //         }

        //         // accept_socket_fd = accept(listen_socket_fd, NULL, NULL);
        //         // if (accept_socket_fd < 0) {
        //         //     perror("Accept failed");
        //         //     continue;
        //         // }
        //         // //Add to epoll
        //         // //Define epoll event for data on new connection
        //         // events[accept_socket_fd].data.fd = accept_socket_fd;
        //         // events[accept_socket_fd].events = EPOLLIN;
        //         // int c = epoll_ctl(server_epoll_fd, EPOLL_CTL_ADD, accept_socket_fd, (struct epoll_event *) &events[accept_socket_fd]);
        //         // if (c < 0) {
        //         //     perror("Failed to add conn to epoll");
        //         //     exit(-1);
        //         // }
        //     }
            // else if (events[i].events & EPOLLIN) { //Data incoming on other connection
            //     if (read(events[i].data.fd, buf, MESSAGE_SIZE) < 0) { //Read message
            //         perror("Read error");
            //         exit(-1);
            //     } 
            //     if (write(events[i].data.fd, buf, MESSAGE_SIZE) < 0) { //Echo message back
            //         perror("Write error");
            //         exit(-1);
            //     }
            // }
        // }
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "server") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);

        run_server();
    } else if (argc > 1 && strcmp(argv[1], "client") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);
        if (argc > 4) num_client_threads = atoi(argv[4]);
        if (argc > 5) num_requests = atoi(argv[5]);

        run_client();
    } else {
        printf("Usage: %s <server|client> [server_ip server_port num_client_threads num_requests]\n", argv[0]);
    }

    return 0;
}
