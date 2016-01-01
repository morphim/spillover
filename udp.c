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

#include "udp.h"

#ifdef WIN32
#include <ws2tcpip.h>

#define SPO_NET_SOCKET_TYPE SOCKET
#define SPO_NET_CLOSE_SOCKET(socket) closesocket(socket)
#define SPO_NET_NFDS(socket) 0
#else
#include <sys/types.h>
#include <sys/socket.h>

#define SPO_NET_SOCKET_TYPE int
#define SPO_NET_CLOSE_SOCKET(socket) close(socket)
#define SPO_NET_NFDS(socket) (socket + 1)
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)
#endif

#ifdef SPO_IPV6_SUPPORT
#define SPO_NET_MAX_SOCKADDR_SIZE sizeof(struct sockaddr_in6)
#else
#define SPO_NET_MAX_SOCKADDR_SIZE sizeof(struct sockaddr_in)
#endif

typedef struct
{
    spo_net_address_t bind_address;
    SPO_NET_SOCKET_TYPE handle;
} spo_net_socket_data_t;

static spo_bool_t spo_internal_set_socket_blocking_mode(SPO_NET_SOCKET_TYPE socket, spo_bool_t block)
{
#ifdef WIN32
    u_long mode = !block;
    if (ioctlsocket(socket, FIONBIO, &mode) == SOCKET_ERROR)
        return SPO_FALSE;
#else
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags == SOCKET_ERROR)
        return SPO_FALSE;

    if (block)
    {
        if (fcntl(socket, F_SETFL, flags & (~O_NONBLOCK)) == SOCKET_ERROR)
            return SPO_FALSE;
    }
    else
    {
        if (fcntl(socket, F_SETFL, flags | O_NONBLOCK) == SOCKET_ERROR)
            return SPO_FALSE;
    }
#endif
    return SPO_TRUE;
}

static void spo_internal_init_sys_address(struct sockaddr *dest, const spo_net_address_t *src)
{
    switch (src->type)
    {
    case SPO_NET_SOCKET_TYPE_IPV4:
        {
            struct sockaddr_in *dest_ipv4 = (struct sockaddr_in *)dest;
            memset(dest_ipv4, 0, sizeof(struct sockaddr_in));

            dest_ipv4->sin_family = AF_INET;
            dest_ipv4->sin_port = htons(src->port);
            memcpy(&dest_ipv4->sin_addr, src->address, SPO_NET_IPV4_ADDRESS_SIZE);
        }
        break;
#ifdef SPO_IPV6_SUPPORT
    case SPO_NET_SOCKET_TYPE_IPV6:
        {
            struct sockaddr_in6 *dest_ipv6 = (struct sockaddr_in6 *)dest;
            memset(dest_ipv6, 0, sizeof(struct sockaddr_in6));

            dest_ipv6->sin6_family = AF_INET6;
            dest_ipv6->sin6_port = htons(src->port);
            memcpy(&dest_ipv6->sin6_addr, src->address, SPO_NET_IPV6_ADDRESS_SIZE);
        }
        break;
#endif
    }
}

static void spo_internal_init_lib_address(spo_net_address_t *dest, const struct sockaddr *src)
{
    switch (src->sa_family)
    {
    case AF_INET:
        {
            const struct sockaddr_in *src_ipv4 = (const struct sockaddr_in *)src;

            dest->type = SPO_NET_SOCKET_TYPE_IPV4;
            dest->port = ntohs(src_ipv4->sin_port);
            memcpy(dest->address, &src_ipv4->sin_addr, SPO_NET_IPV4_ADDRESS_SIZE);
        }
        break;
#ifdef SPO_IPV6_SUPPORT
    case AF_INET6:
        {
            const struct sockaddr_in6 *src_ipv6 = (const struct sockaddr_in6 *)src;

            dest->type = SPO_NET_SOCKET_TYPE_IPV6;
            dest->port = ntohs(src_ipv6->sin6_port);
            memcpy(dest->address, &src_ipv6->sin6_addr, SPO_NET_IPV6_ADDRESS_SIZE);
        }
        break;
#endif
    }
}

spo_bool_t spo_net_equal_addresses(const spo_net_address_t *first, const spo_net_address_t *second)
{
    if (first->type != second->type)
        return SPO_FALSE;
    if (first->port != second->port)
        return SPO_FALSE;

    switch (first->type)
    {
    case SPO_NET_SOCKET_TYPE_IPV4:
        return (memcmp(first->address, second->address, SPO_NET_IPV4_ADDRESS_SIZE) == 0);
    case SPO_NET_SOCKET_TYPE_IPV6:
        return (memcmp(first->address, second->address, SPO_NET_IPV6_ADDRESS_SIZE) == 0);
    }

    return SPO_FALSE;
}

spo_bool_t spo_net_init()
{
#ifdef WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
        return SPO_FALSE;
#endif
    return SPO_TRUE;
}

void spo_net_shutdown()
{
#ifdef WIN32
    WSACleanup();
#endif
}

spo_net_socket_t spo_net_new_socket(const spo_net_address_t *bind_address, uint32_t buf_size)
{
    int result;
    SPO_NET_SOCKET_TYPE handle;
    spo_net_socket_data_t *data;
    uint8_t sockaddr_value[SPO_NET_MAX_SOCKADDR_SIZE];
    struct sockaddr *sockaddr_ptr = (struct sockaddr *)sockaddr_value;

    /* prepare bind address and port */
    spo_internal_init_sys_address(sockaddr_ptr, bind_address);

    handle = socket(sockaddr_ptr->sa_family, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == INVALID_SOCKET)
        return NULL;

    result = bind(handle, sockaddr_ptr, sizeof(sockaddr_value));
    if (result == SOCKET_ERROR)
    {
        SPO_NET_CLOSE_SOCKET(handle);
        return NULL;
    }

    /* set receive buffer size */
    if (setsockopt(handle, SOL_SOCKET, SO_RCVBUF, (const char *)&buf_size, sizeof(buf_size)) == SOCKET_ERROR)
    {
        SPO_NET_CLOSE_SOCKET(handle);
        return NULL;
    }
    /* set non-blocking mode */
    if (spo_internal_set_socket_blocking_mode(handle, SPO_FALSE) == SPO_FALSE)
    {
        SPO_NET_CLOSE_SOCKET(handle);
        return NULL;
    }

    data = (spo_net_socket_data_t *)malloc(sizeof(spo_net_socket_data_t));
    if (data == NULL)
    {
        SPO_NET_CLOSE_SOCKET(handle);
        return NULL;
    }

    data->handle = handle;
    data->bind_address = *bind_address;
    return data;
}

spo_bool_t spo_net_data_available(spo_net_socket_t socket)
{
    fd_set read_fds;
    struct timeval tv;
    spo_net_socket_data_t *socket_data = (spo_net_socket_data_t *)socket;
    SPO_NET_SOCKET_TYPE handle = socket_data->handle;

    memset(&tv, 0, sizeof(tv));

    FD_ZERO(&read_fds);
    FD_SET(handle, &read_fds);

    if (select(SPO_NET_NFDS(handle), &read_fds, NULL, NULL, &tv) > 0)
    {
        if (FD_ISSET(handle, &read_fds))
            return SPO_TRUE;
    }

    return SPO_FALSE;
}

void spo_net_close_socket(spo_net_socket_t socket)
{
    spo_net_socket_data_t *socket_data = (spo_net_socket_data_t *)socket;

    SPO_NET_CLOSE_SOCKET(socket_data->handle);
    free(socket_data);
}

uint32_t spo_net_recv(spo_net_socket_t socket, uint8_t *buf, uint32_t buf_size, spo_net_address_t *address)
{
    int result;
    uint8_t sockaddr_value[SPO_NET_MAX_SOCKADDR_SIZE];
    struct sockaddr *sockaddr_ptr = (struct sockaddr *)sockaddr_value;
    int sockaddr_len = sizeof(sockaddr_value);
    spo_net_socket_data_t *socket_data = (spo_net_socket_data_t *)socket;

    result = recvfrom(socket_data->handle, (char *)buf, buf_size, 0, sockaddr_ptr, &sockaddr_len);
    if (result == SOCKET_ERROR)
        return 0;

    /* save source address and port */
    spo_internal_init_lib_address(address, sockaddr_ptr);

    return result;
}

uint32_t spo_net_send(spo_net_socket_t socket, const uint8_t *buf, uint32_t buf_size, const spo_net_address_t *address)
{
    int result;
    uint8_t sockaddr_value[SPO_NET_MAX_SOCKADDR_SIZE];
    struct sockaddr *sockaddr_ptr = (struct sockaddr *)sockaddr_value;
    spo_net_socket_data_t *socket_data = (spo_net_socket_data_t *)socket;

    /* prepare destination address and port */
    spo_internal_init_sys_address(sockaddr_ptr, address);

    result = sendto(socket_data->handle, (const char *)buf, buf_size, 0, sockaddr_ptr, sizeof(sockaddr_value));
    if (result == SOCKET_ERROR)
        return 0;

    return result;
}
