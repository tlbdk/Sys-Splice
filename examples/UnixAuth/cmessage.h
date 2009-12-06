#include <sys/socket.h>
struct cmessage {
        struct cmsghdr cmsg;
        struct cmsgcred cmcred;
};
