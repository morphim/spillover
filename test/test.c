#include <stdio.h>
#include "rudp.h"
#include "time.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#define SPO_RWND_SIZE (200 * 1024)
#define SPO_ITERATIONS_BEFORE_SLEEP 500

static void sleep(uint32_t msecs)
{
#ifdef _WIN32
    Sleep(msecs);
#else
    struct timespec ts;
    ts.tv_sec = msecs / 1000;
    ts.tv_nsec = (msecs % 1000) * 1000000;
    nanosleep(&ts, NULL);
#endif
}

void incoming_data(spo_host_t host, spo_connection_t connection, uint32_t data_size)
{
    static uint8_t buf[SPO_RWND_SIZE];
    static int32_t count = 0;
    static uint32_t start_time = 0;
    static uint32_t bytes_received = 0;

    if (start_time == 0)
        start_time = spo_time_current();

    bytes_received += spo_read(connection, buf, data_size);
    if (count++ == 10000)
    {
        uint32_t time_elapsed = spo_time_elapsed(start_time);
        if (time_elapsed > 0)
        {
            double speed = (double)bytes_received / time_elapsed * 1000;
            printf("===================== Average speed: %.0f bytes / sec\n", speed);
        }
        start_time = spo_time_current();
        bytes_received = 0;
        count = 0;
    }
}

void incoming_connection(spo_host_t host, spo_connection_t connection)
{
    printf("host %p: incoming connection\n", host);
}

void unable_to_connect(spo_host_t host, spo_connection_t connection)
{
    printf("host %p: unable to connect\n", host);
}

void connected(spo_host_t host, spo_connection_t connection)
{
    printf("host %p: connected\n", host);
}

void connection_lost(spo_host_t host, spo_connection_t connection)
{
    printf("host %p: connection was lost\n", host);
}

void logger(const char *message)
{
    static uint32_t time = 0;
    if (time == 0)
        time = spo_time_current();

    printf("%u %s\n", spo_time_elapsed(time), message);
}

int show_usage(char *filename)
{
    printf("Usage:\n");
    printf("%s <bind to address> [connect to address]\n", filename);

    return 0;
}

spo_bool_t get_ipv4_address_from_string(const char *address, spo_net_address_t *res_address)
{
    unsigned part1;
    unsigned part2;
    unsigned part3;
    unsigned part4;
    unsigned port = 0;

    if (sscanf(address, "%u.%u.%u.%u:%u", &part1, &part2, &part3, &part4, &port) < 4)
        return SPO_FALSE;

    if (part1 <= UINT8_MAX &&
        part2 <= UINT8_MAX &&
        part3 <= UINT8_MAX &&
        part4 <= UINT8_MAX &&
        port <= UINT16_MAX)
    {
        res_address->type = SPO_NET_SOCKET_TYPE_IPV4;
        res_address->address[0] = part1;
        res_address->address[1] = part2;
        res_address->address[2] = part3;
        res_address->address[3] = part4;
        res_address->port = port;

        return SPO_TRUE;
    }

    return SPO_FALSE;
}

int main(int argc, char **argv)
{
    spo_net_address_t bind_addr1;
    spo_net_address_t bind_addr2;
    spo_callbacks_t callbacks;
    spo_configuration configuration;
    spo_host_t host;
    spo_connection_t conn;
    uint8_t buf[10240];
    spo_bool_t client_mode = SPO_FALSE;
    uint32_t idle_count = 0;

    if (argc < 2)
        return show_usage(argv[0]);

    if (get_ipv4_address_from_string(argv[1], &bind_addr1) == SPO_FALSE)
        return 1;

    if (argc > 2) /* client mode */
    {
        if (get_ipv4_address_from_string(argv[2], &bind_addr2) == SPO_FALSE)
            return 1;
        client_mode = SPO_TRUE;
    }

    if (!spo_init())
        return 1;

    spo_set_logger(logger);

    callbacks.connected = connected;
    callbacks.unable_to_connect = unable_to_connect;
    callbacks.incoming_data = incoming_data;
    callbacks.incoming_connection = incoming_connection;
    callbacks.connection_lost = connection_lost;

    configuration.initial_cwnd_in_packets = 2;
    configuration.cwnd_on_timeout_in_packets = 2;
    configuration.min_ssthresh_in_packets = 4;
    configuration.max_cwnd_inc_on_slowstart_in_packets = 50;
    configuration.duplicate_acks_for_retransmit = 2;
    configuration.ssthresh_factor_on_timeout_percent = 50;
    configuration.ssthresh_factor_on_loss_percent = 70;

    configuration.connection_buf_size = SPO_RWND_SIZE;
    configuration.socket_buf_size = 1048576 * 4;
    configuration.max_connections = 500;
    configuration.connection_timeout = 8000;
    configuration.ping_interval = 1500;
    configuration.connect_retransmission_timeout = 2000;
    configuration.max_connect_attempts = 3;
    configuration.accept_retransmission_timeout = 1000;
    configuration.max_accepted_attempts = 2;
    configuration.data_retransmission_timeout = 600;
    configuration.skip_packets_before_acknowledgement = 0;
    configuration.max_consecutive_acknowledges = 10;

    host = spo_new_host(&bind_addr1, &configuration, &callbacks);
    if (host == NULL)
    {
        printf("can't create a new host!\n");
        return 1;
    }

    if (client_mode)
    {
        conn = spo_new_connection(host, &bind_addr2);
        if (conn == NULL)
        {
            printf("can't create a new connection!\n");
            return 1;
        }
    }

    while (1)
    {
        if (spo_make_progress(host))
            idle_count = 0;
        else if (++idle_count > SPO_ITERATIONS_BEFORE_SLEEP)
        {
            sleep(2);
            idle_count = 0;
        }

        if (client_mode)
        {
            if (spo_get_connection_state(conn) == SPO_CONNECTION_STATE_CONNECTED)
                spo_send(conn, buf, sizeof(buf));
        }
    }

    spo_close_host(host);
    spo_shutdown();
    return 0;
}
