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

#ifndef SPO_H
#define SPO_H

#include "common.h"
#include "pstdint.h"
#include "udp.h"

typedef void *spo_host_t;
typedef void *spo_connection_t;

typedef enum
{
    SPO_CONNECTION_STATE_INIT,
    SPO_CONNECTION_STATE_CONNECT_STARTED, /* started connections */
    SPO_CONNECTION_STATE_CONNECT_RECEIVED_WHILE_STARTED, /* rendezvous connections */
    SPO_CONNECTION_STATE_CONNECT_RECEIVED, /* incoming connections */
    SPO_CONNECTION_STATE_CONNECTED, /* established connections */
    SPO_CONNECTION_STATE_CLOSED, /* terminated connections */

    SPO_CONNECTION_STATES_COUNT
} spo_connection_state_t;

typedef struct
{
    void (*unable_to_connect)(spo_host_t host, spo_connection_t connection); /* for started connections */
    void (*connected)(spo_host_t host, spo_connection_t connection); /* for established connections */
    void (*incoming_connection)(spo_host_t host, spo_connection_t connection); /* for established connections */
    void (*incoming_data)(spo_host_t host, spo_connection_t connection, uint32_t data_size); /* for established connections */
    void (*connection_lost)(spo_host_t host, spo_connection_t connection); /* for recently established connections */
} spo_callbacks_t;

typedef struct
{
    uint32_t connection_buf_size; /* 65536 is recommended */
    uint32_t socket_buf_size; /* 4194304 is recommended */

    uint32_t initial_cwnd_in_packets; /* 2 is recommended */
    uint32_t cwnd_on_timeout_in_packets; /* 2 is recommended */
    uint32_t min_ssthresh_in_packets; /* 4 is recommended */
    uint32_t max_cwnd_inc_on_slowstart_in_packets; /* 50 is recommended */
    uint32_t duplicate_acks_for_retransmit; /* 2 is recommended */
    uint32_t ssthresh_factor_on_timeout_percent; /* 50 is recommended */
    uint32_t ssthresh_factor_on_loss_percent; /* 70 is recommended */

    uint32_t max_connections; /* 500 is recommended */
    uint32_t connection_timeout; /* 8000 is recommended */
    uint32_t ping_interval; /* 1500 is recommended */
    uint32_t connect_retransmission_timeout; /* 2000 is recommended */
    uint32_t max_connect_attempts; /* 3 is recommended */
    uint32_t accept_retransmission_timeout; /* 1000 is recommended */
    uint32_t max_accepted_attempts; /* 2 is recommended */
    uint32_t data_retransmission_timeout; /* 600 is recommended */
    uint32_t skip_packets_before_acknowledgement; /* 0 is recommended */
    uint32_t max_consecutive_acknowledges; /* 10 is recommended */
} spo_configuration;

typedef void (*logger_ptr_t)(const char *message);

spo_bool_t spo_init();
void spo_shutdown();
void spo_set_logger(logger_ptr_t logger);

spo_host_t spo_new_host(const spo_net_address_t *bind_address, const spo_configuration *configuration, const spo_callbacks_t *callbacks);
void spo_close_host(spo_host_t host);
spo_bool_t spo_make_progress(spo_host_t host);

spo_connection_t spo_new_connection(spo_host_t host, const spo_net_address_t *host_address);
spo_connection_state_t spo_get_connection_state(spo_connection_t connection);
spo_bool_t spo_get_remote_address(spo_connection_t connection, spo_net_address_t *host_address);
void spo_close_connection(spo_connection_t connection);

uint32_t spo_send(spo_connection_t connection, const uint8_t *buf, uint32_t buf_size);
uint32_t spo_read(spo_connection_t connection, uint8_t *buf, uint32_t buf_size);

#endif
