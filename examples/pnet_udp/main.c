#include <stdio.h>
#include <string.h>
#include "vtimer.h"
#include "thread.h"
#include "net_if.h"
#include "sixlowpan.h"
#include "udp.h"
#include "transceiver.h"

char addr_str[IPV6_MAX_ADDR_STR_LEN];
char monitor_stack_buffer[KERNEL_CONF_STACKSIZE_MAIN];

#define RCV_BUFFER_SIZE     (32)
msg_t msg_q[RCV_BUFFER_SIZE];

extern uint8_t ipv6_ext_hdr_len;
#define LL_HDR_LEN  (0x4)
#define IPV6_HDR_LEN    (0x28)

void *pnet_udp_monitor(void *arg)
{
    (void) arg;

    msg_t m;
    radio_packet_t *p;
    ipv6_hdr_t *ipv6_buf;
    uint8_t icmp_type, icmp_code;
    icmpv6_hdr_t *icmpv6_buf = NULL;

    msg_init_queue(msg_q, RCV_BUFFER_SIZE);
    while (1) {
        msg_receive(&m);

        if (m.type == PKT_PENDING) {
            p = (radio_packet_t *) m.content.ptr;

            printf("Received packet from ID %u\n", p->src);
            printf("\tLength:\t%u\n", p->length);
            printf("\tSrc:\t%u\n", p->src);
            printf("\tDst:\t%u\n", p->dst);
            printf("\tLQI:\t%u\n", p->lqi);
            printf("\tRSSI:\t%i\n", (int8_t) p->rssi);

            for (uint8_t i = 0; i < p->length; i++) {
                printf("%02X ", p->data[i]);
            }

            p->processing--;
            printf("\n");
        }
        else if (m.type == IPV6_PACKET_RECEIVED) {
            ipv6_buf = (ipv6_hdr_t *) m.content.ptr;
            printf("IPv6 datagram received (next header: %02X)", ipv6_buf->nextheader);
            printf(" from %s ", ipv6_addr_to_str(addr_str, IPV6_MAX_ADDR_STR_LEN,
            &ipv6_buf->srcaddr));

            if (ipv6_buf->nextheader == IPV6_PROTO_NUM_ICMPV6) {
                icmpv6_buf = (icmpv6_hdr_t *) &ipv6_buf[(LL_HDR_LEN + IPV6_HDR_LEN) + ipv6_ext_hdr_len];
                icmp_type = icmpv6_buf->type;
                icmp_code = icmpv6_buf->code;
            }

            if (ipv6_buf->nextheader == IPV6_PROTO_NUM_ICMPV6) {
                printf("\t ICMP type: %02X ", icmp_type);
                printf("\t ICMP code: %02X ", icmp_code);
                (void) icmp_type;
                (void) icmp_code;
            }

            printf("\n");
        }
        else if (m.type == ENOBUFFER) {
            puts("Transceiver buffer full");
        }
        else {
            printf("Unknown packet received, type %04X\n", m.type);
        }
    }

    return NULL;
}

int main(void)
{
    net_if_set_src_address_mode(0, NET_IF_TRANS_ADDR_M_SHORT);
    radio_address_t id = net_if_get_hardware_address(0);

    transceiver_command_t tcmd;
    msg_t m;
    uint32_t chan = 21;

    net_if_set_hardware_address(0, id);

/////////////////////////////////
    ipv6_iface_set_routing_provider(NULL);
///////////////////////////////////
puts("[main] provider NULLed");

    kernel_pid_t monitor_pid = thread_create(monitor_stack_buffer,
    sizeof(monitor_stack_buffer),
    PRIORITY_MAIN - 2,
    CREATE_STACKTEST,
    pnet_udp_monitor,
    NULL,
    "monitor");

    puts("[main] monitor started");



    transceiver_register(TRANSCEIVER_DEFAULT, monitor_pid);
    ipv6_register_packet_handler(monitor_pid);

    /* add global address */
    ipv6_addr_t tmp;
    /* initialize prefix */
    ipv6_addr_init(&tmp, 0x2001, 0x0db8, 0x0001, 0x0, 0x0, 0x0, 0x0, 0x0003);
    /* set host suffix */
    ipv6_addr_set_by_eui64(&tmp, 0, &tmp);
    ipv6_net_if_add_addr(0, &tmp, NDP_ADDR_STATE_PREFERRED, 0, 0, 0);

    /* set channel to 21 */
    tcmd.transceivers = TRANSCEIVER_DEFAULT;
    tcmd.data = &chan;
    m.type = SET_CHANNEL;
    m.content.ptr = (void *) &tcmd;
    msg_send_receive(&m, &m, transceiver_pid);

    /* set panid to 0x3e9 */
    uint32_t pan = 0x3e9;
    tcmd.transceivers = TRANSCEIVER_DEFAULT;
    tcmd.data = &pan;
    m.type = SET_PAN;
    m.content.ptr = (void *) &tcmd;
    msg_send_receive(&m, &m, transceiver_pid);

//    printf("[main] setup transceiver: %d, %d, %d\n", transceiver_pid, KERNEL_PID_UNDEF, monitor_pid);


puts("[main] transceiver initialized");

    int sock;
    sockaddr6_t sa;
    ipv6_addr_t ipaddr;
    int bytes_sent;
    char text[] = "hallo!\0";
    int address = 2;

    sock = socket_base_socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);

    if (-1 == sock) {
        printf("Error Creating Socket!");
        return 1;
    }

    puts("[main] socket bound");
    memset(&sa, 0, sizeof(sa));

    if (address) {
        ipv6_addr_init(&ipaddr, 0x2001, 0x0db8, 0x0001, 0x0, 0x0, 0x0, 0x0, 0x0001);
    }
    else {
        ipv6_addr_set_all_nodes_addr(&ipaddr);
    }
    puts("[main] ipv6 address set");
    sa.sin6_family = AF_INET;
    memcpy(&sa.sin6_addr, &ipaddr, 16);
    sa.sin6_port = HTONS(12345);

    bytes_sent = socket_base_sendto(sock, (char *)text,
    strlen(text) + 1, 0, &sa,
    sizeof(sa));

    puts("[main] packet sent");
    if (bytes_sent < 0) {
        printf("Error sending packet!\n");
    }
    else {
        printf("Successful deliverd %i bytes over UDP to %s to 6LoWPAN\n",
        bytes_sent, ipv6_addr_to_str(addr_str, IPV6_MAX_ADDR_STR_LEN,
        &ipaddr));
    }

    socket_base_close(sock);
    thread_yield();
    puts("[main] socket closed");
    return 0;
}
