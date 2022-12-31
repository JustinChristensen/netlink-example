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
        struct nlattr attrs[1];
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

static struct netlink_message families_req() {
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
    printf("-----------------------------------------------------------------\n");
    printf("length: %4u, type: %hu, flags: 0x%hx, seq: %u, pid: %u",
        hdr->nlmsg_len,
        hdr->nlmsg_type,
        hdr->nlmsg_flags,
        hdr->nlmsg_seq,
        hdr->nlmsg_pid);
}

static void log_gen_header(struct genlmsghdr *hdr) {
    printf(", cmd: %hhu", hdr->cmd);
}

static void log_attr(struct nlattr *attr) {
    // printf("%hu %hu\n", attr->nla_type, attr->nla_len);

    printf("0x");
    for (int i = 0; i < attr->nla_len / sizeof (int); i++) {
        printf("%08x|", ((unsigned *) attr)[i]);
    }
    printf("\n");
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

struct netlink_attr_iter {
    uint32_t const len;
    uint32_t n;
    void *attrbuf;
};

static struct netlink_attr_iter netlink_attr_iter(void *attrs, uint32_t len) {
    return (struct netlink_attr_iter) {
        .attrbuf = attrs,
        .len = len,
        .n = 0
    };
}

static bool next_netlink_attr(struct netlink_attr **attr, struct netlink_attr_iter *iter) {
    if (iter->n >= iter->len) {
        iter->n = 0;
        return false;
    }

    *attr = iter->attrbuf + iter->n;
    iter->n += NLA_ALIGN((*attr)->hdr.nla_len);
    return true;
}

void log_ops(struct netlink_attr *ops) {
    struct netlink_attr_iter ops_iter = netlink_attr_iter(ops->attrs,
        ops->hdr.nla_len - offsetof(struct netlink_attr, attrs));
    bool is_ops = ops->hdr.nla_type == CTRL_ATTR_OPS;

    printf(is_ops ? "operations:\n" : "mcast groups\n");
    while (next_netlink_attr(&ops, &ops_iter)) {
        struct netlink_attr_iter attr_iter = netlink_attr_iter(ops->attrs,
            ops->hdr.nla_len - offsetof(struct netlink_attr, attrs));
        struct netlink_attr *attr = NULL;

        printf("%3hu.", ops->hdr.nla_type);
        while (next_netlink_attr(&attr, &attr_iter)) {
            if (is_ops) {
                if (attr->hdr.nla_type == CTRL_ATTR_OP_ID) {
                    printf(" id: %3hu\n", attr->u16);
                } else if (attr->hdr.nla_type == CTRL_ATTR_OP_FLAGS) {
                    printf("     flags: 0x%hx\n", attr->u16);
                }
            } else {
                if (attr->hdr.nla_type == CTRL_ATTR_MCAST_GRP_NAME) {
                   printf("     name: %s\n", &attr->s);
                } else if (attr->hdr.nla_type == CTRL_ATTR_MCAST_GRP_ID) {
                    printf(" id: %3hu\n", attr->u16);
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {
    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
    if (sock == -1) {
        perror(NULL);
        goto fail;
    }

    struct netlink_message req = families_req();

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

        struct netlink_attr *attr = NULL;
        uint32_t attr_len = msg->hdr.nlmsg_len - offsetof(struct netlink_message, attrs);
        struct netlink_attr_iter attr_iter = netlink_attr_iter(msg->attrs, attr_len);

        while (next_netlink_attr(&attr, &attr_iter)) {
            switch (attr->hdr.nla_type & NLA_TYPE_MASK) {
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
                case CTRL_ATTR_MCAST_GROUPS:
                    log_ops(attr);
                    break;
                default:
                    log_attr(&attr->hdr);
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
