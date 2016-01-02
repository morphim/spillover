#include <stdio.h>
#include "rudp.h"
#include "time.h"

#ifdef WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#define SPO_TEST_CLIENT
#define SPO_RWND_SIZE (200 * 1024)
#define SPO_ITERATIONS_BEFORE_SLEEP 500

static void sleep(uint32_t msecs)
{
#ifdef WIN32
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

int main(int argc, char **argv)
{
    spo_net_address_t bind_addr1;
    spo_net_address_t bind_addr2;
    spo_callbacks_t callbacks;
    spo_configuration configuration;
    spo_host_t host;
#ifdef SPO_TEST_CLIENT
    spo_connection_t conn;
    uint8_t buf[10240];
#endif
    uint32_t idle_count = 0;

    if (!spo_init())
        return 1;

    spo_set_logger(logger);

    bind_addr1.type = SPO_NET_SOCKET_TYPE_IPV4;
    bind_addr1.address[0] = 127;
    bind_addr1.address[1] = 0;
    bind_addr1.address[2] = 0;
    bind_addr1.address[3] = 1;
    bind_addr1.port = 5000;

    bind_addr2.type = SPO_NET_SOCKET_TYPE_IPV4;
    bind_addr2.address[0] = 127;
    bind_addr2.address[1] = 0;
    bind_addr2.address[2] = 0;
    bind_addr2.address[3] = 1;
    bind_addr2.port = 6000;

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

#ifdef SPO_TEST_CLIENT
    host = spo_new_host(&bind_addr1, &configuration, &callbacks);
    if (host == NULL)
    {
        printf("can't create a new host!\n");
        return 1;
    }
    conn = spo_new_connection(host, &bind_addr2);
    if (conn == NULL)
    {
        printf("can't create a new connection!\n");
        return 1;
    }
#else
    host = spo_new_host(&bind_addr2, &configuration, &callbacks);
    if (host == NULL)
    {
        printf("can't create a new host!\n");
        return 1;
    }
#endif

    while (1)
    {
        if (spo_make_progress(host))
            idle_count = 0;
        else if (++idle_count > SPO_ITERATIONS_BEFORE_SLEEP)
        {
            sleep(2);
            idle_count = 0;
        }

#ifdef SPO_TEST_CLIENT
        if (spo_get_connection_state(conn) == SPO_CONNECTION_STATE_CONNECTED)
            spo_send(conn, buf, sizeof(buf));
#endif
    }

    spo_close_host(host);
    spo_shutdown();
    return 0;
}
