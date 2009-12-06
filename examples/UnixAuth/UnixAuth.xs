#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include "cmessage.h"

MODULE = UnixAuth               PACKAGE = UnixAuth

int verify(fd)
        int fd
        CODE:
        struct iovec iov[1];
        struct msghdr msg;
                struct cmessage cm;
        char buf = '\0';
        bzero((char *)&cm, sizeof(cm));
        iov[0].iov_base = &buf;
        iov[0].iov_len = 1;

        msg.msg_iov = iov;
        msg.msg_iovlen = 1;
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
        msg.msg_control = (caddr_t)&cm;
        msg.msg_controllen = CMSG_SPACE(sizeof(struct cmessage));
        msg.msg_flags = 0;

        RETVAL = recvmsg(fd, &msg, 0);
        OUTPUT:
                RETVAL

int authenticate(fd)
        int fd
        CODE:
                struct iovec iov[1];
                struct msghdr msg;
                struct cmessage cm;
                char buf = '\0';
                int ret;

                bzero((char *)&cm, sizeof(cm));
                iov[0].iov_base = &buf;
                iov[0].iov_len = 1;

                cm.cmsg.cmsg_type = SCM_CREDS;
                cm.cmsg.cmsg_level = SOL_SOCKET;
                cm.cmsg.cmsg_len = CMSG_SPACE(sizeof(struct cmessage));

                msg.msg_iov = iov;
                msg.msg_iovlen = 1;
                msg.msg_name = NULL;
                msg.msg_namelen = 0;
                msg.msg_control = (caddr_t)&cm;
                msg.msg_controllen = CMSG_SPACE(sizeof(struct cmessage));
                msg.msg_flags = 0;

                RETVAL = sendmsg(fd, &msg, 0);
        OUTPUT:
                RETVAL
