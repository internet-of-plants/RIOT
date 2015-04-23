#include <stdio.h>
#include <string.h>
#include "vtimer.h"
#include "thread.h"
#include "net_if.h"
#include "sixlowpan.h"
#include "udp.h"
#include "transceiver.h"
#include "test_coap.h"
//#include "ps.h"
//#include "coap.h"

char addr_str[IPV6_MAX_ADDR_STR_LEN];
char monitor_stack_buffer[KERNEL_CONF_STACKSIZE_MAIN];
char udp_server_stack_buffer[KERNEL_CONF_STACKSIZE_MAIN];

#define UDP_BUFFER_SIZE (1024)

static void *init_udp_server(void *arg)
{
    (void) arg;

    sockaddr6_t sa;
    char buffer_main[UDP_BUFFER_SIZE];
    uint32_t fromlen;
    int sock = socket_base_socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);

    memset(&sa, 0, sizeof(sa));

    sa.sin6_family = AF_INET;
    sa.sin6_port = HTONS(12345);

    fromlen = sizeof(sa);

    if (-1 == socket_base_bind(sock, &sa, sizeof(sa))) {
        puts("[init_udp_server] Error bind failed!");
        socket_base_close(sock);
        return NULL;
    }

    while (1) {
        int32_t recsize = socket_base_recvfrom(sock, (void *)buffer_main, UDP_BUFFER_SIZE, 0, &sa, &fromlen);

        if (recsize < 0) {
            puts("[init_udp_server] ERROR: recsize < 0!");
        }

        printf("UDP packet received, payload: %s\n", buffer_main);
    }

    socket_base_close(sock);

    return NULL;
}

/**
* @brief init the network stack and start and UDP server
*/
int init_network(uint32_t chan, uint32_t pan, ipv6_addr_t* myaddr)
{
    net_if_set_src_address_mode(0, NET_IF_TRANS_ADDR_M_SHORT);
    radio_address_t id = net_if_get_hardware_address(0);

    transceiver_command_t tcmd;
    msg_t m;

    printf("[init_network] Setting HW address to %x\n", id);
    net_if_set_hardware_address(0, id);
    sixlowpan_lowpan_init_interface(0);

    ///////////////////////////////////
    ipv6_iface_set_routing_provider(NULL);
    ///////////////////////////////////

    /* kernel_pid_t udp_server_thread_pid = */
    thread_create(udp_server_stack_buffer,
    sizeof(udp_server_stack_buffer),
    PRIORITY_MAIN, CREATE_STACKTEST,
    init_udp_server,
    NULL,
    "init_udp_server");

    /* Set the last address part tio id */
    myaddr->uint16[7] = id;
    /* set host suffix */
    ipv6_addr_set_by_eui64(myaddr, 0, myaddr);
    ipv6_net_if_add_addr(0, myaddr, NDP_ADDR_STATE_PREFERRED, 0, 0, 0);

    /* set channel to chan */
    tcmd.transceivers = TRANSCEIVER_DEFAULT;
    tcmd.data = &chan;
    m.type = SET_CHANNEL;
    m.content.ptr = (void *) &tcmd;
    msg_send_receive(&m, &m, transceiver_pid);

    /* set panid to pan */
    tcmd.transceivers = TRANSCEIVER_DEFAULT;
    tcmd.data = &pan;
    m.type = SET_PAN;
    m.content.ptr = (void *) &tcmd;
    msg_send_receive(&m, &m, transceiver_pid);

    printf("[init_network] transceiver initialized, my address is %s\n",
     ipv6_addr_to_str(addr_str, IPV6_MAX_ADDR_STR_LEN,
    myaddr));
    return 0;
}

int main(void)
{
    ipv6_addr_t myaddr;
    /* Set prefix - the last address part (myaddr.uint16[7]) will be set in `init_network()` */
    ipv6_addr_init(&myaddr, 0x2015, 0x1, 0x28, 0x1111, 0x0, 0x0, 0x0, 0x0);

    /* initialize the network stack on channel 21 and pan 0x3e9 */
    init_network(21, 0x3e9, &myaddr);

//    thread_print_all();

    my_cool_coap_test_function();

    int sock;
    sockaddr6_t sa;
    ipv6_addr_t ipaddr;
    int bytes_sent;
    char text[] = "hallo!\0";

    sock = socket_base_socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);

    if (-1 == sock) {
        puts("[main] Error Creating Socket!");
        return 1;
    }

    memset(&sa, 0, sizeof(sa));

    ipv6_addr_init(&ipaddr, 0x2015, 0x1, 0x28, 0x1111, 0x0, 0x0, 0x00, 0x0a83);
    //ipv6_addr_set_all_nodes_addr(&ipaddr);

    sa.sin6_family = AF_INET;
    memcpy(&sa.sin6_addr, &ipaddr, 16);
    sa.sin6_port = HTONS(12345);

    bytes_sent = socket_base_sendto(sock, (char *)text,
    strlen(text) + 1, 0, &sa,
    sizeof(sa));

    if (bytes_sent < 0) {
        puts("[main] Error sending packet!");
    }
    else {
        printf("[main] Successful deliverd %i bytes over UDP to %s to 6LoWPAN\n",
        bytes_sent, ipv6_addr_to_str(addr_str, IPV6_MAX_ADDR_STR_LEN,
        &ipaddr));
    }

    socket_base_close(sock);
    puts("[main] socket closed");
    while(1){
        thread_yield();
    }
    return 0;
}
