#ifndef MSG_H
#define MSG_H

struct msg {
	unsigned long msg_size;	/* length of crc data */
	unsigned long crc32;	/* data checksum */
}; /* real data follows this message */

#endif
