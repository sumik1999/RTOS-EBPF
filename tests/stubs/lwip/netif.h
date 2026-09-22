#ifndef TEST_LWIP_NETIF_H
#define TEST_LWIP_NETIF_H
#include "lwip/err.h"
struct pbuf;
struct netif;
typedef err_t (*netif_input_fn)(struct pbuf *, struct netif *);
struct netif { netif_input_fn input; void *state; };
#endif
