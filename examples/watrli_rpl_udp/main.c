/*
 * Copyright (C) 2015
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     examples
 * @{
 *
 * @file
 * @brief       watr.li rpl udp application
 *
 * @author
 *
 * @}
 */

#include <stdio.h>
#include <unistd.h>
#include "udp.h"
#include "rpl.h"

#define WATR_LI_CHANNEL         (26)     /**< The used channel */
#define WATR_LI_PAN             (0x03e9) /**< The used PAN ID */
#define WATR_LI_IFACE           (0)      /**< The used Trasmssion device */
#define WATR_LI_UDP_PORT        (12345)  /**< The UDP port to listen */
#define WATR_LI_UDP_BUFFER_SIZE (1024)   /**< The buffer size for receiving UDPs */

/** The UDP server thread stack */
char udp_server_stack_buffer[KERNEL_CONF_STACKSIZE_MAIN];
/** The node IPv6 address */
ipv6_addr_t myaddr;

/**
* @brief the sample UDP server that expects receiving strings
* @param[in] arg unused parameter pointer
*/
static void *watr_li_udp_server(void *arg)
{
    (void) arg;

    sockaddr6_t sa;
    char buffer_main[WATR_LI_UDP_BUFFER_SIZE];
    uint32_t fromlen;
    int sock = socket_base_socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);

    memset(&sa, 0, sizeof(sa));

    sa.sin6_family = AF_INET;
    sa.sin6_port = HTONS(WATR_LI_UDP_PORT);

    fromlen = sizeof(sa);

    if (-1 == socket_base_bind(sock, &sa, sizeof(sa))) {
        puts("[watr_li_udp_server] Error bind failed!");
        socket_base_close(sock);
        return NULL;
    }

    while (1) {
        int32_t recsize = socket_base_recvfrom(sock, (void *)buffer_main,
                                               WATR_LI_UDP_BUFFER_SIZE, 0,
                                               &sa, &fromlen);

        if (recsize < 0) {
            puts("[watr_li_udp_server] ERROR: recsize < 0!");
        } else {
            /* if we received a string print it */
            if (buffer_main[recsize-1] == '\0' ) {
                printf("UDP packet received, payload:\n%s\n", buffer_main);
            } else {
                /* print the buffer bytes in hex */
                printf("UDP packet received, payload (%d bytes):\n", (int)recsize);
                for(int i = 0; i < recsize; ++i) {

                    if ( (i%8) == 0 ) {
                        /* newline after 8 bytes */
                        puts("");
                    }

                    printf("%02x ", buffer_main[i]);
                }
                puts("");
            }
        }

    }

    socket_base_close(sock);

    return NULL;
}

/**
* @brief sends a packet to the DODAG ID (should be the root node IPv6 address)
* @param[in] payload pointer to the payload to be sent
* @param[in] size number of bytes of the payload
*/
static void watr_li_udp_send(char* payload, size_t payload_size)
{
    int sock;
    sockaddr6_t sa;
    int bytes_sent;
    sock = socket_base_socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    char addr_str[IPV6_MAX_ADDR_STR_LEN];

    if (-1 == sock) {
        puts("[watr_li_udp_send] Error Creating Socket!");
        return;
    }
    rpl_dodag_t *mydodag = rpl_get_my_dodag();

    if(mydodag != NULL) {
        memset(&sa, 0, sizeof(sa));
        sa.sin6_family = AF_INET;
        memcpy(&sa.sin6_addr, &(mydodag->dodag_id), 16);
        sa.sin6_port = HTONS(WATR_LI_UDP_PORT);

        bytes_sent = socket_base_sendto(sock,
                                        payload,
                                        payload_size,
                                        0, &sa, sizeof(sa));

        if (bytes_sent < 0) {
            puts("[watr_li_udp_send] Error sending packet!");
        }

        printf("[watr_li_udp_send] Successful deliverd %i bytes over UDP to %s to 6LoWPAN\n",
               bytes_sent, ipv6_addr_to_str(addr_str, IPV6_MAX_ADDR_STR_LEN,
                    &(sa.sin6_addr)));
    } else {
        puts("[watr_li_udp_send] not joined a DODAG (yet), so payload will not be sent.");
    }
    socket_base_close(sock);
}

/**
* @brief setup the readio interface
* @retrun radio_address_t of the set interface
*/
static radio_address_t set_watr_li_if(void)
{
    net_if_set_src_address_mode(WATR_LI_IFACE, NET_IF_TRANS_ADDR_M_SHORT);
    radio_address_t iface_id = net_if_get_hardware_address(WATR_LI_IFACE);
    return iface_id;
}

/**
* @brief set the channel for this watr.li node
* @param[in] chan the channel to use
* @return 0 on success
*/
static int set_watr_li_channel(int32_t chan)
{
    transceiver_command_t tcmd;
    msg_t m;

    tcmd.transceivers = TRANSCEIVER_DEFAULT;
    tcmd.data = &chan;
    m.type = SET_CHANNEL;
    m.content.ptr = (void *) &tcmd;

    msg_send_receive(&m, &m, transceiver_pid);
    return 0;
}

/**
* @brief set the PAN ID for this watr.li node
* @param[in] pan the PAN ID to use
* @return 0 on success
*/
static int set_watr_li_pan(int32_t pan)
{
    transceiver_command_t tcmd;
    msg_t m;

    tcmd.transceivers = TRANSCEIVER_DEFAULT;
    tcmd.data = &pan;
    m.type = SET_PAN;
    m.content.ptr = (void *) &tcmd;

    msg_send_receive(&m, &m, transceiver_pid);
    return 0;
}

/**
* @brief set a desire address for this watr.li node
* @return 0 on success
*/
static int set_watr_li_address(ipv6_addr_t* node_addr)
{
    ipv6_net_if_add_addr(WATR_LI_IFACE, node_addr, NDP_ADDR_STATE_PREFERRED, 0, 0, 0);
    return 0;
}

/**
* @brief prepares this node to join the watr.li DODAG
* @return 0 on success
*/
static int watr_li_setup_node(void)
{
    radio_address_t iface_id = 0xffff;

    set_watr_li_channel(WATR_LI_CHANNEL);
    set_watr_li_pan(WATR_LI_PAN);
    iface_id = set_watr_li_if();

    /* choose addresses */
    ipv6_addr_init(&myaddr, 0x2015, 0x3, 0x18, 0x1111, 0x0, 0x0, 0x0, iface_id);

    /* and set it */
    //set_watr_li_address(&myaddr);

    return 0;
}

/**
* @brief initialize RPL on this watr.li node
* @return 0 on success
*/
static int watr_li_init_rpl(void)
{
    rpl_init(WATR_LI_IFACE, &myaddr);
    ipv6_iface_set_routing_provider(rpl_get_next_hop);
    return 0;
}

/**
* @brief create a thread to receive UDP messages
*/
static void watr_li_start_udp_server(void)
{
    thread_create(udp_server_stack_buffer,sizeof(udp_server_stack_buffer),
                  PRIORITY_MAIN, CREATE_STACKTEST, watr_li_udp_server, NULL,
                  "watr.li udp_server");
}

int main(void)
{
    puts("Hello watr.li!");

    printf("You are running RIOT on a(n) %s board.\n", RIOT_BOARD);
    printf("This board features a(n) %s MCU.\n", RIOT_MCU);

    char payload[80];
    int msgnum = 0;

    watr_li_setup_node();
    watr_li_init_rpl();
    watr_li_start_udp_server();


    while(1){
        sleep(30);
        snprintf(payload, 80, "watr.li node(%x) msg: %d", HTONS(myaddr.uint16[7]), msgnum++);
        watr_li_udp_send(payload, (strlen(payload) + 1));
        thread_yield();
    }

    return 0;
}
