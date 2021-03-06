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

#ifndef SPO_PACKET_H
#define SPO_PACKET_H

#include "pstdint.h"

#define SPO_PACKET_MAX_SACKS 8

/* packet type */
typedef enum
{
    SPO_PACKET_CONNECT,
    SPO_PACKET_ACCEPT,
    SPO_PACKET_RESET,
    SPO_PACKET_ACK, /* ack only */
    SPO_PACKET_PING, /* ping */
    SPO_PACKET_DATA, /* contains data */

    SPO_PACKET_TYPES_COUNT
} spo_packet_type_t;

typedef struct
{
    uint8_t type; /* packet type */
    uint8_t sacks;
    uint16_t reserved;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq; /* SEQ and packet payload are info from the sender */
    uint32_t ack; /* ACK and SACKs are info from the receiver */
} spo_packet_header_t;

typedef struct
{
    uint32_t start;
    uint32_t size;
} spo_packet_header_sack_t;

typedef struct
{
    uint32_t start;
    uint32_t size;
} spo_packet_desc_t;

#endif
