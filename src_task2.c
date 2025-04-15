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
- Lecture slides
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
#define TIMEOUT_USEC 50000

char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 1000000;

/*
 * This structure is used to store per-thread data in the client
 */
typedef struct
{
    int epoll_fd;        /* File descriptor for the epoll instance, used for monitoring events on the socket. */
    int socket_fd;       /* File descriptor for the client socket connected to the server. */
    long long total_rtt; /* Accumulated Round-Trip Time (RTT) for all messages sent and received (in microseconds). */
    long total_messages; /* Total number of messages sent and received. */
    float request_rate;  /* Computed request rate (requests per second) based on RTT and total messages. */
    // Added for PA2:
    long tx_cnt, rx_cnt; /* Total number of messages sent and received (respectively) by the thread */
    long lost_pkt_cnt;   /* Number of packets lost (equivalent to tx_cnt - rx_cnt) */
} client_thread_data_t;

// Dataframe structure
typedef unsigned int seq_nr; // Sequence or ACK numbers
typedef struct
{
    pthread_t sender_id; // Thread ID of sender thread
    seq_nr seq;
    seq_nr ack;
    char payload[MESSAGE_SIZE];
} frame;

/*
 * This function runs in a separate client thread to handle communication with the server
 */
void *client_thread_func(void *arg)
{
    client_thread_data_t *data = (client_thread_data_t *)arg;
    // char send_buf[MESSAGE_SIZE] = "ABCDEFGHIJKMLNOP";
    // char recv_buf[MESSAGE_SIZE];
    struct timeval start, end;
    struct timeval timeout;

    // Build dataframe
    frame send_frame, rcv_frame;
    pthread_t tid = pthread_self();
    char msg[MESSAGE_SIZE] = "ABCDEFGHIJKLMNOP";
    strcpy(send_frame.payload, msg);

    // Initialize sequence number count at 0
    seq_nr next_sn = 0;

    data->tx_cnt = 0;
    data->rx_cnt = 0;
    data->total_rtt = 0;
    data->total_messages = 0;

    for (int i = 0; i < num_requests; i++)
    {
        gettimeofday(&start, NULL);

        send_frame.sender_id = tid;
        send_frame.seq = next_sn;

        if (send(data->socket_fd, (struct frame *)&send_frame, MESSAGE_SIZE, 0) < 0)
        {
            perror("Write failed");
            i--;
            continue;
        }
        data->tx_cnt++;

        // Set socket timeout for recv
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(data->socket_fd, &read_fds);
        timeout.tv_sec = 0;
        timeout.tv_usec = TIMEOUT_USEC;

        // Attempt to receive ACK
        int ready = select(data->socket_fd + 1, &read_fds, NULL, NULL, &timeout);
        // If socket is ready to read, try to get ACK frame
        // Sequence number gets compared on next call of while() line below
        if (ready > 0 && FD_ISSET(data->socket_fd, &read_fds))
        {
            // Store ACK from server in same `send_frame` struct
            if (recv(data->socket_fd, (struct frame *)&send_frame, MESSAGE_SIZE, 0) < 0)
            {
                perror("ACK failed");
            }
        }

        // Retransmission loop
        //   While returned ACK number does not match current sequence number
        //   or sender_ID on ACK does not match thread ID, retransmit
        //   (not incrementing tx_cnt)
        while (send_frame.ack != next_sn || send_frame.sender_id != tid)
        {
            // Retransmit
            send_frame.sender_id = tid;
            send_frame.seq = next_sn;

            if (send(data->socket_fd, (struct frame *)&send_frame, MESSAGE_SIZE, 0) < 0)
            {
                perror("Write failed");
                continue;
            }

            // Receive ACK
            FD_ZERO(&read_fds);
            FD_SET(data->socket_fd, &read_fds);
            int ready = select(data->socket_fd + 1, &read_fds, NULL, NULL, &timeout);
            if (ready > 0 && FD_ISSET(data->socket_fd, &read_fds))
            {
                if (recv(data->socket_fd, (struct frame *)&send_frame, MESSAGE_SIZE, 0) < 0)
                {
                    perror("ACK failed");
                }
            }
        }
        // After successful ACK:
        data->rx_cnt++; // Increment packets received count
        next_sn++;      // Increment sequence number

        gettimeofday(&end, NULL); // record end time

        // compute RTT
        long long rtt = (end.tv_sec - start.tv_sec) * 1000000LL + (end.tv_usec - start.tv_usec);
        data->total_rtt += rtt;
        data->total_messages++;
    }

    data->request_rate = (float)data->total_messages / (data->total_rtt / 1000000.0);
    data->lost_pkt_cnt = data->tx_cnt - data->rx_cnt;

    return NULL;
}
/*
 * This function orchestrates multiple client threads to send requests to a server,
 * collect performance data of each threads, and compute aggregated metrics of all threads.
 */
void run_client()
{
    pthread_t threads[num_client_threads];
    client_thread_data_t thread_data[num_client_threads];
    const int SOCK_TIMEOUT_SEC = 1; // Socket timeout value
    const int MAX_RETRIES = 5;      // Max number of times to retry creating epoll instance, socket, or connecting socket

    // Store host info in server_addr
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = server_port;

    // Create epoll instance, socket & socket connection for each thread
    for (int i = 0; i < num_client_threads; i++)
    {
        int retryCount = 0;
        // Create socket
        int s = thread_data[i].socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        while (s < 0 && retryCount++ <= MAX_RETRIES)
        {
            s = thread_data[i].socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            printf("Socket init %i failed. Retrying (%i remaining)\n", i, MAX_RETRIES - retryCount);
        }
        if (retryCount > MAX_RETRIES)
        {
            perror("Max socket retries exceeded");
            exit(-1);
        }
        retryCount = 0;

        // Set socket timeout option
        struct timeval timeout;
        timeout.tv_sec = SOCK_TIMEOUT_SEC; // Timeout value
        timeout.tv_usec = 0;
        setsockopt(thread_data[i].socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        // Connect sockets
        int w = connect(thread_data[i].socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        while (w < 0 && retryCount++ <= MAX_RETRIES)
        {
            w = connect(thread_data[i].socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
            printf("Socket connection %i failed. Retrying (%i remaining)\n", i, MAX_RETRIES - retryCount);
        }
        if (retryCount > MAX_RETRIES)
        {
            perror("Max socket connect retries exceeded");
            exit(-1);
        }
    }

    // Hint: use thread_data to save the created socket and epoll instance for each thread
    // You will pass the thread_data to pthread_create() as below
    for (int i = 0; i < num_client_threads; i++)
    {
        pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
    }

    // Aggregate request data
    long long total_rtt = 0;
    long total_messages = 0;
    float total_request_rate = 0;
    long total_packets_lost = 0;
    for (int i = 0; i < num_client_threads; i++)
    {
        int pj = pthread_join(threads[i], NULL); // Ignores retval of individual threads (2nd arg is NULL)
        if (pj != 0)
        {
            perror("Error when joining thread");
            exit(-1);
        }
        total_rtt += thread_data[i].total_rtt;
        total_messages += thread_data[i].total_messages;
        total_request_rate += thread_data[i].request_rate;
        total_packets_lost += thread_data[i].lost_pkt_cnt;

        // Close socket
        close(thread_data[i].socket_fd);
    }

    printf("Average RTT: %lld us\n", total_rtt / total_messages);
    printf("Total Request Rate: %f messages/s\n", total_request_rate);
    printf("Total packets lost: %ld\n", total_packets_lost);
}

void run_server()
{
    int QUEUE_SIZE = 10; // Define server listen queue size
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    frame recv_frame;

    // Create server IP addr structure to bind socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = server_port;

    // Create listen socket
    int on = 1;
    int listen_socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    setsockopt(listen_socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on)); // Set options for listen socket to allow reusing local port number
    int b, l;                                                                        // Variables for ret values of bind & listen calls
    if (listen_socket_fd < 0)
    {
        perror("Listen socket creation failed");
        exit(-1);
    }
    b = bind(listen_socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (b < 0)
    {
        perror("Listen socket bind failed");
        exit(-1);
    }

    /* Server's run-to-completion listener_event loop */
    int accept_socket_fd;
    while (1)
    {
        // Receive incoming packet
        int r = recvfrom(listen_socket_fd, (struct frame *)&recv_frame, MESSAGE_SIZE, 0, (struct sockaddr *)&client_addr, &client_addr_len);
        if (r < 0)
        {
            perror("Error receiving packet");
            break;
        }

        // Update ack number
        recv_frame.ack = recv_frame.seq;

        // Echo packet back
        int s = sendto(listen_socket_fd, (struct frame *)&recv_frame, MESSAGE_SIZE, 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
        if (s < 0)
        {
            perror("Error echoing packet");
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "server") == 0)
    {
        if (argc > 2)
            server_ip = argv[2];
        if (argc > 3)
            server_port = atoi(argv[3]);

        run_server();
    }
    else if (argc > 1 && strcmp(argv[1], "client") == 0)
    {
        if (argc > 2)
            server_ip = argv[2];
        if (argc > 3)
            server_port = atoi(argv[3]);
        if (argc > 4)
            num_client_threads = atoi(argv[4]);
        if (argc > 5)
            num_requests = atoi(argv[5]);

        run_client();
    }
    else
    {
        printf("Usage: %s <server|client> [server_ip server_port num_client_threads num_requests]\n", argv[0]);
    }

    return 0;
}
