#ifndef __UPLINK_MIRRORING_H__
#define __UPLINK_MIRRORING_H__

#include <linux/kernel.h>

#define WAN_IF_NAME     "eth1"
#define LAN_IF_NAME     "eth0"
#define BR_LAN_NAME     "br-lan"

#define UPLINK_MIRROR_MODULE_TAG    "[UL_MIRROR] "

#define UM_DBG(fmt, ...) pr_debug(UPLINK_MIRROR_MODULE_TAG fmt, ##__VA_ARGS__)
#define UM_INFO(fmt, ...) pr_info(UPLINK_MIRROR_MODULE_TAG fmt, ##__VA_ARGS__)
#define UM_WARN(fmt, ...) pr_warn(UPLINK_MIRROR_MODULE_TAG fmt, ##__VA_ARGS__)
#define UM_ERR(fmt, ...) pr_err(UPLINK_MIRROR_MODULE_TAG fmt, ##__VA_ARGS__)

#endif /* END __UPLINK_MIRRORING_H__ */
