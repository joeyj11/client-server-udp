#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

int main() {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in server_addr, client_addr_from, client_addr_to;
    struct packet buffer;
    socklen_t addr_size = sizeof(client_addr_from);
    int expected_seq_num = 0;
    int recv_len;
    struct packet ack_pkt;

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("Could not create send socket");
        return 1;
    }

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0) {
        perror("Could not create listen socket");
        return 1;
    }

    // Configure the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the server address
    if (bind(listen_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    // Configure the client address structure to which we will send ACKs
    memset(&client_addr_to, 0, sizeof(client_addr_to));
    client_addr_to.sin_family = AF_INET;
    client_addr_to.sin_addr.s_addr = inet_addr(LOCAL_HOST);
    client_addr_to.sin_port = htons(CLIENT_PORT_TO);

    // Open the target file for writing (always write to output.txt)
    FILE *fp = fopen("output.txt", "wb");

   // TODO: Receive file from the client and save it as output.txt
    ssize_t received_bytes;
    char collected_segments[8000][PAYLOAD_SIZE];
    int rec_flgs[8000] = {0};
    int total_coll = 0;
    int tail_len = 0;
    
    struct timeval timeout_data;
    timeout_data.tv_sec = 0;
    timeout_data.tv_usec = 500000;
    
    while(1){
        received_bytes = recvfrom(listen_sockfd, &buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr_from, &addr_size);

        if (received_bytes == 0) {
            break;
        }

        fd_set read_file_descriptors;
        FD_ZERO(&read_file_descriptors);
        FD_SET(listen_sockfd, &read_file_descriptors);

        int select_result = select(listen_sockfd + 1, &read_file_descriptors, NULL, NULL, &timeout_data);

        // Handle `select()` timeout: no data received in the given time frame.
        if (select_result == 0) {
            build_packet(&ack_pkt, 0, expected_seq_num, 0, 1, 1, "0");
            sendto(send_sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&client_addr_to, addr_size);
            continue;
                    
        }
        //Handle error in select
        else if (select_result == -1) {
            perror("Select error. Exiting...");
            fclose(fp);
            close(listen_sockfd);
            close(send_sockfd);
            return 1;
        }

        // Handle packets that are out of order but not a last packet case.
        if (buffer.seqnum < expected_seq_num && !(buffer.last == 1 && expected_seq_num - 1 == buffer.seqnum)){
            build_packet(&ack_pkt, 0, expected_seq_num, 0, 1, 1, "0");
            sendto(send_sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&client_addr_to, addr_size);
            continue;
        }         

        
        if (!rec_flgs[buffer.seqnum]) {
                    memcpy(collected_segments[buffer.seqnum], buffer.payload, buffer.length);
                    rec_flgs[buffer.seqnum] = 1;
                }
        
        if (buffer.seqnum > total_coll)
            total_coll = buffer.seqnum;
        
        if (expected_seq_num == buffer.seqnum) {
            while (rec_flgs[expected_seq_num] == 1)
                expected_seq_num++;
        }
            
            
        // Check if the received packet is the last one.
        printf("Received packet with seqnum: %d and last flag: %d and expected sequence number of: %d\n", buffer.seqnum, buffer.last, expected_seq_num);
        fflush(stdout);
        if (buffer.last == 1 && expected_seq_num - 1 == buffer.seqnum) {
            tail_len = buffer.length;
            build_packet(&ack_pkt, 0, expected_seq_num, buffer.last, 1, 1, "0"); // Assuming build_packet properly sets the last flag based on its argument
            sendto(send_sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&client_addr_to, addr_size);
        }
        build_packet(&ack_pkt, 0, expected_seq_num, 0, 1, 1, "0");
        sendto(send_sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&client_addr_to, addr_size);
    }
    
    for (int i = 0; i < total_coll; i++) {
        fwrite(collected_segments[i], 1, PAYLOAD_SIZE, fp);
    }


    // Write the final packet's data to the file if it wasn't a full-sized packet.
    if (tail_len > 0) { fwrite(collected_segments[total_coll], 1, tail_len, fp); }
    
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}


    
    