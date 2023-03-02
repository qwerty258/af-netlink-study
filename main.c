#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <linux/rtnetlink.h>

#define BUFFER_SIZE 8192

void parse_rtattr(struct rtattr* tb[], int max, struct rtattr* rta, int len)
{
    memset(tb, 0, sizeof(struct rtattr*) * (max + 1));

    while (RTA_OK(rta, len))
    {
        if (rta->rta_type <= max)
        {
            tb[rta->rta_type] = rta;
        }
        rta = RTA_NEXT(rta, len);
    }
}

void parse_msg(uint8_t* buf, ssize_t size)
{
    struct nlmsghdr* h = (struct nlmsghdr*)buf;

    while (size >= sizeof(struct nlmsghdr))
    {
        if (((h->nlmsg_len - sizeof(struct nlmsghdr)) < 0) || (h->nlmsg_len > size))
        {
            printf("Invalid message length: %u\n", h->nlmsg_len);
            return;
        }

        switch (h->nlmsg_type)
        {
        case RTM_NEWLINK:
        case RTM_DELLINK:
        case RTM_GETLINK:
        {
            struct ifinfomsg* interface_info = (struct ifinfomsg*)NLMSG_DATA(h);
            char* interface_state = NULL;
            if (interface_info->ifi_flags & IFF_UP)
            {
                interface_state = "UP";
            }
            else
            {
                interface_state = "DOWN";
            }
            char* interface_run_state = NULL;
            if (interface_info->ifi_flags & IFF_RUNNING)
            {
                interface_run_state = "RUNNING";
            }
            else
            {
                interface_run_state = "NOT RUNNING";
            }


            struct rtattr* tb[IFLA_MAX + 1] = { 0 };
            parse_rtattr(tb, IFLA_MAX, IFLA_RTA(interface_info), h->nlmsg_type);


            char* interface_name = NULL;
            if (NULL != tb[IFLA_IFNAME])
            {
                interface_name = (char*)RTA_DATA(tb[IFLA_IFNAME]);
            }
            else
            {
                printf("IFLA_IFNAME attr not present\n");
            }

            uint32_t interface_carrier = 0;
            if (NULL != tb[IFLA_CARRIER])
            {
                interface_carrier = *(uint32_t*)RTA_PAYLOAD(tb[IFLA_CARRIER]);
            }
            else
            {
                printf("IFLA_CARRIER attr not present\n");
            }

            char* interface_operstate = NULL;
            if (NULL != tb[IFLA_OPERSTATE])
            {
                interface_operstate = (char*)RTA_DATA(tb[IFLA_OPERSTATE]);
            }
            else
            {
                printf("IFLA_OPERSTATE attr not present\n");
            }

            printf("Network interface %s, index: %d, carrier: %u, operstate: %s, state: %s, run state: %s\n",
                interface_name,
                interface_info->ifi_index,
                interface_carrier,
                interface_operstate,
                interface_state,
                interface_run_state);
        }
        break;
        default:
            printf("Unhandled message type: %d\n", h->nlmsg_type);
            break;
        }

        // align offsets by the message length, this is important
        size -= NLMSG_ALIGN(h->nlmsg_len);

        // move pointer to next message
        h = (struct nlmsghdr*)((uint8_t*)h + NLMSG_ALIGN(h->nlmsg_len));
    }
}

int socket_set_timeout(int fd)
{
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 250000;

    if (0 != setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timeval)))
    {
        printf("setsockopt() error: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    return 0;
}

int main(int argc, char* argv[])
{
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);

    if (fd < 0)
    {
        printf("socket() error: %s\n", strerror(errno));
        return 1;
    }

    struct sockaddr_nl local = { 0 };
    local.nl_family = AF_NETLINK;
    local.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE;
    local.nl_pid = getpid();

    uint8_t buf[BUFFER_SIZE];
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = BUFFER_SIZE;

    struct msghdr msg = { 0 };
    msg.msg_name = &local;
    msg.msg_namelen = sizeof(struct sockaddr_nl);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    if (0 != socket_set_timeout(fd))
        return 1;

    if (bind(fd, (struct sockaddr*)&local, sizeof(struct sockaddr_nl)) < 0)
    {
        printf("bind() error: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    while (true)
    {
        // block until timeout or message received
        ssize_t received_result = recvmsg(fd, &msg, 0);

        if (received_result < 0)
        {
            // receive was interrupted or timeout
            // continue to receive
            if (errno == EINTR || errno == EAGAIN)
            {
                continue;
            }

            printf("recvmsg() error: %s\n", strerror(errno));
            continue;
        }

        if (msg.msg_namelen != sizeof(struct sockaddr_nl))
        {
            printf("struct msghdr.msg_namelen mismatch\n");
            continue;
        }

        parse_msg(buf, received_result);
    }

    close(fd);

    return 0;
}
