#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>

struct __attribute__((packed, aligned(4))) netlink_attr {
    struct nlattr hdr;
    union {
        uint16_t u16;
        char s;
    };
};

struct __attribute__((packed, aligned(4))) netlink_message {
    struct nlmsghdr hdr;
    struct genlmsghdr genhdr;
    struct netlink_attr attrs[];
};

static uint32_t seqn = 0;

static struct netlink_message get_families_req() {
    struct netlink_message cmd = {
        .hdr = {
            .nlmsg_len = sizeof cmd,
            .nlmsg_type = GENL_ID_CTRL,
            .nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP,
            .nlmsg_seq = seqn++,
            .nlmsg_pid = 0
        },
        .genhdr = {
            .cmd = CTRL_CMD_GETFAMILY,
            .version = 1,
            .reserved = 0
        }
    };

    return cmd;
}

static void log_header(struct nlmsghdr *hdr) {
    printf("l: %4u, t: %hu, f: %hx, s: %u, p: %u | ",
        hdr->nlmsg_len,
        hdr->nlmsg_type,
        hdr->nlmsg_flags,
        hdr->nlmsg_seq,
        hdr->nlmsg_pid);
}

static void log_gen_header(struct genlmsghdr *hdr) {
    printf("c: %hhu | ", hdr->cmd);
}

static void log_attr(struct nlattr *attr) {
    printf("%hu %hu\n", attr->nla_type, attr->nla_len);
}

struct nl_resp_iter {
    char buf[BUFSIZ * 16];
    ssize_t n;
    int sock;
};

static struct nl_resp_iter nl_resp_iter(int sock) {
    return (struct nl_resp_iter) {
        .sock = sock,
        .buf = { 0 },
        .n = 0
    };
}

static bool next_message(struct netlink_message **msg, struct nl_resp_iter *iter) {
    struct nlmsghdr *hdr = (struct nlmsghdr *) *msg;

    if (iter->n) hdr = NLMSG_NEXT(hdr, iter->n);

    if (!NLMSG_OK(hdr, iter->n)) {
        iter->n = read(iter->sock, iter->buf, sizeof iter->buf);

        printf("bytes received: %zu\n", iter->n);

        if (iter->n < 0) {
            perror("error reading from socket");
            *msg = NULL;
            return false;
        }

        hdr = (struct nlmsghdr *) iter->buf;
    }

    *msg = (struct netlink_message *) hdr;
    uint16_t type = hdr->nlmsg_type;
    return type != NLMSG_ERROR && type != NLMSG_DONE;
}

int main(int argc, char *argv[]) {
    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
    if (sock == -1) {
        perror(NULL);
        goto fail;
    }

    struct netlink_message req = get_families_req();

    ssize_t sent = write(sock, &req, sizeof req);
    if (sent < 0) {
        perror("error sending request");
        goto fail;
    }

    struct nl_resp_iter iter = nl_resp_iter(sock);
    struct netlink_message *msg = NULL;

    while (next_message(&msg, &iter)) {
        log_header(&msg->hdr);
        log_gen_header(&msg->genhdr);
        printf("\n");

        struct netlink_attr *attr = msg->attrs;
        for (
            uint32_t n = msg->hdr.nlmsg_len - offsetof(struct netlink_message, attrs);
            n;
            n -= NLA_ALIGN(attr->hdr.nla_len),
            attr = (struct netlink_attr *) ((char *) attr + NLA_ALIGN(attr->hdr.nla_len))
        ) {
            // log_attr(&attr->hdr);
            switch (attr->hdr.nla_type) {
                case CTRL_ATTR_FAMILY_ID:
                    printf("family id: %hu\n", attr->u16);
                    break;
                case CTRL_ATTR_FAMILY_NAME:
                    printf("family name: %s\n", &attr->s);
                    break;
                case CTRL_ATTR_VERSION:
                    printf("family version: %hu\n", attr->u16);
                    break;
                case CTRL_ATTR_HDRSIZE:
                    printf("header size: %hu\n", attr->u16);
                    break;
                case CTRL_ATTR_MAXATTR:
                    printf("maxattr: %hu\n", attr->u16);
                    break;
                case CTRL_ATTR_OPS:
                    break;
                case CTRL_ATTR_MCAST_GROUPS:
                    break;
                case CTRL_ATTR_POLICY:
                    break;
            }
        }
    }

    if (msg->hdr.nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = (struct nlmsgerr *) ((char *) msg + sizeof *msg);
        fprintf(stderr, "netlink error encountered: %d\n", err->error);
        goto fail;
    }

    return EXIT_SUCCESS;
fail:
    close(sock);
    return EXIT_FAILURE;
}
