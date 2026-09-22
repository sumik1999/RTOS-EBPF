#ifndef TEST_W6X_API_H
#define TEST_W6X_API_H
#include <stdint.h>
#define W6X_NET_IF_STA 0u
#define W6X_NET_IF_AP 1u
int32_t W6X_Netif_input(uint32_t link_id, void **buffer, uint8_t **data);
int32_t W6X_Netif_free(void *buffer);
#endif
