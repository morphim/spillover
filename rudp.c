/*
Copyright (c) 2015 drugaddicted - c17h19no3 AT openmailbox DOT org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <memory.h>
#include "rudp.h"
#include "list.h"
#include "index.h"
#include "packet.h"
#include "time.h"
#include "random.h"

#define SPO_HEADER_SIZE(acks_count) (sizeof(spo_packet_header_t) + (acks_count) * sizeof(spo_packet_header_sack_t))
#define SPO_MAX_PAYLOAD_SIZE (SPO_NET_MAX_PACKET_SIZE - sizeof(spo_packet_header_t))

#define SPO_MIN(x, y) ((x) < (y) ? (x) : (y))
#define SPO_MAX(x, y) ((x) > (y) ? (x) : (y))

/* unsigned arithmetic does all the magic */
#define SPO_WRAPPED_LESS(a, b)       ((int32_t)((a)-(b)) < 0)
#define SPO_WRAPPED_LESS_EQ(a, b)    ((int32_t)((a)-(b)) <= 0)
#define SPO_WRAPPED_GREATER(a, b)    ((int32_t)((a)-(b)) > 0)
#define SPO_WRAPPED_GREATER_EQ(a, b) ((int32_t)((a)-(b)) >= 0)

#define SPO_WRAPPED_MIN(a, b) (SPO_WRAPPED_LESS((a), (b)) ? (a) : (b))
#define SPO_WRAPPED_MAX(a, b) (SPO_WRAPPED_GREATER((a), (b)) ? (a) : (b))

typedef struct spo_host_data spo_host_data_t;
typedef struct spo_connection_data spo_connection_data_t;

typedef enum
{
    SPO_RECOVERY_OFF,
    SPO_RECOVERY_BY_LOSS,
    SPO_RECOVERY_BY_TIMEOUT
} spo_recovery_mode_t;

struct spo_host_data
{
    spo_net_socket_t socket;
    spo_configuration configuration;
    spo_callbacks_t callbacks;
    spo_list_t connections;
    spo_list_t started_connections; /* connections in SPO_CONNECTION_STATE_CONNECT_STARTED state */
    spo_list_t incoming_connections; /* connections in SPO_CONNECTION_STATE_CONNECT_RECEIVED state */
    spo_connection_data_t *connections_by_ports[UINT16_MAX];
};

struct spo_connection_data
{
    spo_host_data_t *host;
    spo_connection_state_t state;
    spo_net_address_t remote_address;
    uint32_t created_time;
    uint16_t local_port;
    uint16_t remote_port;
    uint8_t connect_attempts;

    uint8_t *rcv_buf;
    spo_index_t rcv_packets; /* received packets descriptors */
    uint32_t rcv_bytes_ready; /* bytes ready to be read */
    uint32_t rcv_start_seq; /* start of the receive buffer */
    uint32_t rcv_last_packet_time; /* last received packet time */

    uint8_t *snd_buf;
    uint32_t snd_buf_size; /* size of the allocated buffer */
    spo_index_t snd_acked_packets; /* packets acked by the receiver */
    uint32_t snd_buf_bytes; /* total bytes in the send buffer */
    uint32_t snd_start_seq; /* start of the send buffer */
    uint32_t snd_next_seq; /* first seq for the new data to send */
    uint32_t snd_last_packet_time; /* last sent packet time */
    uint8_t snd_mandatory_packets; /* count of mandatory packets */

    /* variables for the congestion control algorithm */

    uint8_t snd_duplicate_acks; /* duplicate acks counter */
    uint8_t snd_mandatory_packets_skipped; /* skipped packets */
    uint8_t snd_recovery_mode; /* indicates that sender is in recovery mode */
    uint32_t snd_cwnd_bytes; /* size of the congestion window */
    uint32_t snd_ssthresh_bytes; /* slow start threshold */
    uint32_t snd_last_data_sent_time; /* last data transmission time */
    uint32_t snd_retransmit_next_seq; /* next seq to retransmit */
    uint32_t snd_recovery_point_seq; /* recovery mode is up to specified seq */
    uint32_t snd_retransmit_rescue_seq; /* seq for the rescue retransmission */
};

static logger_ptr_t spo_logger;

static uint32_t spo_internal_send_next_connection_data(spo_connection_data_t *connection, uint32_t cwnd_bytes);
static uint32_t spo_internal_transmit_packet(spo_connection_data_t *connection, uint32_t seq);

static uint16_t spo_internal_swap_2bytes(uint16_t src)
{
#ifdef SPO_BIGENDIAN_PLATFORM
    return src;
#else
    return (src << 8) | (src >> 8);
#endif
}

static uint32_t spo_internal_swap_4bytes(uint32_t src)
{
#ifdef SPO_BIGENDIAN_PLATFORM
    return src;
#else
    return ((src & 0x000000FF) << 24) |
           ((src & 0x0000FF00) << 8)  |
           ((src & 0x00FF0000) >> 8)  |
           ((src & 0xFF000000) >> 24);
#endif
}

#ifdef _DEBUG

static void spo_internal_log(const char *fmt, ...)
{
    char buf[1024];
    va_list list;

    if (spo_logger != NULL)
    {
        va_start(list, fmt);
        vsnprintf(buf, sizeof(buf), fmt, list);
        va_end (list);

        spo_logger(buf);
    }
}

#define SPO_LOG(...) spo_internal_log(__VA_ARGS__)

#else

#define SPO_LOG(...)

#endif

/* congestion control */

static void spo_internal_increase_cwnd_by_bytes(spo_connection_data_t *connection, uint32_t bytes)
{
    connection->snd_cwnd_bytes += bytes;

    if (connection->snd_cwnd_bytes > connection->host->configuration.connection_buf_size)
        connection->snd_cwnd_bytes = connection->host->configuration.connection_buf_size;
}

static void spo_internal_decrease_cwnd_by_bytes(spo_connection_data_t *connection, uint32_t bytes)
{
    if (connection->snd_cwnd_bytes >= bytes)
        connection->snd_cwnd_bytes -= bytes;
    else
        connection->snd_cwnd_bytes = 0;
}

static void spo_internal_handle_connection_init(spo_connection_data_t *connection)
{
    connection->snd_last_data_sent_time = spo_time_current();
    connection->snd_cwnd_bytes = SPO_MAX_PAYLOAD_SIZE * connection->host->configuration.initial_cwnd_in_packets;
    connection->snd_ssthresh_bytes = connection->host->configuration.connection_buf_size;
    connection->snd_recovery_point_seq = connection->snd_start_seq;
    connection->snd_retransmit_rescue_seq = connection->snd_start_seq;
    connection->snd_retransmit_next_seq = connection->snd_start_seq;
}

/* for each sent packet */
static void spo_internal_handle_next_data_sent(spo_connection_data_t *connection)
{
    /* reset retransmission timer */
    connection->snd_last_data_sent_time = spo_time_current();
}

/* for each received packet */
static void spo_internal_handle_new_data_received(spo_connection_data_t *connection)
{
    if (connection->snd_mandatory_packets < connection->host->configuration.max_consecutive_acknowledges)
    {
        if (connection->snd_mandatory_packets == 0)
        {
            connection->snd_mandatory_packets = 1; /* send at least one confirming packet */
            connection->snd_mandatory_packets_skipped = 0;
        }
        else if (connection->snd_mandatory_packets_skipped >= connection->host->configuration.skip_packets_before_acknowledgement)
        {
            ++connection->snd_mandatory_packets;
            connection->snd_mandatory_packets_skipped = 0;
        }
        else
            ++connection->snd_mandatory_packets_skipped;
    }
}

/* for each received packet */
static void spo_internal_handle_unknown_ack(spo_connection_data_t *connection, uint32_t ack)
{
    /* check for duplicate ack */
    /* ack is considered a duplicate when the sender has
       outstanding data and ack is equal to 'snd_start_seq' */
    if (ack == connection->snd_start_seq && SPO_WRAPPED_LESS(ack, connection->snd_next_seq))
    {
        if (connection->snd_acked_packets.length > 0) /* acks list is not empty */
        {
            /* duplicate ack received */
            if (connection->snd_duplicate_acks < UINT8_MAX)
                ++connection->snd_duplicate_acks;

            if (connection->snd_recovery_mode != SPO_RECOVERY_OFF)
            {
                /* increase congestion window by bytes that have left the network */
                spo_internal_increase_cwnd_by_bytes(connection, SPO_MAX_PAYLOAD_SIZE);

                SPO_LOG("REC, one more DUPACK, CWND increased to %u", connection->snd_cwnd_bytes);
            }
        }
    }
}

static uint32_t spo_internal_recovery_retransmit_by_seq(spo_connection_data_t *connection, uint32_t seq)
{
    /* don't retransmit if congestion window isn't big enough */
    if (connection->snd_cwnd_bytes >= SPO_MAX_PAYLOAD_SIZE)
    {
        uint32_t bytes_sent = spo_internal_transmit_packet(connection, seq);
        if (bytes_sent > 0)
        {
            spo_internal_decrease_cwnd_by_bytes(connection, bytes_sent); /* update congestion window */

            /* 'snd_retransmit_next_seq' always points to the next data to retransmit */
            if (SPO_WRAPPED_LESS(connection->snd_retransmit_next_seq, seq + bytes_sent))
                connection->snd_retransmit_next_seq = seq + bytes_sent;

            SPO_LOG("REC, retransmitted %u bytes, SEQ %u, decrease CWND to %u", bytes_sent, seq, connection->snd_cwnd_bytes);
            return bytes_sent;
        }
    }

    return 0;
}

static uint32_t spo_internal_recovery_retransmit_next_data(spo_connection_data_t *connection)
{
    spo_index_item_t *send_after_item;
    uint32_t seq;

    if (connection->snd_acked_packets.length == 0) /* acks list is empty */
        return 0;

    seq = SPO_WRAPPED_MAX(connection->snd_retransmit_next_seq, connection->snd_start_seq);

    /* find position of the seq */
    send_after_item = spo_index_find_pos_by_key(&connection->snd_acked_packets, seq);

    if (send_after_item == NULL)
    {
        /* before the head */
        return spo_internal_recovery_retransmit_by_seq(connection, seq);
    }

    if (SPO_INDEX_VALID(&connection->snd_acked_packets,
        SPO_INDEX_NEXT(&connection->snd_acked_packets, send_after_item))) /* not a last item */
    {
        /* before the tail */
        spo_packet_desc_t *packet_desc = (spo_packet_desc_t *)send_after_item->data;
        uint32_t packet_end = packet_desc->start + packet_desc->size;

        if (SPO_WRAPPED_LESS(seq, packet_end))
            seq = packet_end;

        return spo_internal_recovery_retransmit_by_seq(connection, seq);
    }

    return 0;
}

static uint32_t spo_internal_recovery_send_next_data(spo_connection_data_t *connection)
{
    /* send next data if congestion window allows this */
    if (connection->snd_cwnd_bytes >= SPO_MAX_PAYLOAD_SIZE)
    {
        uint32_t bytes_sent = spo_internal_send_next_connection_data(connection, connection->snd_buf_bytes);
        if (bytes_sent > 0)
        {
            spo_internal_decrease_cwnd_by_bytes(connection, bytes_sent); /* update congestion window */

            SPO_LOG("REC, next data sent, decrease CWND to %u", connection->snd_cwnd_bytes);
            return bytes_sent;
        }
    }

    return 0;
}

static void spo_internal_update_ssthresh(spo_connection_data_t *connection, uint32_t ssthresh_factor_percent)
{
    uint32_t bytes_not_acked = connection->snd_next_seq - connection->snd_start_seq;
    uint32_t ssthresh_in_bytes = bytes_not_acked * ssthresh_factor_percent / 100;

    /* update slow start threshold */
    connection->snd_ssthresh_bytes = SPO_MAX(ssthresh_in_bytes,
        connection->host->configuration.min_ssthresh_in_packets * SPO_MAX_PAYLOAD_SIZE);
}

static spo_bool_t spo_internal_initiate_recovery_mode(spo_connection_data_t *connection, spo_recovery_mode_t mode)
{
    switch (mode)
    {
    case SPO_RECOVERY_BY_LOSS:
        if (connection->snd_recovery_mode == SPO_RECOVERY_OFF)
            spo_internal_update_ssthresh(connection, connection->host->configuration.ssthresh_factor_on_loss_percent);

        /* update congestion window */
        connection->snd_cwnd_bytes = SPO_MAX(connection->snd_ssthresh_bytes,
            connection->snd_duplicate_acks * SPO_MAX_PAYLOAD_SIZE);
        break;
    case SPO_RECOVERY_BY_TIMEOUT:
        if (connection->snd_recovery_mode == SPO_RECOVERY_OFF)
            spo_internal_update_ssthresh(connection, connection->host->configuration.ssthresh_factor_on_timeout_percent);

        /* reset congestion window */
        connection->snd_cwnd_bytes = connection->host->configuration.cwnd_on_timeout_in_packets * SPO_MAX_PAYLOAD_SIZE;
        break;
    }

    /* reset duplicate acks counter */
    connection->snd_duplicate_acks = 0;

    /* initiate recovery mode */
    connection->snd_recovery_mode = mode;
    connection->snd_recovery_point_seq = connection->snd_next_seq;
    connection->snd_retransmit_rescue_seq = connection->snd_start_seq;
    connection->snd_retransmit_next_seq = connection->snd_start_seq;

    SPO_LOG("ENTER REC, point is %u, set CWND to %u, set SSTHRESH to %u",
        connection->snd_recovery_point_seq, connection->snd_cwnd_bytes, connection->snd_ssthresh_bytes);

    /* retransmit first packet */
    if (spo_internal_recovery_retransmit_next_data(connection) > 0)
        return SPO_TRUE;

    return spo_internal_recovery_send_next_data(connection) > 0;
}

static void spo_internal_terminate_recovery_mode(spo_connection_data_t *connection)
{
    switch (connection->snd_recovery_mode)
    {
    case SPO_RECOVERY_BY_LOSS:
        /* restore congestion window */
        connection->snd_cwnd_bytes = connection->snd_ssthresh_bytes;
        SPO_LOG("EXIT REC, set CWND to %u, SSTHRESH is %u", connection->snd_cwnd_bytes, connection->snd_ssthresh_bytes);
        break;
    case SPO_RECOVERY_BY_TIMEOUT:
        /* enter slow start mode */
        connection->snd_cwnd_bytes = connection->host->configuration.cwnd_on_timeout_in_packets * SPO_MAX_PAYLOAD_SIZE;
        SPO_LOG("EXIT RTO REC, CWND is %u, SSTHRESH is %u", connection->snd_cwnd_bytes, connection->snd_ssthresh_bytes);
        break;
    }

    /* terminate recovery mode */
    connection->snd_recovery_mode = SPO_RECOVERY_OFF;
}

static spo_bool_t spo_internal_initiate_slowstart_by_timeout(spo_connection_data_t *connection)
{
    if (connection->snd_recovery_mode == SPO_RECOVERY_OFF)
        spo_internal_update_ssthresh(connection, connection->host->configuration.ssthresh_factor_on_timeout_percent);

    /* update congestion window */
    connection->snd_cwnd_bytes = connection->host->configuration.cwnd_on_timeout_in_packets * SPO_MAX_PAYLOAD_SIZE;

    /* reset duplicate acks counter */
    connection->snd_duplicate_acks = 0;

    SPO_LOG("ENTER SSTART, SWND is %u, set CWND to %u, set SSTHRESH to %u",
        connection->snd_start_seq, connection->snd_cwnd_bytes, connection->snd_ssthresh_bytes);

    /* retransmit first packet */
    /* we don't know about lost packets, so this packet is like a rescue packet without recovery mode */
    if (spo_internal_transmit_packet(connection, connection->snd_start_seq) > 0)
    {
        SPO_LOG("retrasmitted from SEQ %u", connection->snd_start_seq);
        return SPO_TRUE;
    }

    return SPO_FALSE;
}

/* for each received packet */
static void spo_internal_handle_sent_data_acknowledged(spo_connection_data_t *connection, uint32_t bytes_sent)
{
    if (connection->snd_recovery_mode != SPO_RECOVERY_OFF)
    {
        /* check whether recovery point has reached */
        if (SPO_WRAPPED_LESS(connection->snd_start_seq, connection->snd_recovery_point_seq))
        {
            /* still in recovery mode */
            if (bytes_sent >= SPO_MAX_PAYLOAD_SIZE)
            {
                spo_internal_increase_cwnd_by_bytes(connection, SPO_MAX_PAYLOAD_SIZE);
                SPO_LOG("REC, received ACK for %u bytes, CWND increased to %u", bytes_sent, connection->snd_cwnd_bytes);
            }
        }
        else
        {
            if (connection->snd_acked_packets.length > 0) /* acks list is not empty */
            {
                /* new data are lost */
                spo_internal_initiate_recovery_mode(connection, connection->snd_recovery_mode);
            }
            else
            {
                /* the lost data are fully restored now */
                spo_internal_terminate_recovery_mode(connection);
            }
        }
    }
    else
    {
        if (connection->snd_cwnd_bytes < connection->snd_ssthresh_bytes)
        {
            /* slow start */
            uint32_t max_cwnd_increment_in_bytes =
                connection->host->configuration.max_cwnd_inc_on_slowstart_in_packets * SPO_MAX_PAYLOAD_SIZE;

            spo_internal_increase_cwnd_by_bytes(connection, SPO_MIN(bytes_sent, max_cwnd_increment_in_bytes));
            SPO_LOG("SLOW START, increase CWND to %u", connection->snd_cwnd_bytes);
        }
        else
        {
            /* congestion avoidance */
            spo_internal_increase_cwnd_by_bytes(connection,
                SPO_MAX_PAYLOAD_SIZE * SPO_MAX_PAYLOAD_SIZE / connection->snd_cwnd_bytes);
            SPO_LOG("CONGESTION AVOIDANCE, increase CWND to %u", connection->snd_cwnd_bytes);
        }
    }

    /* reset duplicate acknowledges counter */
    connection->snd_duplicate_acks = 0;
    /* reset retransmission timer */
    connection->snd_last_data_sent_time = spo_time_current();
}

static spo_bool_t spo_internal_recovery_data_transmission(spo_connection_data_t *connection)
{
    /* first, try to retransmit next lost data */
    if (spo_internal_recovery_retransmit_next_data(connection) > 0)
        return SPO_TRUE;

    if (connection->snd_duplicate_acks >= connection->host->configuration.duplicate_acks_for_retransmit)
    {
        if (SPO_WRAPPED_LESS(connection->snd_retransmit_rescue_seq, connection->snd_retransmit_next_seq))
        {
            /* it seems like some retransmitted packets are lost */
            /* retransmit first packet because we have received max duplicate acks */
            /* retransmit first segment before next data transmission as it is more robust to the packet loss */
            if (spo_internal_recovery_retransmit_by_seq(connection, connection->snd_start_seq) > 0)
            {
                SPO_LOG("REC, no data to retransmit, so first segment was retransmitted");
                /* packet is sent, disable rescue retransmissions for a while */
                /* use 'snd_retransmit_next_seq' instead of 'snd_recovery_point_seq' as
                   it shows better results under high loss rates */
                connection->snd_retransmit_rescue_seq = connection->snd_retransmit_next_seq;
                /* reset duplicate acks counter */
                connection->snd_duplicate_acks = 0;
                return SPO_TRUE;
            }
        }
    }

    /* don't use limited transmit as in the recovery mode
       each duplicate ack inflates cwnd, so next data are sent as expected */

    return spo_internal_recovery_send_next_data(connection) > 0;
}

static spo_bool_t spo_internal_data_transmission(spo_connection_data_t *connection)
{
    if (connection->snd_duplicate_acks >= connection->host->configuration.duplicate_acks_for_retransmit)
    {
        /* it seems like some packets are lost */
        return spo_internal_initiate_recovery_mode(connection, SPO_RECOVERY_BY_LOSS);
    }

    if (connection->snd_duplicate_acks > 0)
    {
        /* limited transmit */
        if (spo_internal_send_next_connection_data(connection, connection->snd_cwnd_bytes +
            connection->snd_duplicate_acks * SPO_MAX_PAYLOAD_SIZE) > 0)
        {
            SPO_LOG("transmitted next data (CWND increased by %u DUPACKs)", connection->snd_duplicate_acks);
            return SPO_TRUE;
        }

        return SPO_FALSE;
    }

    return spo_internal_send_next_connection_data(connection, connection->snd_cwnd_bytes) > 0;
}

static spo_bool_t spo_internal_process_retransmission_timer(spo_connection_data_t *connection)
{
    if (spo_time_elapsed(connection->snd_last_data_sent_time) >= connection->host->configuration.data_retransmission_timeout)
    {
        /* reset retransmission timer */
        connection->snd_last_data_sent_time = spo_time_current();

        if (connection->snd_recovery_mode != SPO_RECOVERY_OFF)
        {
            /* RTO during recovery mode indicates that we can't restore data on the receiver */
            /* acks list can be empty here if the last packet is lost */
            /* re-enter recovery mode */
            return spo_internal_initiate_recovery_mode(connection, SPO_RECOVERY_BY_TIMEOUT);
        }

        if (connection->snd_acked_packets.length > 0) /* acks list is not empty */
        {
            /* receiver has holes in the data, so try to restore them */
            return spo_internal_initiate_recovery_mode(connection, SPO_RECOVERY_BY_TIMEOUT);
        }

        return spo_internal_initiate_slowstart_by_timeout(connection);
    }

    return SPO_FALSE;
}

static spo_bool_t spo_internal_handle_data_transmission(spo_connection_data_t *connection)
{
    /* we are trying to send only one packet per call */

    if (connection->snd_recovery_mode != SPO_RECOVERY_OFF)
    {
        if (spo_internal_recovery_data_transmission(connection))
            return SPO_TRUE;
    }
    else
    {
        if (spo_internal_data_transmission(connection))
            return SPO_TRUE;
    }

    /* check RTO as a last step to be sure that current data are sent */
    if (spo_internal_process_retransmission_timer(connection))
        return SPO_TRUE;

    return SPO_FALSE;
}

/* events */

static void spo_internal_fire_connected_event(spo_connection_data_t *connection)
{
    if (connection->host->callbacks.connected != NULL)
        connection->host->callbacks.connected(connection->host, connection);
}

static void spo_internal_fire_unable_to_connect_event(spo_connection_data_t *connection)
{
    if (connection->host->callbacks.unable_to_connect != NULL)
        connection->host->callbacks.unable_to_connect(connection->host, connection);
}

static void spo_internal_fire_incoming_connection_event(spo_connection_data_t *connection)
{
    if (connection->host->callbacks.incoming_connection != NULL)
        connection->host->callbacks.incoming_connection(connection->host, connection);
}

static void spo_internal_fire_connection_lost_event(spo_connection_data_t *connection)
{
    if (connection->host->callbacks.connection_lost != NULL)
        connection->host->callbacks.connection_lost(connection->host, connection);
}

static void spo_internal_fire_incoming_data_event(spo_connection_data_t *connection, uint32_t data_size)
{
    if (connection->host->callbacks.incoming_data != NULL)
        connection->host->callbacks.incoming_data(connection->host, connection, data_size);
}

/* connection search */

static spo_connection_data_t *spo_internal_find_started_connection(spo_host_data_t *host, const spo_net_address_t *remote_address)
{
    spo_connection_data_t *connection;
    spo_list_item_t *current = SPO_LIST_FIRST(&host->started_connections);

    while (SPO_LIST_VALID(&host->started_connections, current))
    {
        connection = (spo_connection_data_t *)current->data;

        if (spo_net_equal_addresses(&connection->remote_address, remote_address))
            return connection;

        current = SPO_LIST_NEXT(&host->started_connections, current);
    }

    return NULL;
}

static spo_connection_data_t *spo_internal_find_active_connection(spo_host_data_t *host,
    const spo_net_address_t *remote_address, uint16_t remote_port)
{
    spo_connection_data_t *connection;
    spo_list_item_t *current = SPO_LIST_FIRST(&host->connections);

    while (SPO_LIST_VALID(&host->connections, current))
    {
        connection = (spo_connection_data_t *)current->data;

        if (connection->remote_port == remote_port && spo_net_equal_addresses(&connection->remote_address, remote_address))
            return connection;

        current = SPO_LIST_NEXT(&host->connections, current);
    }

    return NULL;
}

static spo_connection_data_t *spo_internal_find_oldest_incoming_connection(spo_host_data_t *host)
{
    spo_connection_data_t *connection;
    spo_connection_data_t *oldest = NULL;
    spo_list_item_t *current = SPO_LIST_FIRST(&host->incoming_connections);

    while (SPO_LIST_VALID(&host->incoming_connections, current))
    {
        connection = (spo_connection_data_t *)current->data;

        if (oldest == NULL)
            oldest = connection;
        else if (SPO_WRAPPED_LESS(connection->created_time, oldest->created_time))
            oldest = connection;

        current = SPO_LIST_NEXT(&host->incoming_connections, current);
    }

    return oldest;
}

/* data transmission over the network */

static void spo_internal_pack_acks(uint8_t *data, const spo_packet_desc_t *acks_list, uint16_t acks_count)
{
    spo_packet_header_sack_t *ack;
    uint16_t count = 0;

    while (count < acks_count)
    {
        ack = (spo_packet_header_sack_t *)data;
        ack->start = spo_internal_swap_4bytes(acks_list[count].start);
        ack->size = spo_internal_swap_4bytes(acks_list[count].size);

        data += sizeof(spo_packet_header_sack_t);
        ++count;
    }
}

static void spo_internal_unpack_acks(spo_packet_desc_t *acks_list, const uint8_t *data, uint32_t data_size, uint16_t max_acks)
{
    spo_packet_header_sack_t *ack;
    uint16_t count = 0;

    while (count < max_acks && data_size >= sizeof(spo_packet_header_sack_t))
    {
        ack = (spo_packet_header_sack_t *)data;

        acks_list[count].start = spo_internal_swap_4bytes(ack->start);
        acks_list[count].size = spo_internal_swap_4bytes(ack->size);

        data += sizeof(spo_packet_header_sack_t);
        data_size -= sizeof(spo_packet_header_sack_t);
        ++count;
    }
}

static uint16_t spo_internal_get_acks(spo_packet_desc_t *acks_list, spo_connection_data_t *connection)
{
    spo_packet_desc_t *packet;
    uint16_t count = 0;
    spo_index_item_t *current = SPO_INDEX_FIRST(&connection->rcv_packets);

    while (SPO_INDEX_VALID(&connection->rcv_packets, current) && count < SPO_PACKET_MAX_SACKS)
    {
        packet = (spo_packet_desc_t *)current->data;

        acks_list[count].start = packet->start;
        acks_list[count].size = packet->size;

        ++count;
        current = SPO_INDEX_NEXT(&connection->rcv_packets, current);
    }

    return count;
}

static uint32_t spo_internal_send_packet(spo_connection_data_t *connection, uint32_t seq, const uint8_t *data, uint32_t data_size)
{
    uint8_t packet_data[SPO_NET_MAX_PACKET_SIZE];
    spo_packet_desc_t acks_list[SPO_PACKET_MAX_SACKS];
    uint16_t acks_count;
    uint32_t bytes_sent;
    uint32_t header_size;
    spo_packet_header_t *packet_header = (spo_packet_header_t *)packet_data;

    acks_count = spo_internal_get_acks(acks_list, connection);

    packet_header->src_port = spo_internal_swap_2bytes(connection->local_port);
    packet_header->dst_port = spo_internal_swap_2bytes(connection->remote_port);
    packet_header->seq = spo_internal_swap_4bytes(seq);
    packet_header->ack = spo_internal_swap_4bytes(connection->rcv_start_seq);
    packet_header->sacks = spo_internal_swap_2bytes(acks_count);

    if (acks_count > 0)
        spo_internal_pack_acks(packet_data + sizeof(spo_packet_header_t), acks_list, acks_count);

    header_size = SPO_HEADER_SIZE(acks_count);

    if (data_size > 0)
    {
        uint32_t max_payload_size = SPO_NET_MAX_PACKET_SIZE - header_size;
        if (data_size > max_payload_size)
            data_size = max_payload_size;

        memcpy(packet_data + header_size, data, data_size);
    }

    bytes_sent = spo_net_send(connection->host->socket, packet_data, data_size + header_size, &connection->remote_address);
    if (bytes_sent >= header_size)
    {
        connection->snd_last_packet_time = spo_time_current();
        if (connection->snd_mandatory_packets > 0)
            --connection->snd_mandatory_packets;

        return (bytes_sent - header_size);
    }

    return 0;
}

static uint32_t spo_internal_send_data_packets(spo_connection_data_t *connection,
    uint32_t start_seq, uint32_t max_packets, const uint8_t *data, uint32_t data_size)
{
    uint32_t bytes_sent;
    uint32_t total_bytes_sent = 0;

    while (total_bytes_sent < data_size && max_packets > 0)
    {
        bytes_sent = spo_internal_send_packet(connection,
            start_seq + total_bytes_sent, data + total_bytes_sent, data_size - total_bytes_sent);
        if (bytes_sent == 0) /* can't send data */
            break;

        total_bytes_sent += bytes_sent;
        --max_packets;
    }

    return total_bytes_sent;
}

static uint32_t spo_internal_send_data(spo_connection_data_t *connection, const uint8_t *data, uint32_t data_size)
{
    /* don't allow sending data over the unestablished connection */
    if (connection->state == SPO_CONNECTION_STATE_CONNECTED)
    {
        uint32_t max_bytes_to_send = connection->host->configuration.connection_buf_size - connection->snd_buf_bytes;
        if (max_bytes_to_send > 0)
        {
            uint32_t bytes_to_send = SPO_MIN(data_size, max_bytes_to_send);
            uint32_t buf_size_required = connection->snd_buf_bytes + bytes_to_send;

            if (buf_size_required > connection->snd_buf_size)
            {
                connection->snd_buf = (uint8_t *)realloc(connection->snd_buf, buf_size_required);
                if (connection->snd_buf == NULL)
                    return 0; /* can't allocate enough space */

                connection->snd_buf_size = buf_size_required;
            }

            memcpy(connection->snd_buf + connection->snd_buf_bytes, data, bytes_to_send);
            connection->snd_buf_bytes += bytes_to_send;

            return bytes_to_send;
        }
    }

    return 0;
}

static uint32_t spo_internal_read_data(spo_connection_data_t *connection, uint8_t *buf, uint32_t buf_size)
{
    /* don't allow reading data from the unestablished connection */
    if (connection->state == SPO_CONNECTION_STATE_CONNECTED)
    {
        if (connection->rcv_bytes_ready > 0)
        {
            uint32_t bytes_to_read = SPO_MIN(connection->rcv_bytes_ready, buf_size);

            memcpy(buf, connection->rcv_buf, bytes_to_read);
            /* shift receive buffer */
            memmove(connection->rcv_buf, connection->rcv_buf + bytes_to_read,
                connection->host->configuration.connection_buf_size - bytes_to_read);
            connection->rcv_start_seq += bytes_to_read;
            connection->rcv_bytes_ready -= bytes_to_read;

            return bytes_to_read;
        }
    }

    return 0;
}

/* connections management */

static uint16_t spo_internal_get_port_from_pool(spo_host_data_t *host)
{
    uint16_t port;
    uint16_t ports_available[UINT16_MAX];
    uint16_t ports_count = 0;

    /* find available ports, first and last ports are reserved */
    for (port = 1; port < UINT16_MAX; ++port)
    {
        if (host->connections_by_ports[port] == NULL)
            ports_available[ports_count++] = port;
    }
    if (ports_count == 0)
        return 0;

    return ports_available[spo_random_next() % ports_count];
}

static spo_bool_t spo_internal_allocate_connection_buffers(spo_connection_data_t *connection)
{
    connection->rcv_buf = (uint8_t *)malloc(connection->host->configuration.connection_buf_size);
    if (connection->rcv_buf == NULL)
        return SPO_FALSE;

    connection->snd_buf = NULL; /* lazy allocation */

    return SPO_TRUE;
}

static void spo_internal_destroy_connection(spo_connection_data_t *connection)
{
    spo_index_item_t *current;

    /* release port */
    connection->host->connections_by_ports[connection->local_port] = NULL;

    /* destroy buffers */
    current = SPO_INDEX_FIRST(&connection->rcv_packets);
    while (SPO_INDEX_VALID(&connection->rcv_packets, current))
    {
        free(current->data);
        current = SPO_INDEX_NEXT(&connection->rcv_packets, current);
    }
    spo_index_destroy(&connection->rcv_packets);

    current = SPO_INDEX_FIRST(&connection->snd_acked_packets);
    while (SPO_INDEX_VALID(&connection->snd_acked_packets, current))
    {
        free(current->data);
        current = SPO_INDEX_NEXT(&connection->snd_acked_packets, current);
    }
    spo_index_destroy(&connection->snd_acked_packets);

    if (connection->rcv_buf != NULL)
    {
        free(connection->rcv_buf);
        connection->rcv_buf = NULL;
    }
    if (connection->snd_buf != NULL)
    {
        free(connection->snd_buf);
        connection->snd_buf = NULL;
    }
}

static void spo_internal_terminate_connection(spo_connection_data_t *connection)
{
    spo_connection_state_t state = connection->state;

    /* first, mark connection as closed */
    connection->state = SPO_CONNECTION_STATE_CLOSED;

    switch (state)
    {
    case SPO_CONNECTION_STATE_CONNECT_STARTED:
        /* remove connection from 'started_connections' list */
        spo_list_remove_items_by_data(&connection->host->started_connections, connection);

        /* fall through */
    case SPO_CONNECTION_STATE_CONNECT_RECEIVED_WHILE_STARTED:
        /* it was a started connection, so notify the user code */
        spo_internal_fire_unable_to_connect_event(connection);
        spo_internal_destroy_connection(connection);
        break;
    case SPO_CONNECTION_STATE_CONNECT_RECEIVED:
        /* remove connection from 'incoming_connections' list */
        spo_list_remove_items_by_data(&connection->host->incoming_connections, connection);

        spo_internal_destroy_connection(connection);
        /* we must entirely destroy such connections because the user code doesn't know about them */
        free(connection);
        break;
    case SPO_CONNECTION_STATE_CONNECTED:
        spo_internal_fire_connection_lost_event(connection);
        spo_internal_destroy_connection(connection);
        break;
    case SPO_CONNECTION_STATE_INIT:
        /* remove connection from 'connections' list */
        /* in other cases connections are removed via 'spo_internal_process_connections' function */
        spo_list_remove_items_by_data(&connection->host->connections, connection);

        spo_internal_destroy_connection(connection);
        /* we must entirely destroy such connections because the user code doesn't know about them */
        free(connection);
        break;
    }
}

static void spo_internal_init_connection(spo_connection_data_t *connection, spo_host_data_t *host, uint16_t port)
{
    memset(connection, 0, sizeof(spo_connection_data_t));
    connection->host = host;
    connection->state = SPO_CONNECTION_STATE_INIT;
    connection->created_time = spo_time_current();
    connection->local_port = port;
    connection->snd_start_seq = spo_random_next();
    connection->snd_next_seq = connection->snd_start_seq;
    spo_index_init(&connection->rcv_packets);
    spo_index_init(&connection->snd_acked_packets);

    host->connections_by_ports[port] = connection;
}

static spo_connection_data_t *spo_internal_reuse_oldest_connection(spo_host_data_t *host)
{
    spo_connection_data_t *connection = spo_internal_find_oldest_incoming_connection(host);
    if (connection == NULL)
        return NULL;

    spo_internal_destroy_connection(connection);

    spo_internal_init_connection(connection, host, connection->local_port);
    return connection;
}

static spo_connection_data_t *spo_internal_allocate_connection(spo_host_data_t *host)
{
    spo_connection_data_t *connection;
    uint16_t port;

    if (host->connections.length >= host->configuration.max_connections)
    {
        /* try to reuse the oldest connection */
        return spo_internal_reuse_oldest_connection(host);
    }

    port = spo_internal_get_port_from_pool(host);
    if (port == 0) /* all ports are busy */
    {
        /* try to reuse the oldest connection */
        return spo_internal_reuse_oldest_connection(host);
    }

    /* create new connection */
    connection = (spo_connection_data_t *)malloc(sizeof(spo_connection_data_t));
    if (connection == NULL)
        return NULL;

    if (spo_list_add_item(&host->connections, connection) == NULL)
    {
        free(connection);
        return NULL;
    }

    spo_internal_init_connection(connection, host, port);
    return connection;
}

/* network packets processing */

static spo_index_item_t *spo_internal_insert_packet_desc(spo_index_t *list,
    spo_index_item_t *after_item, const spo_packet_desc_t *packet_desc)
{
    spo_index_item_t *new_item;
    spo_packet_desc_t *packet_to_insert = (spo_packet_desc_t *)malloc(sizeof(spo_packet_desc_t));

    if (packet_to_insert == NULL)
        return NULL;

    *packet_to_insert = *packet_desc;

    new_item = spo_index_insert_item_after(list, after_item, packet_to_insert->start, packet_to_insert);
    if (new_item == NULL)
    {
        free(packet_to_insert);
        return NULL;
    }

    return new_item;
}

static spo_bool_t spo_internal_merge_packet_desc(spo_index_t *list, const spo_packet_desc_t *packet)
{
    uint32_t current_start;
    uint32_t current_end;
    uint32_t prev_end;
    spo_packet_desc_t *prev_packet_desc;
    spo_packet_desc_t *packet_desc;
    spo_index_item_t *current;

    /* search item after which a new item can be inserted */
    current = spo_index_find_pos_by_key(list, packet->start);
    if (current == NULL)
    {
        /* insert head */
        current = spo_internal_insert_packet_desc(list, NULL, packet);
        if (current == NULL)
            return SPO_FALSE;

        prev_packet_desc = (spo_packet_desc_t *)current->data;
        /* get the next item */
        current = SPO_INDEX_NEXT(list, current);
    }
    else
    {
        prev_packet_desc = (spo_packet_desc_t *)current->data;

        /* new packet is a current item */
        current_start = packet->start;
        current_end = current_start + packet->size;
        prev_end = prev_packet_desc->start + prev_packet_desc->size;

        /* don't check for current_start >= prev_start as it's always true */
        if (SPO_WRAPPED_LESS(prev_end, current_end))
        {
            if (SPO_WRAPPED_LESS(prev_end, current_start))
            {
                current = spo_internal_insert_packet_desc(list, current, packet);
                if (current == NULL)
                    return SPO_FALSE;
                /* set new item as a previous item */
                prev_packet_desc = (spo_packet_desc_t *)current->data;
            }
            else
            {
                /* new packet starts within previous and ends after the previous */
                prev_packet_desc->size += (current_end - prev_end);
            }
            /* get the next item */
            current = SPO_INDEX_NEXT(list, current);
        }
        else
        {
            /* previous item includes the current one */
            /* merge is completed because nothing has changed */
            return SPO_TRUE;
        }
    }

    /* items are sorted by start seq */
    while (SPO_INDEX_VALID(list, current))
    {
        packet_desc = (spo_packet_desc_t *)current->data;

        current_start = packet_desc->start;
        current_end = current_start + packet_desc->size;
        prev_end = prev_packet_desc->start + prev_packet_desc->size;

        /* don't check for current_start >= prev_start as it's always true */
        if (SPO_WRAPPED_LESS(prev_end, current_end))
        {
            if (SPO_WRAPPED_LESS(prev_end, current_start))
            {
                /* a hole in the data, merge is completed */
                break;
            }
            /* current item starts within previous and ends after the previous */
            prev_packet_desc->size += (current_end - prev_end);
        }
        /* else previous item includes the current one */

        free(packet_desc);
        current = spo_index_remove_item(list, current);
    }

    return SPO_TRUE;
}

static spo_bool_t spo_internal_fill_rcv_buffer(spo_connection_data_t *connection, uint32_t seq, const uint8_t *data, uint32_t data_size)
{
    spo_packet_desc_t packet_desc;
    uint32_t common_start_seq;
    uint32_t common_end_seq;
    uint32_t common_data_size;
    uint32_t pos_in_buf;
    uint32_t pos_in_data;

    uint32_t win_start_seq = connection->rcv_start_seq + connection->rcv_bytes_ready;
    uint32_t win_end_seq = connection->rcv_start_seq + connection->host->configuration.connection_buf_size - 1;

    uint32_t data_start_seq = seq;
    uint32_t data_end_seq = data_start_seq + data_size - 1;

    /* check if the receive buffer can accept received packets */
    if (SPO_WRAPPED_LESS(win_end_seq, win_start_seq))
        return SPO_FALSE;
    if (SPO_WRAPPED_LESS(data_end_seq, win_start_seq))
        return SPO_FALSE;
    if (SPO_WRAPPED_LESS(win_end_seq, data_start_seq))
        return SPO_FALSE;

    /* unsigned arithmetic does all the magic */
    common_start_seq = SPO_WRAPPED_MAX(data_start_seq, win_start_seq);
    common_end_seq = SPO_WRAPPED_MIN(win_end_seq, data_end_seq);
    common_data_size = common_end_seq - common_start_seq + 1;

    pos_in_buf = common_start_seq - win_start_seq;
    pos_in_data = common_start_seq - data_start_seq;

    memcpy(connection->rcv_buf + pos_in_buf, data + pos_in_data, common_data_size);

    /* save description of the new data */
    packet_desc.start = common_start_seq;
    packet_desc.size = common_data_size;

    if (spo_internal_merge_packet_desc(&connection->rcv_packets, &packet_desc) == SPO_FALSE)
        return SPO_FALSE;

    return SPO_TRUE;
}

static uint32_t spo_internal_remove_acknowledged_packets(spo_connection_data_t *connection, uint32_t ack)
{
    uint32_t bytes_sent;
    uint32_t win_start_seq = connection->snd_start_seq;
    uint32_t win_end_seq = connection->snd_next_seq - 1;
    uint32_t last_received_seq = ack - 1;

    /* check if acknowledged packets are in the send buffer */
    if (SPO_WRAPPED_LESS(last_received_seq, win_start_seq))
        return 0;
    if (SPO_WRAPPED_LESS(win_end_seq, last_received_seq))
        return 0;

    /* at least one more byte has acknowledged */
    bytes_sent = ack - win_start_seq;
    /* shift send buffer */
    memmove(connection->snd_buf, connection->snd_buf + bytes_sent, connection->snd_buf_bytes - bytes_sent);
    connection->snd_buf_bytes -= bytes_sent;
    connection->snd_start_seq = ack;

    SPO_LOG("received ACK %u (accepted %u bytes)", ack, bytes_sent);
    return bytes_sent;
}

static void spo_internal_process_started_connection_packet(spo_connection_data_t *connection,
    uint16_t src_port, uint32_t seq, uint32_t ack)
{
    if (ack != connection->snd_start_seq)
        return;

    if (spo_internal_allocate_connection_buffers(connection) == SPO_FALSE)
    {
        spo_internal_terminate_connection(connection);
        return;
    }

    /* remove connection from 'started_connections' list */
    spo_list_remove_items_by_data(&connection->host->started_connections, connection);

    connection->state = SPO_CONNECTION_STATE_CONNECTED;
    connection->remote_port = src_port;
    connection->rcv_start_seq = seq;
    connection->rcv_last_packet_time = spo_time_current();

    spo_internal_handle_connection_init(connection);
    spo_internal_fire_connected_event(connection);
}

static void spo_internal_process_rendezvous_connection_packet(spo_connection_data_t *connection, uint16_t src_port, uint32_t seq)
{
    SPO_LOG("CONNECT received while in STARTED state");

    /* remove connection from 'started_connections' list */
    spo_list_remove_items_by_data(&connection->host->started_connections, connection);

    connection->state = SPO_CONNECTION_STATE_CONNECT_RECEIVED_WHILE_STARTED;
    connection->remote_port = src_port;
    connection->rcv_start_seq = seq;
    connection->rcv_last_packet_time = spo_time_current();

    /* init the connection after confirming packet */
}

static void spo_internal_process_incoming_connection_initial_packet(spo_host_data_t *host,
    const spo_net_address_t *src_address, uint16_t src_port, uint32_t seq)
{
    spo_connection_data_t *connection = spo_internal_allocate_connection(host);
    if (connection == NULL)
        return;

    /* add connection to 'incoming_connections' list */
    if (spo_list_add_item(&host->incoming_connections, connection) == NULL)
    {
        spo_internal_terminate_connection(connection);
        return;
    }

    SPO_LOG("CONNECT received");

    connection->state = SPO_CONNECTION_STATE_CONNECT_RECEIVED;
    connection->remote_address = *src_address;
    connection->remote_port = src_port;
    connection->rcv_start_seq = seq;
    connection->rcv_last_packet_time = spo_time_current();
}

static void spo_internal_process_incoming_connection_confirming_packet(spo_connection_data_t *connection,
    uint16_t src_port, uint32_t seq, uint32_t ack, const uint8_t *data, uint32_t data_size)
{
    if (src_port != connection->remote_port)
        return;
    if (ack != connection->snd_start_seq)
        return;

    if (spo_internal_allocate_connection_buffers(connection) == SPO_FALSE)
    {
        spo_internal_terminate_connection(connection);
        return;
    }

    if (data_size > 0)
    {
        if (spo_internal_fill_rcv_buffer(connection, seq, data, data_size) == SPO_FALSE)
            return;
        spo_internal_handle_new_data_received(connection);
    }
    else
    {
        if (seq != connection->rcv_start_seq)
            return;
    }

    connection->rcv_last_packet_time = spo_time_current();

    spo_internal_handle_connection_init(connection);

    if (connection->state == SPO_CONNECTION_STATE_CONNECT_RECEIVED_WHILE_STARTED)
    {
        connection->state = SPO_CONNECTION_STATE_CONNECTED;
        spo_internal_fire_connected_event(connection);
    }
    else
    {
        /* remove connection from 'incoming_connections' list */
        spo_list_remove_items_by_data(&connection->host->incoming_connections, connection);

        connection->state = SPO_CONNECTION_STATE_CONNECTED;
        spo_internal_fire_incoming_connection_event(connection);
    }
}

static void spo_internal_process_incoming_connection_packet(spo_host_data_t *host,
    const spo_net_address_t *src_address, uint16_t src_port, uint32_t seq)
{
    spo_connection_data_t *connection = spo_internal_find_active_connection(host, src_address, src_port);
    if (connection != NULL)
    {
        /* looks like duplicate CONNECT, so update receive time and ignore the packet */
        connection->rcv_last_packet_time = spo_time_current();
        SPO_LOG("duplicate CONNECT received");
        return;
    }

    connection = spo_internal_find_started_connection(host, src_address);
    if (connection != NULL)
        spo_internal_process_rendezvous_connection_packet(connection, src_port, seq);
    else
        spo_internal_process_incoming_connection_initial_packet(host, src_address, src_port, seq);
}

static void spo_internal_process_acks_list(spo_connection_data_t *connection, const spo_packet_desc_t *acks_list, uint16_t acks_count)
{
    spo_packet_desc_t packet_desc;
    uint32_t start_seq;
    uint32_t size;
    uint16_t ack = 0;

    while (ack < acks_count)
    {
        start_seq = acks_list[ack].start;
        size = acks_list[ack].size;

        /* check if the data are in send buffer */
        if (SPO_WRAPPED_GREATER_EQ(start_seq, connection->snd_start_seq) &&
            SPO_WRAPPED_LESS_EQ(start_seq + size, connection->snd_next_seq))
        {
            /* save description of the new data */
            packet_desc.start = start_seq;
            packet_desc.size = size;

            if (spo_internal_merge_packet_desc(&connection->snd_acked_packets, &packet_desc) == SPO_FALSE)
                break;
        }

        ++ack;
    }
}

static void spo_internal_remove_old_acks(spo_connection_data_t *connection, uint32_t ack)
{
    spo_packet_desc_t *packet_desc;
    spo_index_item_t *current = SPO_INDEX_FIRST(&connection->snd_acked_packets);

    while (SPO_INDEX_VALID(&connection->snd_acked_packets, current))
    {
        packet_desc = (spo_packet_desc_t *)current->data;

        if (SPO_WRAPPED_LESS_EQ(ack, packet_desc->start)) /* packet from the future */
            break;

        if (SPO_WRAPPED_LESS(ack, packet_desc->start + packet_desc->size))
        {
            /* the data are not fully acknowledged */
            /* it should not happen, so something is wrong */
            current = SPO_INDEX_NEXT(&connection->snd_acked_packets, current);
        }
        else
        {
            /* remove old packet */
            free(packet_desc);
            current = spo_index_remove_item(&connection->snd_acked_packets, current);
        }
    }
}

static void spo_internal_process_established_connection_packet(spo_connection_data_t *connection,
    uint16_t src_port, uint32_t seq, uint32_t ack, const spo_packet_desc_t *acks_list, uint16_t acks_count,
    const uint8_t *data, uint32_t data_size)
{
    if (src_port != connection->remote_port)
        return;

    connection->rcv_last_packet_time = spo_time_current();

    /* sender */
    if (connection->snd_buf_bytes > 0)
    {
        uint32_t bytes_sent = spo_internal_remove_acknowledged_packets(connection, ack);
        if (bytes_sent > 0)
        {
            spo_internal_remove_old_acks(connection, ack);
            spo_internal_process_acks_list(connection, acks_list, acks_count);
            spo_internal_handle_sent_data_acknowledged(connection, bytes_sent);
        }
        else
        {
            spo_internal_process_acks_list(connection, acks_list, acks_count);
            spo_internal_handle_unknown_ack(connection, ack);
        }
    }
    /* receiver */
    if (data_size > 0)
    {
        if (spo_internal_fill_rcv_buffer(connection, seq, data, data_size))
            spo_internal_handle_new_data_received(connection);
    }
}

static void spo_internal_process_packet(spo_host_data_t *host,
    const spo_net_address_t *src_address, const uint8_t *packet_data, uint32_t packet_size)
{
    spo_packet_desc_t acks_list[SPO_PACKET_MAX_SACKS];
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint16_t acks_count;
    spo_connection_data_t *connection;
    spo_packet_header_t *header = (spo_packet_header_t *)packet_data;

    if (packet_size < sizeof(spo_packet_header_t))
        return;

    src_port = spo_internal_swap_2bytes(header->src_port);
    dst_port = spo_internal_swap_2bytes(header->dst_port);
    seq = spo_internal_swap_4bytes(header->seq);
    ack = spo_internal_swap_4bytes(header->ack);
    acks_count = spo_internal_swap_2bytes(header->sacks);

    if (acks_count > 0)
    {
        if (acks_count > SPO_PACKET_MAX_SACKS)
            return;

        if (packet_size < SPO_HEADER_SIZE(acks_count))
            return;

        spo_internal_unpack_acks(acks_list, packet_data + sizeof(spo_packet_header_t),
            packet_size - sizeof(spo_packet_header_t), acks_count);
    }

    SPO_LOG("incoming packet (SEQ %u, ACK %u, acks %hu, %u bytes)",
        seq, ack, acks_count, packet_size - SPO_HEADER_SIZE(acks_count));

    if (spo_random_next() % 100 < 5)
    {
        SPO_LOG("LOST!");
        return;
    }

    if (dst_port == 0) /* incoming connection */
    {
        spo_internal_process_incoming_connection_packet(host, src_address, src_port, seq);
        return;
    }

    /* find connection */
    connection = host->connections_by_ports[dst_port];
    if (connection == NULL)
        return;

    /* check remote address */
    if (spo_net_equal_addresses(src_address, &connection->remote_address) == SPO_FALSE)
        return;

    switch (connection->state)
    {
    case SPO_CONNECTION_STATE_CONNECT_STARTED:
        spo_internal_process_started_connection_packet(connection, src_port, seq, ack);
        break;
    case SPO_CONNECTION_STATE_CONNECT_RECEIVED_WHILE_STARTED:
    case SPO_CONNECTION_STATE_CONNECT_RECEIVED:
        spo_internal_process_incoming_connection_confirming_packet(connection, src_port, seq, ack,
            packet_data + SPO_HEADER_SIZE(acks_count), packet_size - SPO_HEADER_SIZE(acks_count));
        break;
    case SPO_CONNECTION_STATE_CONNECTED:
        spo_internal_process_established_connection_packet(connection, src_port, seq, ack, acks_list, acks_count,
            packet_data + SPO_HEADER_SIZE(acks_count), packet_size - SPO_HEADER_SIZE(acks_count));
        break;
    }
}

static spo_bool_t spo_internal_check_received_data(spo_connection_data_t *connection)
{
    spo_packet_desc_t *packet_desc;
    uint32_t packet_end;
    uint32_t expected_seq;
    uint32_t bytes_received = 0;
    spo_index_item_t *current = SPO_INDEX_FIRST(&connection->rcv_packets);

    while (SPO_INDEX_VALID(&connection->rcv_packets, current))
    {
        packet_desc = (spo_packet_desc_t *)current->data;

        packet_end = packet_desc->start + packet_desc->size;
        expected_seq = connection->rcv_start_seq + connection->rcv_bytes_ready + bytes_received;

        if (SPO_WRAPPED_LESS(expected_seq, packet_desc->start)) /* a hole in the data */
            break;

        if (SPO_WRAPPED_LESS(expected_seq, packet_end)) /* packet has new data */
            bytes_received += (packet_end - expected_seq);

        /* one more case: it is an old packet, so we can simply destroy it */

        free(packet_desc);
        current = spo_index_remove_item(&connection->rcv_packets, current);
    }

    if (bytes_received > 0)
    {
        connection->rcv_bytes_ready += bytes_received;

        spo_internal_fire_incoming_data_event(connection, connection->rcv_bytes_ready);

        return SPO_TRUE;
    }

    return SPO_FALSE;
}

static spo_bool_t spo_internal_check_connection_timeout(spo_connection_data_t *connection)
{
    if (spo_time_elapsed(connection->rcv_last_packet_time) >= connection->host->configuration.connection_timeout)
    {
        spo_internal_terminate_connection(connection);
        return SPO_TRUE;
    }

    return SPO_FALSE;
}

static spo_bool_t spo_internal_send_ping_packet(spo_connection_data_t *connection)
{
    if (spo_time_elapsed(connection->snd_last_packet_time) >= connection->host->configuration.ping_interval)
    {
        spo_internal_send_packet(connection, connection->snd_start_seq, NULL, 0);
        SPO_LOG("PING sent, ACK %u", connection->rcv_start_seq);
        return SPO_TRUE;
    }

    return SPO_FALSE;
}

static spo_bool_t spo_internal_send_fast_ack(spo_connection_data_t *connection)
{
    if (connection->snd_mandatory_packets > 0)
    {
        spo_internal_send_packet(connection, connection->snd_start_seq, NULL, 0);
        SPO_LOG("ACK %u sent", connection->rcv_start_seq);
        return SPO_TRUE;
    }

    return SPO_FALSE;
}

static uint32_t spo_internal_send_next_connection_data(spo_connection_data_t *connection, uint32_t cwnd_bytes)
{
    uint32_t bytes_sent_already = connection->snd_next_seq - connection->snd_start_seq;
    uint32_t max_bytes_limit = SPO_MIN(connection->snd_buf_bytes, cwnd_bytes);

    if (bytes_sent_already < max_bytes_limit) /* connection is allowed to send data */
    {
        /* send next data packet */
        uint32_t bytes_sent = spo_internal_send_data_packets(connection, connection->snd_next_seq, 1,
            connection->snd_buf + bytes_sent_already, max_bytes_limit - bytes_sent_already);

        if (bytes_sent > 0)
        {
            SPO_LOG("data sent (%u bytes, SEQ %u, ACK %u)",
                bytes_sent, connection->snd_next_seq, connection->rcv_start_seq);

            connection->snd_next_seq += bytes_sent;

            spo_internal_handle_next_data_sent(connection);
        }

        return bytes_sent;
    }

    return 0;
}

static uint32_t spo_internal_transmit_packet(spo_connection_data_t *connection, uint32_t seq)
{
    uint32_t pos_in_buf = seq - connection->snd_start_seq;

    if (pos_in_buf < connection->snd_buf_bytes)
    {
        /* send specified data packet */
        uint32_t bytes_sent = spo_internal_send_data_packets(connection, seq, 1,
            connection->snd_buf + pos_in_buf, connection->snd_buf_bytes - pos_in_buf);

        if (bytes_sent > 0)
        {
            /* 'snd_next_seq' always points to the next seq to send */
            if (SPO_WRAPPED_LESS(connection->snd_next_seq, seq + bytes_sent))
                connection->snd_next_seq = seq + bytes_sent;

            return bytes_sent;
        }
    }

    return 0;
}

static spo_bool_t spo_internal_process_established_connection(spo_connection_data_t *connection)
{
    /* can't transmit data if send buffer is empty */
    if (connection->snd_buf_bytes > 0 && spo_internal_handle_data_transmission(connection))
        return SPO_TRUE;

    /* can't send data, so send a special packet */

    if (spo_internal_send_fast_ack(connection))
        return SPO_TRUE;

    if (spo_internal_send_ping_packet(connection))
        return SPO_TRUE;

    return SPO_FALSE;
}

static spo_bool_t spo_internal_process_started_connection(spo_connection_data_t *connection)
{
    if (spo_time_elapsed(connection->snd_last_packet_time) >= connection->host->configuration.connect_retransmission_timeout)
    {
        if (connection->connect_attempts < connection->host->configuration.max_connect_attempts)
        {
            spo_internal_send_packet(connection, connection->snd_start_seq, NULL, 0);
            ++connection->connect_attempts;

            SPO_LOG("CONNECT sent");
        }
        else
        {
            spo_internal_terminate_connection(connection);
        }

        return SPO_TRUE;
    }

    return SPO_FALSE;
}

static spo_bool_t spo_internal_process_incoming_connection(spo_connection_data_t *connection)
{
    if (spo_time_elapsed(connection->snd_last_packet_time) >= connection->host->configuration.accept_retransmission_timeout)
    {
        if (connection->connect_attempts < connection->host->configuration.max_accepted_attempts)
        {
            spo_internal_send_packet(connection, connection->snd_start_seq, NULL, 0);
            ++connection->connect_attempts;

            SPO_LOG("ACCEPTED sent");
        }
        else
        {
            spo_internal_terminate_connection(connection);
        }

        return SPO_TRUE;
    }

    return SPO_FALSE;
}

static spo_bool_t spo_internal_process_connections(spo_host_data_t *host)
{
    spo_bool_t state_changed = SPO_FALSE;
    spo_connection_data_t *connection;
    spo_list_item_t *current = SPO_LIST_FIRST(&host->connections);

    while (SPO_LIST_VALID(&host->connections, current))
    {
        connection = (spo_connection_data_t *)current->data;

        switch (connection->state)
        {
        case SPO_CONNECTION_STATE_CONNECT_STARTED:
            if (spo_internal_process_started_connection(connection))
                state_changed = SPO_TRUE;
            break;
        case SPO_CONNECTION_STATE_CONNECT_RECEIVED_WHILE_STARTED:
        case SPO_CONNECTION_STATE_CONNECT_RECEIVED:
            if (spo_internal_process_incoming_connection(connection))
                state_changed = SPO_TRUE;
            break;
        case SPO_CONNECTION_STATE_CONNECTED:
            if (spo_internal_check_connection_timeout(connection))
            {
                state_changed = SPO_TRUE;
                break;
            }

            if (spo_internal_check_received_data(connection))
                state_changed = SPO_TRUE;
            if (spo_internal_process_established_connection(connection))
                state_changed = SPO_TRUE;
            break;
        }

        /* remove terminated connections from the list */
        if (connection->state == SPO_CONNECTION_STATE_CLOSED)
            current = spo_list_remove_item(&host->connections, current);
        else
            current = SPO_LIST_NEXT(&host->connections, current);
    }

    return state_changed;
}

static spo_bool_t spo_internal_receive_packets(spo_host_data_t *host)
{
    uint8_t packet_data[SPO_NET_MAX_PACKET_SIZE];
    spo_net_address_t address;
    uint32_t bytes_received;
    spo_bool_t data_received = SPO_FALSE;
    spo_bool_t data_available = spo_net_data_available(host->socket);
    while (data_available)
    {
        bytes_received = spo_net_recv(host->socket, packet_data, sizeof(packet_data), &address);
        if (bytes_received > 0)
        {
            spo_internal_process_packet(host, &address, packet_data, bytes_received);
            data_received = SPO_TRUE;
        }

        data_available = spo_net_data_available(host->socket);
    }

    return data_received;
}

static spo_connection_data_t *spo_internal_start_connection(spo_host_data_t *host, const spo_net_address_t *remote_address)
{
    spo_connection_data_t *connection = spo_internal_allocate_connection(host);
    if (connection == NULL)
        return NULL;

    /* add connection to 'started_connections' list */
    if (spo_list_add_item(&host->started_connections, connection) == NULL)
    {
        spo_internal_terminate_connection(connection);
        return NULL;
    }

    SPO_LOG("CONNECT started");

    connection->state = SPO_CONNECTION_STATE_CONNECT_STARTED;
    connection->remote_address = *remote_address;

    return connection;
}

spo_bool_t spo_init()
{
    if (!spo_net_init())
        return SPO_FALSE;

    spo_logger = NULL;
    spo_random_init();

    return SPO_TRUE;
}

void spo_shutdown()
{
    spo_net_shutdown();
}

void spo_set_logger(logger_ptr_t logger)
{
    spo_logger = logger;
}

spo_host_t spo_new_host(const spo_net_address_t *bind_address,
    const spo_configuration *configuration, const spo_callbacks_t *callbacks)
{
    spo_net_socket_t socket;
    spo_host_data_t *host_data;

    socket = spo_net_new_socket(bind_address, configuration->socket_buf_size);
    if (socket == NULL)
        return NULL;

    host_data = (spo_host_data_t *)malloc(sizeof(spo_host_data_t));
    if (host_data == NULL)
    {
        spo_net_close_socket(socket);
        return NULL;
    }

    host_data->socket = socket;
    host_data->configuration = *configuration;
    host_data->callbacks = *callbacks;
    spo_list_init(&host_data->connections);
    spo_list_init(&host_data->started_connections);
    spo_list_init(&host_data->incoming_connections);
    memset(host_data->connections_by_ports, 0, sizeof(host_data->connections_by_ports));

    return host_data;
}

void spo_close_host(spo_host_t host)
{
    spo_host_data_t *host_data = (spo_host_data_t *)host;

    /* TODO: terminate and remove all connections */
    spo_net_close_socket(host_data->socket);
    spo_list_destroy(&host_data->connections);
    spo_list_destroy(&host_data->started_connections);
    spo_list_destroy(&host_data->incoming_connections);

    free(host_data);
}

spo_bool_t spo_make_progress(spo_host_t host)
{
    spo_bool_t result = SPO_FALSE;
    spo_host_data_t *host_data = (spo_host_data_t *)host;

    if (spo_internal_receive_packets(host_data))
        result = SPO_TRUE;
    if (spo_internal_process_connections(host_data))
        result = SPO_TRUE;

    return result;
}

spo_connection_t spo_new_connection(spo_host_t host, const spo_net_address_t *host_address)
{
    return spo_internal_start_connection((spo_host_data_t *)host, host_address);
}

spo_connection_state_t spo_get_connection_state(spo_connection_t connection)
{
    spo_connection_data_t *connection_data = (spo_connection_data_t *)connection;

    return connection_data->state;
}

spo_bool_t spo_get_remote_address(spo_connection_t connection, spo_net_address_t *host_address)
{
    spo_connection_data_t *connection_data = (spo_connection_data_t *)connection;

    if (connection_data->state == SPO_CONNECTION_STATE_CONNECTED)
    {
        *host_address = connection_data->remote_address;
        return SPO_TRUE;
    }

    return SPO_FALSE;
}

void spo_close_connection(spo_connection_t connection)
{
    spo_connection_data_t *connection_data = (spo_connection_data_t *)connection;

    spo_internal_destroy_connection(connection_data);
    spo_list_remove_items_by_data(&connection_data->host->connections, connection_data);
    spo_list_remove_items_by_data(&connection_data->host->started_connections, connection_data);
    /* don't try to remove the connection from 'incoming_connections' list as if the user code
       knows about connection then there is nothing to remove */

    free(connection_data);
}

uint32_t spo_send(spo_connection_t connection, const uint8_t *buf, uint32_t buf_size)
{
    spo_connection_data_t *connection_data = (spo_connection_data_t *)connection;

    return spo_internal_send_data(connection_data, buf, buf_size);
}

uint32_t spo_read(spo_connection_t connection, uint8_t *buf, uint32_t buf_size)
{
    spo_connection_data_t *connection_data = (spo_connection_data_t *)connection;

    return spo_internal_read_data(connection_data, buf, buf_size);
}
