#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>

__attribute__((packed, aligned(4)))
struct netlink_cmd {
    struct nlmsghdr hdr;
    struct genlmsghdr genhdr;
};

static uint32_t seqn = 0;

struct netlink_cmd get_families_req() {
    struct netlink_cmd cmd = {
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

void log_header(struct nlmsghdr *hdr) {
    printf("| %u %hu %hx %u %u \n ",
        hdr->nlmsg_len,
        hdr->nlmsg_type,
        hdr->nlmsg_flags,
        hdr->nlmsg_seq,
        hdr->nlmsg_pid
    );
}

int main(int argc, char *argv[]) {
    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
    if (sock == -1) {
        perror(NULL);
        goto fail;
    }

    struct netlink_cmd request = get_families_req();

    ssize_t sent = write(sock, &request, sizeof request);
    if (sent < 0) {
        perror("error sending request");
        goto fail;
    }

    char buf[BUFSIZ * 16] = {0};
    ssize_t received = read(sock, buf, sizeof buf);

    while (received != 0) {
        printf("bytes received: %zu\n", received);

        if (received < 0) {
            perror("error receiving response");
            goto fail;
        }

        uint16_t type = NLMSG_DONE;

        for (struct nlmsghdr *hdr = (struct nlmsghdr *) buf; NLMSG_OK(hdr, received); hdr = NLMSG_NEXT(hdr, received)) {
            printf("bytes remaining: %zu ", received);

            type = hdr->nlmsg_type;
            if (type == NLMSG_DONE) break;

            if (type == NLMSG_ERROR) {
                struct nlmsgerr *err = (struct nlmsgerr *) ((char *) hdr + sizeof *hdr);
                fprintf(stderr, "netlink error encountered: %d\n", err->error);
                type = NLMSG_DONE;
            }

            printf("rocking and rolling\n");
        }

        if (type == NLMSG_DONE) break;

        received = read(sock, buf, sizeof buf);
    }


    return EXIT_SUCCESS;
fail:
    close(sock);
    return EXIT_FAILURE;
}
