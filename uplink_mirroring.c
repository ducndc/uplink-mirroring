/**
 * uplink_mirroring.c
 *
 * Copyright (c) 2025 Chung Duc Nguyen Dang
 *
 */

#include "uplink_mirroring.h"

#include <linux/module.h>
#include <linux/netfilte.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/if_ether.h>

static struct net_device *g_pWanDev = NULL;
static struct net_device *g_pLanDev = NULL;
static bool g_mirrorEnable = false;

static ssize_t 
EnabledShow(
    struct kobject *kobj,
    struct kobject_attribute *attr,
    char *buf
)
{
    return sprintf(buf, "%d\n", g_mirrorEnable);
}

static ssize_t
EnabledStore(
    struct kobject *kobj,
    struct kobject_attribute *attr,
    const char *buf,
    size_t count
)
{
    int ret;
    bool newValue;

    ret = kstrtobool(buf, &newValue);

    if (ret) {
        return ret;
    }

    g_mirrorEnable = newValue;

    UM_INFO("Uplink Mirror: %s\n", (g_mirrorEnable ? "Enable" : "Disable"));

    return count;
}

static struct kobject_attribute g_enableAttribute = 
    __ATTR(enabled, 0664, EnabledShow, EnabledStore);

static struct attribute *g_pAttrs[] = {
    &g_enableAttribute.attr,
    NULL,
};

static struct attribute_group g_attrGroup = {
    .attrs = g_pAttrs,
};

static struct kobject *g_pMirrorKobj;

static bool
IsIcmpPacket(
    struct sk_buff *skb
)
{
    struct iphdr *iph;

    if (!skb || (skb->protocol != htons(ETH_P_IP))) {
        return false;
    }

    iph = ip_hdr(skb);

    if (!iph) {
        return false;
    }

    return (iph->protocol == IPPROTO_ICMP);
}

static void
InspectSkb(
    struct sk_buff *skb
)
{
    struct ethhdr *eth;

    if (!skb) {
        return;
    }

    eth = eth_hdr(skb);

    if (!eth) {
        return;
    }

    UM_INFO("ETH src=%pM dst=%pM proto=%0x%04x\n",
            eth->h_source, eth->h_dest, ntohs(eth->h_proto));
}

static void
MirrorPacketPreRouting(
    struct sk_buff *skb,
    struct net_device *outDev
)
{
    struct sk_buff *nskb;
    int ret;

    if (!g_mirrorEnable || !outDev || !netif_running(outDev)) {
        return;
    }

    if (!IsIcmpPacket(skb)) {
        return;
    }

    nskb = skb_clone(skb, GFP_ATOMIC);

    if (!nskb) {
        return;
    }

    nskb->dev = outDev;
    nskb->pkt_type = PACKET_OUTGOING;
    nskb->protocol = htons(ETH_P_IP);
    nskb->ip_summed = CHECKSUM_NONE;

    skb_push(nskb, ETH_HLEN);
    InspectSkb(nskb);
    ret = dev_queue_xmit(nskb);

    if (ret != NETDEV_TX_OK) {
        UM_ERR("mirror fail %s (%d)\n", outDev->name, ret);
    } else {
        UM_INFO("Send packet in wan to %s (%d)\n", outDev->name, ret);
    }
}

static void 
MirrorPacketPostRouting(
    struct sk_buff *skb,
    struct net_device *outDev
)
{
    struct sk_buff *nskb;
    int ret;

    if (!g_mirrorEnable || !outDev || !netif_running(outDev)) {
        return;
    }

    if (!IsIcmpPacket(skb)) {
        return;
    }

    nskb = skb_clone(skb, GFP_ATOMIC);

    if (!nskb) {
        return;
    }

    nskb->dev = outDev;
    nskb->ip_summed = CHECKSUM_NONE;
    skb_push(nskb, ETH_HLEN);
    skb_reset_mac_header(nskb);
    InspectSkb(nskb);
    ret = dev_queue_xmit(nskb);

    if (ret != NETDEV_TX_OK) {
        UM_ERR("mirror fail %s (%d)\n", outDev->name, ret);
    } else {
        UM_INFO("Send packet out wan to %s (%d)\n", outDev->name, ret);
    }
}

static unsigned int
HookPreRouting(
    void *priv,
    struct sk_buff *skb,
    const struct nf_hook_state *state
)
{
    if (state->in == g_pWanDev) {
        MirrorPacketPreRouting(skb, g_pLanDev);
    }

    return NF_ACCEPT;
}

static unsigned int
HookPostRouting(
    void *priv,
    struct sk_buff *skb,
    const struct nf_hook_state *state
)
{
    if (state->out == g_pWanDev) {
        MirrorPacketPostRouting(skb, g_pLanDev);
    }

    return NF_ACCEPT;
}

static struct nf_hook_ops g_uplinkMirrorNfOps[] __read_mostly = {
    {
        .hook = HookPreRouting,
        .pf = PF_INET,
        .hooknum = NF_INET_PRE_ROUTING,
        .priority = NF_IP_PRI_FIRST,
    },
    {
        .hook = HookPostRouting,
        .pf = PF_INET,
        .hooknum = NF_INET_POST_ROUTING,
        .priority = NF_IP_PRI_LAST,
    },
};

static int __init MirrorInit(void)
{
    int ret;

    g_pWanDev = dev_get_by_name(&init_net, WAN_IF_NAME);
    g_pLanDev = dev_get_by_name(&init_net, LAN_IF_NAME);

    if (!g_pWanDev || !g_pLanDev) {
        UM_ERR("Failed to get net device\n");
        return -ENODEV;
    }

    g_pMirrorKobj = kobject_create_and_add("uplink_mirror", kernel_kobj);

    if (!g_pMirrorKobj) {
        UM_ERR("Failed to create sysfs entry\n");
        return -ENOMEM;
    }

    ret = sysfs_create_group(g_pMirrorKobj, &g_attrGroup);

    if (ret) {
        UM_ERR("Failed to create sysfs group\n");
        kobject_put(g_pMirrorKobj);
        return ret;
    }

    ret = nf_register_net_hooks(&init_net, g_uplinkMirrorNfOps,
                                ARRAY_SIZE(g_uplinkMirrorNfOps));

    if (ret) {
        goto err1;
    }

    UM_INFO("Uplink mirroring module loaded\n");

    return 0;

err1:
    if (g_pLanDev) {
        dev_put(g_pLanDev);
    }

    if (g_pWanDev) {
        dev_put(g_pWanDev);
    }

    return ret;
}

static void __exit MirrorExit(void)
{
    nf_unregister_net_hooks(&init_net, g_uplinkMirrorNfOps,
                            ARRAY_SIZE(g_uplinkMirrorNfOps));

    if (g_pWanDev) {
        dev_put(g_pWanDev);
    }

    if (g_pLanDev) {
        dev_put(g_pLanDev);
    }

    sysfs_remove_group(g_pMirrorKobj, &g_attrGroup);
    kobject_put(g_pMirrorKobj);

    UM_INFO("Uplink mirror module unloaded\n");
}

module_init(MirrorInit);
module_exit(MirrorExit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chung Duc Nguyen Dang");
MODULE_DESCRIPTION("Mirror packets from Wan to Lan for debugging");
