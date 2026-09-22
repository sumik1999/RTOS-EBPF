#ifndef RTBPF_NET_SDK_H
#define RTBPF_NET_SDK_H

/* Restricted program-facing definitions. Do not include RTOS/driver headers. */
typedef unsigned char rtbpf_u8;
typedef unsigned short rtbpf_u16;
typedef unsigned int rtbpf_u32;
typedef unsigned long long rtbpf_u64;
typedef signed int rtbpf_s32;

#define SEC(name) __attribute__((section(name), used))
#define RTBPF_ALWAYS_INLINE __attribute__((always_inline)) inline

#define RTBPF_NET_ABORTED 0
#define RTBPF_NET_DROP 1
#define RTBPF_NET_PASS 2

#define RTBPF_LINK_WIFI_ETHERNET 1u
#define RTBPF_LINK_ETHERNET 2u
#define RTBPF_LINK_RAW_IP 3u

struct rtbpf_net_md_v1 {
    rtbpf_u32 data;
    rtbpf_u32 data_end;
    rtbpf_u32 ingress_port;
    rtbpf_u32 link_type;
    rtbpf_u32 packet_length;
    rtbpf_u32 window_epoch;
    rtbpf_s32 rssi_dbm;
    rtbpf_u32 channel;
    rtbpf_u64 timestamp_ns;
} __attribute__((packed));

#define RTBPF_HELPER_MAP_LOOKUP 1
static void *(*const rtbpf_map_lookup)(void *map, const void *key) =
    (void *)RTBPF_HELPER_MAP_LOOKUP;

/* Map declaration metadata is a packer contract; relocation support lands next. */
struct rtbpf_map_def {
    rtbpf_u32 type;
    rtbpf_u32 key_size;
    rtbpf_u32 value_size;
    rtbpf_u32 max_entries;
    rtbpf_u32 flags;
};

#define RTBPF_MAP_ARRAY 1u
#define RTBPF_MAP_CONFIG_ARRAY 2u
#define RTBPF_MAP_F_PROGRAM_WRITE (1u << 0)

#define RTBPF_ARRAY(name, value_type, entries, map_flags) \
    struct rtbpf_map_def SEC("rtbpf/maps") name = { \
        RTBPF_MAP_ARRAY, sizeof(rtbpf_u32), sizeof(value_type), (entries), (map_flags) \
    }

static RTBPF_ALWAYS_INLINE const rtbpf_u8 *
rtbpf_packet_data(const struct rtbpf_net_md_v1 *ctx)
{
    return (const rtbpf_u8 *)(unsigned long)ctx->data;
}

static RTBPF_ALWAYS_INLINE const rtbpf_u8 *
rtbpf_packet_end(const struct rtbpf_net_md_v1 *ctx)
{
    return (const rtbpf_u8 *)(unsigned long)ctx->data_end;
}

#endif
