#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>

#define ARRAY_LENGTH(x) (sizeof(x) / sizeof(*(x)))
#define ARRAY_END(x) ((x) + ARRAY_LENGTH(x))

#define RECV_DELAY    1000
#define MAX_QUEUED_PACKETS 256
#define RECV_BUF_SIZE 2048

#define LOCALHOST ((uint32_t)0x7F000001)

struct QueuedPacket
{
    struct timeval send_time;
    struct sockaddr_in send_to;
    int len;
    uint8_t packet_data[RECV_BUF_SIZE];
};

struct QueuedPacket queued_packets[MAX_QUEUED_PACKETS];

void parse_args(int argc, char *argv[], uint16_t *port, uint16_t *server_port, uint32_t *client_server_ms, uint32_t *server_client_ms)
{
    if (argc != 5)
    {
        printf("Usage: ./netfk <port> <server_port> <ms_to_server> <ms_to_client>\n");
        exit(1);
    }

    *port = strtoul(argv[1], NULL, 10);
    *server_port = strtoul(argv[2], NULL, 10);
    *client_server_ms = strtoul(argv[3], NULL, 10);
    *server_client_ms = strtoul(argv[4], NULL, 10);
}

int main(int argc, char *argv[])
{
    uint16_t port;
    uint16_t server_port;
    uint32_t client_server_ms;
    uint32_t server_client_ms;
    parse_args(argc, argv, &port, &server_port, &client_server_ms, &server_client_ms);

    struct timeval client_server_latency;
    client_server_latency.tv_sec = 0;
    client_server_latency.tv_usec = 1000 * client_server_ms;

    struct timeval server_client_latency;
    server_client_latency.tv_sec = 0;
    server_client_latency.tv_usec = 1000 * server_client_ms;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    assert(sock != -1);

    // bind
    {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;

        int err = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
        assert(err != -1);
    }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(server_port);
    server.sin_addr.s_addr = htonl(LOCALHOST);

    struct QueuedPacket *send_next = NULL;
    struct QueuedPacket *recv_next = queued_packets;

    // Forward first packet to get client addr
    struct sockaddr_in client;
    {
        struct timeval now;
        gettimeofday(&now, NULL);

        socklen_t client_len = sizeof(client);
        recv_next->len = recvfrom(sock, recv_next->packet_data, sizeof(recv_next->packet_data), 0, (struct sockaddr*)&client, &client_len);
        assert(recv_next->len != -1);
        assert(client_len == sizeof(client));

        recv_next->send_to = server;
        timeradd(&now, &client_server_latency, &recv_next->send_time);
        
        send_next = recv_next;
        recv_next++;
        if (recv_next == ARRAY_END(queued_packets))
        {
            recv_next = queued_packets;
        }
    }
    
    printf("client port: %d\n", ntohs(client.sin_port));

    while (1)
    {
        struct timeval now;
        gettimeofday(&now, NULL);

        struct sockaddr_in sender;
        socklen_t sender_len = sizeof(sender);
        recv_next->len = recvfrom(sock, recv_next->packet_data, sizeof(recv_next->packet_data), MSG_DONTWAIT, (struct sockaddr*)&sender, &sender_len);

        if (recv_next->len == -1)
        {
            assert(errno == EAGAIN || errno == EWOULDBLOCK);
            usleep(RECV_DELAY);
        }

        assert(sender_len == sizeof(sender));
        if (sender.sin_port == server.sin_port)
        {
            recv_next->send_to = client;
            timeradd(&now, &server_client_latency, &recv_next->send_time);
        }
        else if (sender.sin_port == client.sin_port)
        {
            recv_next->send_to = server;
            timeradd(&now, &client_server_latency, &recv_next->send_time);
        }

        if (!send_next) send_next = recv_next;
        recv_next++;
        if (recv_next == ARRAY_END(queued_packets))
        {
            recv_next = queued_packets;
        }
        
        if (timercmp(&now, &send_next->send_time, >))
        {
            int send_len = sendto(sock, send_next->packet_data, send_next->len, 0, (struct sockaddr*)&send_next->send_to, sizeof(send_next->send_to));
            assert(send_len == send_next->len);
            send_next++;
            if (send_next == ARRAY_END(queued_packets)) send_next = queued_packets;
            if (send_next == recv_next) send_next = NULL;
        }
    }

    return 0;
}
