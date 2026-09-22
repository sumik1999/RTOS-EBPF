#include "rtbpf_net_sdk.h"

SEC("rtbpf/net_rx")
int drop_first_ff(struct rtbpf_net_md_v1 *ctx)
{
    const rtbpf_u8 *data = rtbpf_packet_data(ctx);
    const rtbpf_u8 *end = rtbpf_packet_end(ctx);

    if (data + 1 > end)
        return RTBPF_NET_PASS;
    if (data[0] == 0xff)
        return RTBPF_NET_DROP;
    return RTBPF_NET_PASS;
}
