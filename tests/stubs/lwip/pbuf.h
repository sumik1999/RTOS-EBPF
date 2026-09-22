#ifndef TEST_LWIP_PBUF_H
#define TEST_LWIP_PBUF_H
#include <stdint.h>
typedef uint16_t u16_t;
#define PBUF_RAW 0
#define PBUF_REF 0
struct pbuf { void *payload; u16_t len; u16_t tot_len; };
struct pbuf_custom { struct pbuf pbuf; void (*custom_free_function)(struct pbuf *); };
struct pbuf *pbuf_alloced_custom(int layer, u16_t length, int type,
                                 struct pbuf_custom *custom, void *payload,
                                 u16_t payload_length);
u16_t pbuf_free(struct pbuf *pb);
#endif
