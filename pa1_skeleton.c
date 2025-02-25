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
} client_thread_data_t;

/*
 * This function runs in a separate client thread to handle communication with the server
 */
void *client_thread_func(void *arg) {
    client_thread_data_t *data = (client_thread_data_t *)arg;
    struct epoll_event event, events[MAX_EVENTS];
    char send_buf[MESSAGE_SIZE] = "ABCDEFGHIJKMLNOP"; /* Send 16-Bytes message every time */
    char recv_buf[MESSAGE_SIZE];
    struct timeval start, end;

    // Hint 1: register the "connected" client_thread's socket in the its epoll instance
    // Hint 2: use gettimeofday() and "struct timeval start, end" to record timestamp, which can be used to calculated RTT.

    /* TODO:
     * It sends messages to the server, waits for a response using epoll,
     * and measures the round-trip time (RTT) of this request-response.
     */
 
    /* TODO:
     * The function exits after sending and receiving a predefined number of messages (num_requests). 
     * It calculates the request rate based on total messages and RTT
     */

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

    /* DONE:
     * Create sockets and epoll instances for client threads
     * and connect these sockets of client threads to the server
     */
    const int MAX_RETRIES = 5; //Max number of times to retry creating epoll instance, socket, or connecting socket
    //Store host info in server_addr
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = server_port;

    //Create epoll instance, socket & socket connection for each thread
    for (int i = 0; i < num_client_threads; i++) {
        int retryCount = 0;
        //Create epoll instance
        int c = thread_data[i].epoll_fd = epoll_create1(0);
        while (c < 0 && retryCount++ <= MAX_RETRIES) {
            c = thread_data[i].epoll_fd = epoll_create1(0);
            printf("Epoll instance %i failed. Retrying (%i remaining)\n", i, MAX_RETRIES-retryCount);
        }
        if (retryCount > MAX_RETRIES) {
            perror("Max epoll retries exceeded");
            exit(-1);
        }
        retryCount = 0;

        //Create socket
        int s = thread_data[i].socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        while (s < 0 && retryCount++ <= MAX_RETRIES) {
            s = thread_data[i].socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            printf("Socket init %i failed. Retrying (%i remaining)\n", i, MAX_RETRIES-retryCount);
        }
        if (retryCount > MAX_RETRIES) {
            perror("Max socket retries exceeded");
            exit(-1);
        }
        retryCount = 0;

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
    }
    
    // Hint: use thread_data to save the created socket and epoll instance for each thread
    // You will pass the thread_data to pthread_create() as below
    for (int i = 0; i < num_client_threads; i++) {
        pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
    }

    /* DONE:
     * Wait for client threads to complete and aggregate metrics of all client threads
     */
    long long total_rtt = 0;
    long total_messages = 0;
    float total_request_rate = 0;
    for (int i = 0; i < num_client_threads; i++) {
        int pj = pthread_join(threads[i], NULL); //Ignores retval of individual threads (2nd arg is NULL)
        if (pj != 0) {
             perror("Error when joining thread"); 
             exit (-1); 
        }
        total_rtt += thread_data[i].total_rtt;
        total_messages += thread_data[i].total_messages;
        total_request_rate += thread_data[i].request_rate;

        //Close socket & epoll instance
        close(thread_data[i].socket_fd);
        close(thread_data[i].epoll_fd);
    }

    printf("Average RTT: %lld us\n", total_rtt / total_messages);
    printf("Total Request Rate: %f messages/s\n", total_request_rate);
}

void run_server() {

    /* TODO:
     * Server creates listening socket and epoll instance.
     * Server registers the listening socket to epoll
     */

    /* Server's run-to-completion event loop */
    while (1) {
        /* TODO:
         * Server uses epoll to handle connection establishment with clients
         * or receive the message from clients and echo the message back
         */
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