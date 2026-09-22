#include "rtbpf/maps.h"

#include <string.h>

static int checked_storage_bytes(uint32_t value_size, uint32_t max_entries,
                                 size_t *bytes)
{
    if (value_size == 0u || max_entries == 0u || bytes == NULL) {
        return -1;
    }
    if ((size_t)max_entries > SIZE_MAX / (size_t)value_size) {
        return -1;
    }
    *bytes = (size_t)value_size * (size_t)max_entries;
    return 0;
}

int rtbpf_map_array_init(rtbpf_map_t *map, rtbpf_map_type_t type,
                         uint32_t value_size, uint32_t max_entries,
                         void *storage, size_t storage_size, uint32_t flags)
{
    size_t needed = 0u;
    if (map == NULL || storage == NULL ||
        (type != RTBPF_MAP_ARRAY && type != RTBPF_MAP_CONFIG_ARRAY) ||
        checked_storage_bytes(value_size, max_entries, &needed) != 0 ||
        needed > storage_size) {
        return -1;
    }

    map->type = type;
    map->key_size = sizeof(uint32_t);
    map->value_size = value_size;
    map->max_entries = max_entries;
    map->storage = (uint8_t *)storage;
    map->storage_size = needed;
    map->flags = flags;
    memset(map->storage, 0, needed);
    return 0;
}

void *rtbpf_map_lookup(rtbpf_map_t *map, const void *key, size_t key_size)
{
    uint32_t index = 0u;
    if (map == NULL || key == NULL || key_size != sizeof(index) ||
        map->storage == NULL) {
        return NULL;
    }
    memcpy(&index, key, sizeof(index));
    if (index >= map->max_entries) {
        return NULL;
    }
    return map->storage + ((size_t)index * map->value_size);
}

const void *rtbpf_map_lookup_const(const rtbpf_map_t *map,
                                   const void *key, size_t key_size)
{
    return rtbpf_map_lookup((rtbpf_map_t *)(uintptr_t)map, key, key_size);
}
