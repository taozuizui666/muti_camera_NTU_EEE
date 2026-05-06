#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <pthread.h>

//#include <net/if.h>     /* for IFNAMSIZ and co... */
#include <linux/types.h>
#include <linux/wireless.h>
#include "bl_util.h"

#include <linux/netlink.h>
#include <signal.h>

#define IFNAMSIZ 16
int sockfd;
char iface_name[IFNAMSIZ];
int b_stop_flag = 0;
pthread_t event_task;
#define MAX_LINE_LENGTH 1096

//#define dbg_app printf
#define dbg_app

static int bl_util_send_cmd(int count, u8 *hex_array, struct iwreq * wrq,
                                  u32 cmd_id)
{
    int i = 0, temp, offset = 0, ret = BL_UTIL_SUCCESS;
    u8 *buf = NULL, *pos = NULL;

    pos = buf = (u8 *)malloc(BL_UTIL_BUFF_LEN);
    if (!buf) {
        printf("Error:%s, can't alloc command buf.\n", __func__);
        
        return BL_UTIL_FAIL;
    }

    memset(buf, 0, BL_UTIL_BUFF_LEN);
    memset(wrq, 0, sizeof(struct iwreq));
    strncpy(wrq->ifr_ifrn.ifrn_name, iface_name, IFNAMSIZ);
    wrq->ifr_ifrn.ifrn_name[IFNAMSIZ - 1] = '\0';
    wrq->u.data.pointer = (void *) buf;
    wrq->u.data.length = 0;

    // copy cmd id
    *(u32 *)pos = cmd_id;
    wrq->u.data.length += sizeof(cmd_id);
    pos += wrq->u.data.length;

    // hex hci cmd
    wrq->u.data.length += count;
    memcpy(pos, hex_array, count);
    
    dbg_app("dump ");
    for (i = 0; i < wrq->u.data.length; i++)
        dbg_app(":0x%x", *(buf + i));
    dbg_app("dump end\n");

    ret = ioctl(sockfd, BL_DEV_PRIV_IOCTL_DEFAULT, wrq);

    return ret;
}

static void bl_util_free_cmd(struct iwreq * wrq)
{
    if (wrq->u.data.pointer)
        free(wrq->u.data.pointer);
}

static void bl_util_sig_hdl(int sig)
{
    u8 hex_array[1];
    struct iwreq wrq;
    
    printf("%s 0x%x\n", __func__, sig);
    
    b_stop_flag = 1;

    bl_util_send_cmd(0, hex_array, &wrq, BL_UTIL_CMD_HCI_CMD);

    close(sockfd);
    pthread_join(event_task, NULL);

    exit(0);
}

static int bl_util_read_event(int sk_fd, struct nlmsghdr *nlmsg_hdr, 
                            struct msghdr *msg_hdr)
{
    int byte_cnt = 0;

    byte_cnt = recvmsg(sk_fd, msg_hdr, 0);

    if (byte_cnt < 0 || byte_cnt > NLMSG_SPACE(BL_NL_BUF_MAX_LEN))
        return -1;

    return byte_cnt;
}

/* Return total nlmsg len, include <NLMSG_HDRLEN + NLMSG_DATA(nlmsg_hdr)>
 * NLMSG_DATA(nlmsg_hdr) is struct bl_nl_event 
 * struct bl_nl_event.event_id is enum bl_event_id
 * struct bl_nl_event.payload  is full ieee80211_mgmt frame following  kernel struct <struct ieee80211_mgmt>
 * struct bl_nl_event.payload_len is ieee80211_mgmt frame len
*/
static int bl_util_wait_event(int nl_socket, int timeout_s,
                           struct nlmsghdr *nlmsg_hdr, struct msghdr *msg_hdr)
{
    struct timeval * timeout = NULL, time;
    fd_set read_fds;
    int i = 0, ret = 0;

    FD_ZERO(&read_fds);
    FD_SET(nl_socket, &read_fds);

    /* Initialize timeout value */
    if (timeout_s != 0) {
        time.tv_sec = 0;
        time.tv_usec = timeout_s;
        timeout = &time;
    }

    ret = select(nl_socket + 1, &read_fds, NULL, NULL, timeout);
    if (ret == -1) {
        // wait abnormal
        b_stop_flag = 1;
        
        return ret;
    } else if (ret == 0) {
        //wait timeout
        
        return ret;
    }

    if (FD_ISSET(nl_socket, &read_fds)) {
        ret = bl_util_read_event(nl_socket, nlmsg_hdr, msg_hdr);
    }

    return ret;
}

static void *bl_util_event_loop(void *p_args)
{
    int i = 0, ret = 0;
    int nl_socket = -1;
    struct nlmsghdr *nlmsg_hdr = NULL;
    struct msghdr    msg_hdr;
    struct iovec     io_vec;
    struct sockaddr_nl src_addr, dest_addr;
    struct bl_nl_event * bl_event;

    b_stop_flag = 0;
    memset(&src_addr, 0, sizeof(src_addr));
    memset(&dest_addr, 0, sizeof(dest_addr));

    nl_socket = socket(PF_NETLINK, SOCK_RAW, BL_NL_SOCKET_NUM);
    if (nl_socket < 0) {
        printf("%s socket creat fail\n", __func__);
        goto done;
    }

    src_addr.nl_family = AF_NETLINK;
    src_addr.nl_pid = getpid();
    src_addr.nl_groups = BL_NL_BCAST_GROUP_ID;

    dest_addr.nl_family = AF_NETLINK;
    dest_addr.nl_pid = 0;
    dest_addr.nl_groups = BL_NL_BCAST_GROUP_ID;

    if (bind(nl_socket, (struct sockaddr *) &src_addr, sizeof(src_addr)) < 0) {
        printf("%s socket%d bind fail\n", __func__, nl_socket);
        
        goto done;
    }

    nlmsg_hdr = (struct nlmsghdr *) malloc(NLMSG_SPACE(BL_NL_BUF_MAX_LEN));
    if (!nlmsg_hdr) {
        printf("%s nlmsg buffer alloc fail\n", __func__);
        
        goto done;
    }
    memset(nlmsg_hdr, 0, NLMSG_SPACE(BL_NL_BUF_MAX_LEN));

    io_vec.iov_base = (void *) nlmsg_hdr;
    io_vec.iov_len = NLMSG_SPACE(BL_NL_BUF_MAX_LEN);

    memset(&msg_hdr, 0, sizeof(struct msghdr));
    msg_hdr.msg_name = (void *) &dest_addr;
    msg_hdr.msg_namelen = sizeof(dest_addr);
    msg_hdr.msg_iov = &io_vec;
    msg_hdr.msg_iovlen = 1;

    dbg_app("loop event started\n");
    
    while (1) {
        if (b_stop_flag) {
            printf("loop event stop!\n");
            
            break;
        }
        ret = bl_util_wait_event(nl_socket, 500*1000, nlmsg_hdr, &msg_hdr);

        if (ret == -1) {
            printf("wait abnormal, try again\n");
            
            continue;
        }
        if (ret == 0) {
            continue;
        }

        bl_event = (struct bl_nl_event *)NLMSG_DATA(nlmsg_hdr);

        dbg_app("%s recv_len=%d, event_id=0x%x, nlmsg_len=%d seq=%d payload_len=%d\n",
               __func__, ret,
               bl_event->event_id, nlmsg_hdr->nlmsg_len, nlmsg_hdr->nlmsg_seq, 
               bl_event->payload_len);

        switch (bl_event->event_id) {
            case BL_EVENT_ID_HCI_MSG:
                {
                    char rsp[MAX_LINE_LENGTH];
                    int rsp_len = 0;
                    
                    dbg_app("BL_EVENT_ID_HCI_MSG received\n");

                    memset(rsp, 0, sizeof(rsp));
                    
                    for (i = 0; i < bl_event->payload_len; i++)
                        rsp_len += sprintf(rsp+rsp_len, "%02x ", *(bl_event->payload + i));

                    printf("%s\n", rsp);
                }
                break;

            default:
                printf("unknown event:0x%x len=%d\n",
                       bl_event->event_id, bl_event->payload_len);
                break;
        }

        fflush(stdout);
    }

done:
    if (nl_socket > 0)
        close(nl_socket);
    if (nlmsg_hdr)
        free(nlmsg_hdr);

    return NULL;
}

static int parse_hex_string(const char *input, u8 *array, int max_size) {
    char *token;
    char *input_copy;
    int count = 0;
    
    input_copy = strdup(input);
    if (!input_copy) {
        fprintf(stderr, "Memory allocation failed\n");
        
        return -1;
    }
    
    token = strtok(input_copy, " \t\n\r");
    
    while (token != NULL && count < max_size) {
        char *endptr;
        unsigned long value = strtoul(token, &endptr, 16);
        
        if (*endptr == '\0') {
            array[count] = (u8)value;
            count++;
        } else {
            fprintf(stderr, "Invalid hex value: %s\n", token);
            count = 0;
            break;
        }
        
        token = strtok(NULL, " \t\n\r");
    }
    
    free(input_copy);
    
    return count;
}

//1. start program
//./bl_hci wlan0 
//interface wlan0


//2. input hex format hci command
//01 03 0c 00
//01 1e 20 03 26 3c 01
int main(int argc, char * argv[])
{
    int i = 0, ret = -1;
    struct util_cmd_node * util_cmd = NULL;
    char input[MAX_LINE_LENGTH];
    u8 hex_array[MAX_LINE_LENGTH];
    int count;
    struct iwreq wrq;

    if(argc < 2) {
        fprintf(stderr, "wrong param num! Run:./bl_hci wlan0\n");
        
        return -1;
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        printf("creat global socket fail\n");
        
        return -1;
    }

    strncpy(iface_name, argv[1], IFNAMSIZ);

    dbg_app("argc=%d, interface %s\n", argc, iface_name);

    signal(SIGTERM, bl_util_sig_hdl);
    signal(SIGINT, bl_util_sig_hdl);
    signal(SIGALRM, bl_util_sig_hdl);

    ret = pthread_create(&event_task, NULL, bl_util_event_loop, NULL);
    if(ret) {
        printf("thread_created\r\n");
        
        return -1;
    }

    while (!b_stop_flag) {
        dbg_app("Input:");
        
        fflush(stdout);

        memset(hex_array, 0, sizeof(hex_array));
        
        #if 0
        count = fread(hex_array, 1, sizeof(hex_array), stdin);
        
        if (count == 0) {
            if (feof(stdin)) {
                printf("EOF detected, exiting...\n");
                break;
            } else if (ferror(stdin)) {
                printf("Error reading input\n");
                break;
            }
        }

        if (count >= 4 && strncmp((char*)hex_array, "quit", 4) == 0) {
            printf("Goodbye!\n");
            break;
        }
        if (count >= 4 && strncmp((char*)hex_array, "exit", 4) == 0) {
            printf("Goodbye!\n");
            break;
        }
        if (count >= 1 && hex_array[0] == 'q') {
            printf("Goodbye!\n");
            break;
        }        
        #else
        memset(input, 0, sizeof(input));
        
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        
        input[strcspn(input, "\n")] = '\0';
        
        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0 ||
            strcmp(input, "q") == 0)
        {
            printf("Goodbye!\n");

            break;
        }
        
        count = parse_hex_string(input, hex_array, MAX_LINE_LENGTH);
        #endif
        
        if (count) {
            ret = bl_util_send_cmd(count, hex_array, &wrq, BL_UTIL_CMD_HCI_CMD);
            if (ret) {
                printf("Error: bl_util_scan %d\n", ret);
            }

            dbg_app("send done\r\n");

            bl_util_free_cmd(&wrq);
        }
    }

    count = 0;
    ret = bl_util_send_cmd(count, hex_array, &wrq, BL_UTIL_CMD_HCI_CMD);
    fflush(stdout);

    b_stop_flag = 1;

    pthread_join(event_task, NULL);
    close(sockfd);
    
    return ret;
}

