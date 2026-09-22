#ifndef RTBPF_MAPS_H
#define RTBPF_MAPS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RTBPF_MAP_ARRAY = 1,
    RTBPF_MAP_CONFIG_ARRAY = 2,
} rtbpf_map_type_t;

typedef struct {
    rtbpf_map_type_t type;
    uint32_t key_size;
    uint32_t value_size;
    uint32_t max_entries;
    uint8_t *storage;
    size_t storage_size;
    uint32_t flags;
} rtbpf_map_t;

#define RTBPF_MAP_F_PROGRAM_WRITE (1u << 0)

int rtbpf_map_array_init(rtbpf_map_t *map, rtbpf_map_type_t type,
                         uint32_t value_size, uint32_t max_entries,
                         void *storage, size_t storage_size, uint32_t flags);
void *rtbpf_map_lookup(rtbpf_map_t *map, const void *key, size_t key_size);
const void *rtbpf_map_lookup_const(const rtbpf_map_t *map,
                                   const void *key, size_t key_size);

#ifdef __cplusplus
}
#endif
#endif
