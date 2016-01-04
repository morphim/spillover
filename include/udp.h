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

#ifndef SPO_UDP_H
#define SPO_UDP_H

#include "common.h"
#include "pstdint.h"

#define SPO_NET_MAX_PACKET_SIZE 1280

typedef enum
{
    SPO_NET_SOCKET_TYPE_IPV4,
    SPO_NET_SOCKET_TYPE_IPV6
} spo_net_socket_type_t;

#define SPO_NET_IPV4_ADDRESS_SIZE 4
#define SPO_NET_IPV6_ADDRESS_SIZE 16

typedef struct
{
    spo_net_socket_type_t type;
    uint8_t address[SPO_NET_IPV6_ADDRESS_SIZE];
    uint16_t port;
} spo_net_address_t;

typedef void *spo_net_socket_t;

spo_bool_t spo_net_equal_addresses(const spo_net_address_t *first, const spo_net_address_t *second);

spo_bool_t spo_net_init();
void spo_net_shutdown();

spo_net_socket_t spo_net_new_socket(const spo_net_address_t *bind_address, uint32_t buf_size);
spo_bool_t spo_net_data_available(spo_net_socket_t socket);
void spo_net_close_socket(spo_net_socket_t socket);

uint32_t spo_net_recv(spo_net_socket_t socket, uint8_t *buf, uint32_t buf_size, spo_net_address_t *address);
uint32_t spo_net_send(spo_net_socket_t socket, const uint8_t *buf, uint32_t buf_size, const spo_net_address_t *address);

#endif
