#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include "utils.h"


int main(int argc, char *argv[]) {
    clock_t start = clock();
    int listen_sockfd, send_sockfd;
    struct sockaddr_in client_addr, server_addr_to, server_addr_from;
    socklen_t addr_size = sizeof(server_addr_to);
    struct timeval tv;
    struct packet pkt;
    struct packet ack_pkt;
    char buffer[PAYLOAD_SIZE];
    unsigned short seq_num = 0;
    unsigned short ack_num = 0;
    char last = 0;
    char ack = 0;

    // read filename from command line argument
    if (argc != 2) {
        printf("Usage: ./client <filename>\n");
        return 1;
    }
    char *filename = argv[1];

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0) {
        perror("Could not create listen socket");
        return 1;
    }

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("Could not create send socket");
        return 1;
    }

    // Configure the server address structure to which we will send data
    memset(&server_addr_to, 0, sizeof(server_addr_to));
    server_addr_to.sin_family = AF_INET;
    server_addr_to.sin_port = htons(SERVER_PORT_TO);
    server_addr_to.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Configure the client address structure
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(CLIENT_PORT);
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the client address
    if (bind(listen_sockfd, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    // Open file for reading
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        perror("Error opening file");
        close(listen_sockfd);
        close(send_sockfd);
        return 1;
    }

    // TODO: Read from file, and initiate reliable data transfer to the server

    int total_length;
    fseek(fp, 0, SEEK_END);
    total_length = ftell(fp);
    int total_packets = total_length/PAYLOAD_SIZE + (total_length % PAYLOAD_SIZE != 0);

    fseek(fp, 0, SEEK_SET);

    ssize_t bytes_read;
    struct packet segments[total_packets];
    while((bytes_read = fread(buffer, 1, PAYLOAD_SIZE, fp)) > 0) {
        if (bytes_read < PAYLOAD_SIZE) {
            last = 1;
        }
        build_packet(&pkt, seq_num, ack_num, last, ack, bytes_read, (const char*) buffer);
        segments[seq_num] = pkt;
        seq_num++;
    }
    
    tv.tv_sec = 0;
    tv.tv_usec = 500000;
 
    int last_seq_num_sent = seq_num;
    ssize_t bytes_recv;
    seq_num = 0;
    float current_window = 5;
    int ssthreshold = 64;
    int duplicate_acknowledge = 0;
    int current_acknowledge = 0;
    int in_flight_packets = 0;
    int fast_retransmit = 0;
    fclose(fp);

    while(1){
        if (in_flight_packets < (int)current_window){
            for (int i = seq_num; i < seq_num + (int)current_window && i < total_packets; i++)
            {
                printf("Sending packet %d\n", i);
                sendto(send_sockfd, &segments[i], sizeof(segments[i]), 0, (struct sockaddr *)&server_addr_to, addr_size);
                in_flight_packets++;
                usleep(1000);// Add this header for usleep()
            }
        }

        fd_set read_file_descriptors;
        FD_ZERO(&read_file_descriptors);
        FD_SET(listen_sockfd, &read_file_descriptors);

        int select_result = select(listen_sockfd + 1, &read_file_descriptors, NULL, NULL, &tv);
        if (select_result == 0) { // A timeout has occured, check
            // if a timeout occurs, set the ssthreshold to half the current window size
            if (fast_retransmit == 1) {
                ssthreshold = current_window/2;
                current_window = 5;
                duplicate_acknowledge = 0;
            }
            else if ((int)current_window >= ssthreshold) { 
                ssthreshold = current_window/2;
                current_window = 5;
                duplicate_acknowledge = 0;
            }
            else { 
                ssthreshold = current_window/2;
                current_window = 5; 
                duplicate_acknowledge = 0;
            }
            in_flight_packets = 0;
            continue;
                    
        }
            // select error, exit out
        else if (select_result == -1) {
            fclose(fp);
            close(listen_sockfd);
            close(send_sockfd);
            perror("Select statement error. Exiting.");
            return 1;
        }

        // otherwise no timeout, read the bytes that we received
        bytes_recv = recvfrom(listen_sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&server_addr_from, &addr_size);
        if (bytes_recv > 0){
            printf("Ack_pkt seqnum %d, acknum %d, last %d, ack %d, length %d, payload %s\n", ack_pkt.seqnum, ack_pkt.acknum, ack_pkt.last, ack_pkt.ack, ack_pkt.length, ack_pkt.payload);
            fflush(stdout);

            // if the acknum is not equal to the current acknum, we want to update the window size
            if (ack_pkt.acknum != current_acknowledge){
                if (fast_retransmit == 1){
                    current_window = ssthreshold;
                    duplicate_acknowledge = 0;
                    fast_retransmit = 0;
                }
                else if ((int)current_window >= ssthreshold) { 
                    current_window += 5; 
                    duplicate_acknowledge = 0;
                }
                else{
                    current_window += 5; 
                }
                in_flight_packets -= (ack_pkt.acknum - current_acknowledge);
                current_acknowledge = ack_pkt.acknum;
                duplicate_acknowledge = 0;
            }
            else { duplicate_acknowledge++; }
            seq_num = ack_pkt.acknum;

            // if we have received 3 duplicate acks, we want to retransmit the packet
            if (duplicate_acknowledge == 3) {
                fast_retransmit = 1;
                ssthreshold = current_window/2;
                current_window = ssthreshold + 3;

                printf("Retransmitting packet %d\n", seq_num);
                sendto(send_sockfd, &segments[seq_num], sizeof(segments[seq_num]), 0, (struct sockaddr *)&server_addr_to, addr_size);
            }
            
            // if we have more than three duplicate ACKs, we want to MD the window size
            if (duplicate_acknowledge > 3){
                current_window = current_window/2;
            }
            // if we are on the last packet, we want to exit after it is sent:
            if (ack_pkt.acknum == last_seq_num_sent) {
                if (ack_pkt.last == 1) {
                    break; // Break out of the loop
                }
            }
        }
    }
    clock_t end = clock();
    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
    printf("Time spent: %f\n", time_spent);
    // Send packet for the server to shutdown if it doesn't receive it it's okay since we have a failsafe
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}