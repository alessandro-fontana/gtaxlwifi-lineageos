/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Vendor "legacy" Wi-Fi HAL library for gtaxlwifi (§53).
 *
 * The Samsung Galaxy Tab A 10.1 (SM-T580) runs the mainline ath10k driver on
 * a QCA9377 over SDIO, and there is no vendor Wi-Fi HAL for it. Without one
 * Android cannot use the interface combinations the driver advertises (STA +
 * AP on one channel, P2P), and it takes a code path nobody else exercises.
 *
 * This library is what AOSP's default android.hardware.wifi-service loads
 * through /vendor/etc/wifi/vendor_hals/: it does not implement any of the
 * vendor features (gscan, RTT, logging, ...), which the service fills with
 * "not supported" stubs. It implements what the default HAL needs to manage
 * interfaces on a plain cfg80211/mac80211 driver:
 *
 *  - the lifecycle (initialize, event loop, cleanup),
 *  - the list of wireless interfaces, taken from /sys/class/net/<if>/phy80211,
 *  - creating and deleting virtual interfaces with nl80211, so that the soft
 *    AP gets its own wlan1 next to the station on wlan0,
 *  - the regulatory country, through NL80211_CMD_REQ_SET_REG,
 *  - the Wake-on-WLAN triggers (§68), so that the station stays associated
 *    across a system suspend and traffic for the tablet wakes it up.
 *
 * wlan0 belongs to the driver and is never deleted.
 */

#define LOG_TAG "WifiHalGtaxl"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/nl80211.h>
#include <netlink/genl/ctrl.h>
#include <netlink/errno.h>
#include <netlink/genl/genl.h>
#include <netlink/msg.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <log/log.h>

#include "hardware_legacy/wifi_hal.h"

namespace {

constexpr char kPrimaryIface[] = "wlan0";
constexpr char kSysNet[] = "/sys/class/net";

}  // namespace

struct wifi_interface_info {
    char name[IFNAMSIZ];
};

struct wifi_info {
    std::mutex lock;
    int stop_pipe[2] = {-1, -1};
    // One object per name, never freed: the service keeps handles across
    // rescans, and a handle must not outlive the memory it points to.
    std::map<std::string, wifi_interface_info*> pool;
    std::vector<wifi_interface_handle> handles;
    std::set<std::string> created;  // interfaces this library has added
};

namespace {

wifi_info* asInfo(wifi_handle h) {
    return reinterpret_cast<wifi_info*>(h);
}

bool readSysfs(const std::string& path, std::string* out) {
    FILE* f = fopen(path.c_str(), "re");
    if (!f) return false;
    char buf[64] = {};
    bool ok = fgets(buf, sizeof(buf), f) != nullptr;
    fclose(f);
    if (!ok) return false;
    out->assign(buf);
    while (!out->empty() && (out->back() == '\n' || out->back() == '\r')) out->pop_back();
    return true;
}

/* Index of the wiphy behind an interface, -1 if it is not a wireless one. */
int wiphyIndex(const char* ifname) {
    std::string value;
    if (!readSysfs(std::string(kSysNet) + "/" + ifname + "/phy80211/index", &value)) return -1;
    return atoi(value.c_str());
}

/*
 * Whether a network interface exists, asked to sysfs. An ioctl on a name that
 * does not exist (if_nametoindex) makes the kernel consider loading a module
 * called netdev-<name> (dev_load), and the capability check for that is an
 * audited sys_module denial in hal_wifi_default.
 */
bool ifaceExists(const char* ifname) {
    return access((std::string(kSysNet) + "/" + ifname).c_str(), F_OK) == 0;
}

bool readMac(const char* ifname, uint8_t mac[6]) {
    std::string value;
    if (!readSysfs(std::string(kSysNet) + "/" + ifname + "/address", &value)) return false;
    unsigned int b[6];
    if (sscanf(value.c_str(), "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
        return false;
    for (int i = 0; i < 6; i++) mac[i] = b[i];
    return true;
}

/*
 * One nl80211 request and its ack, on a socket of its own. Interface changes
 * are rare enough that a socket per request costs nothing and keeps the
 * library free of shared netlink state.
 */
class Nl80211 {
  public:
    Nl80211() {
        sock_ = nl_socket_alloc();
        if (!sock_) return;
        if (genl_connect(sock_) < 0) return;
        family_ = genl_ctrl_resolve(sock_, "nl80211");
    }
    ~Nl80211() {
        if (sock_) nl_socket_free(sock_);
    }
    bool ok() const { return sock_ && family_ >= 0; }

    nl_msg* message(uint8_t cmd) {
        nl_msg* msg = nlmsg_alloc();
        if (msg && !genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, family_, 0, 0, cmd, 0)) {
            nlmsg_free(msg);
            return nullptr;
        }
        return msg;
    }

    /* Sends and waits for the ack; returns 0 or a negative libnl error. */
    int send(nl_msg* msg) {
        int err = nl_send_auto(sock_, msg);
        nlmsg_free(msg);
        if (err < 0) return err;
        err = nl_wait_for_ack(sock_);
        return err < 0 ? err : 0;
    }

  private:
    nl_sock* sock_ = nullptr;
    int family_ = -1;
};

uint32_t toNl80211Type(wifi_interface_type type) {
    switch (type) {
        case WIFI_INTERFACE_TYPE_AP:
            return NL80211_IFTYPE_AP;
        case WIFI_INTERFACE_TYPE_STA:
        case WIFI_INTERFACE_TYPE_P2P:
            /*
             * The framework wants a real netdev called p2p0 to hand to
             * wpa_supplicant (§49.8), which then creates the P2P group
             * interfaces itself: a station-type netdev is what that takes.
             */
            return NL80211_IFTYPE_STATION;
        default:
            return NL80211_IFTYPE_UNSPECIFIED;
    }
}

void refreshIfaces(wifi_info* info) {
    info->handles.clear();

    DIR* dir = opendir(kSysNet);
    if (!dir) {
        ALOGE("cannot open %s: %s", kSysNet, strerror(errno));
        return;
    }
    while (dirent* entry = readdir(dir)) {
        if (entry->d_name[0] == '.') continue;
        if (wiphyIndex(entry->d_name) < 0) continue;
        auto& iface = info->pool[entry->d_name];
        if (!iface) {
            iface = new wifi_interface_info();
            strlcpy(iface->name, entry->d_name, sizeof(iface->name));
        }
        info->handles.push_back(iface);
    }
    closedir(dir);
}

/*
 * Wake-on-WLAN (§68). Without a WoWLAN configuration cfg80211 disconnects
 * every interface when the system suspends (wiphy_suspend() ->
 * cfg80211_leave_all()); with one, ath10k hands the association to the
 * firmware and the firmware wakes the host on a matching frame. Android does
 * not configure WoWLAN, and what has to match depends on the addresses of the
 * moment (the MAC is randomized per network, the IP comes from DHCP), so the
 * library keeps it up to date while the HAL runs:
 *
 *  - disconnect and magic packet;
 *  - any unicast frame for wlan0's MAC: data for the tablet;
 *  - ARP requests for wlan0's IPv4 address, or the router forgets where
 *    the tablet is while it sleeps (ath10k does not program the firmware's
 *    ARP offload);
 *  - Neighbor Solicitations for each IPv6 address of wlan0, by the
 *    solicited-node multicast MAC.
 *
 * Patterns are 802.3 frames; ath10k converts them to 802.11 for the firmware.
 *
 * With the QCA9377 SDIO firmware the kernel offers no WoWLAN (§68.22: the
 * firmware hangs the first time it has to wake the host), so SET_WOWLAN
 * fails with EOPNOTSUPP, logged once per change, and suspend disconnects.
 */
struct WowPattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> used;  // which bytes of |bytes| must match
};

/*
 * The IPv4 address of an interface. SIOCGIFADDR on a datagram socket:
 * /proc/net/fib_trie would list them too, but Android makes it 0400 root.
 */
std::vector<uint32_t> ipv4Of(const char* ifname) {
    std::vector<uint32_t> out;
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return out;
    ifreq ifr = {};
    strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
    if (ioctl(fd, SIOCGIFADDR, &ifr) == 0) {
        auto* sin = reinterpret_cast<sockaddr_in*>(&ifr.ifr_addr);
        out.push_back(sin->sin_addr.s_addr);
    }
    close(fd);
    return out;
}

/* IPv6 addresses of an interface, as the last three bytes that make up its
 * solicited-node multicast address. */
std::vector<std::vector<uint8_t>> ipv6Tails(const char* ifname) {
    std::vector<std::vector<uint8_t>> out;
    FILE* f = fopen("/proc/net/if_inet6", "re");
    if (!f) return out;
    char addr[40], name[IFNAMSIZ + 1];
    unsigned int index, plen, scope, flags;
    while (fscanf(f, "%32s %x %x %x %x %16s", addr, &index, &plen, &scope, &flags, name) == 6) {
        if (strcmp(name, ifname) != 0 || strlen(addr) != 32) continue;
        std::vector<uint8_t> tail(3);
        for (int i = 0; i < 3; i++) {
            unsigned int b;
            sscanf(addr + 26 + 2 * i, "%2x", &b);
            tail[i] = b;
        }
        out.push_back(tail);
    }
    fclose(f);
    return out;
}

std::vector<WowPattern> wowPatterns(const uint8_t mac[6]) {
    std::vector<WowPattern> out;
    std::vector<uint32_t> ipv4 = ipv4Of(kPrimaryIface);
    std::vector<std::vector<uint8_t>> ipv6 = ipv6Tails(kPrimaryIface);

    WowPattern unicast{std::vector<uint8_t>(mac, mac + 6), std::vector<bool>(6, true)};
    out.push_back(unicast);

    // ARP request (EtherType 0x0806) whose target protocol address, at
    // offset 38 of the 802.3 frame, is ours.
    for (uint32_t ip : ipv4) {
        WowPattern arp{std::vector<uint8_t>(42, 0), std::vector<bool>(42, false)};
        arp.bytes[12] = 0x08;
        arp.bytes[13] = 0x06;
        arp.used[12] = arp.used[13] = true;
        memcpy(&arp.bytes[38], &ip, 4);
        for (int i = 38; i < 42; i++) arp.used[i] = true;
        out.push_back(arp);
    }

    for (const auto& tail : ipv6) {
        WowPattern ns{{0x33, 0x33, 0xff, tail[0], tail[1], tail[2]}, std::vector<bool>(6, true)};
        out.push_back(ns);
    }
    return out;
}

std::string describe(const std::vector<WowPattern>& patterns) {
    std::string out;
    char hex[4];
    for (const auto& p : patterns) {
        for (size_t i = 0; i < p.bytes.size(); i++) {
            if (!p.used[i]) continue;
            snprintf(hex, sizeof(hex), "%02x", p.bytes[i]);
            out += std::to_string(i) + ":" + hex + " ";
        }
        out += "| ";
    }
    return out;
}

class Wowlan {
  public:
    /* Programs the triggers when they differ from the last ones sent. */
    void update() {
        int wiphy = wiphyIndex(kPrimaryIface);
        uint8_t mac[6];
        if (wiphy < 0 || !readMac(kPrimaryIface, mac)) return;
        std::vector<WowPattern> patterns = wowPatterns(mac);
        std::string key = std::to_string(wiphy) + " " + describe(patterns);
        if (key == last_) return;

        Nl80211 nl;
        nl_msg* msg = nl.ok() ? nl.message(NL80211_CMD_SET_WOWLAN) : nullptr;
        if (!msg) return;
        nla_put_u32(msg, NL80211_ATTR_WIPHY, wiphy);
        nlattr* triggers = nla_nest_start(msg, NL80211_ATTR_WOWLAN_TRIGGERS);
        nla_put_flag(msg, NL80211_WOWLAN_TRIG_DISCONNECT);
        nla_put_flag(msg, NL80211_WOWLAN_TRIG_MAGIC_PKT);
        nlattr* list = nla_nest_start(msg, NL80211_WOWLAN_TRIG_PKT_PATTERN);
        int n = 1;
        for (const auto& p : patterns) {
            std::vector<uint8_t> mask((p.bytes.size() + 7) / 8, 0);
            for (size_t i = 0; i < p.bytes.size(); i++)
                if (p.used[i]) mask[i / 8] |= 1 << (i % 8);
            nlattr* one = nla_nest_start(msg, n++);
            nla_put(msg, NL80211_PKTPAT_MASK, mask.size(), mask.data());
            nla_put(msg, NL80211_PKTPAT_PATTERN, p.bytes.size(), p.bytes.data());
            nla_put_u32(msg, NL80211_PKTPAT_OFFSET, 0);
            nla_nest_end(msg, one);
        }
        nla_nest_end(msg, list);
        nla_nest_end(msg, triggers);

        int err = nl.send(msg);
        if (err) {
            // A driver without WoWLAN says so once per change, not in a loop.
            ALOGW("SET_WOWLAN on phy%d: %s", wiphy, nl_geterror(err));
        } else {
            ALOGI("WoWLAN on phy%d: disconnect, magic packet, %zu pattern(s)", wiphy,
                  patterns.size());
        }
        last_ = key;
    }

  private:
    std::string last_;
};

/*
 * nl80211's "mlme" multicast group: a connection or a disconnection is when
 * the addresses change. Its messages are only a nudge, their content is not
 * read.
 */
class MlmeEvents {
  public:
    MlmeEvents() {
        sock_ = nl_socket_alloc();
        if (!sock_) return;
        nl_socket_disable_seq_check(sock_);
        if (genl_connect(sock_) < 0) return;
        int group = genl_ctrl_resolve_grp(sock_, "nl80211", "mlme");
        if (group < 0 || nl_socket_add_membership(sock_, group) < 0) return;
        nl_socket_set_nonblocking(sock_);
        fd_ = nl_socket_get_fd(sock_);
    }
    ~MlmeEvents() {
        if (sock_) nl_socket_free(sock_);
    }
    int fd() const { return fd_; }
    void drain() {
        char buf[4096];
        while (recv(fd_, buf, sizeof(buf), MSG_DONTWAIT) > 0) {
        }
    }

  private:
    nl_sock* sock_ = nullptr;
    int fd_ = -1;
};

/* DHCP gives the address some seconds after the connection: look again. */
constexpr int kWowlanRecheckMs = 30000;

}  // namespace

wifi_error wifi_initialize(wifi_handle* handle) {
    if (!handle) return WIFI_ERROR_INVALID_ARGS;
    /*
     * The service may stop and start the HAL again: keep one state for the
     * whole life of the process, as the emulator's library does, and only
     * renew what a stop consumes.
     */
    static wifi_info* sInfo = new wifi_info();
    std::lock_guard<std::mutex> guard(sInfo->lock);
    for (int& fd : sInfo->stop_pipe) {
        if (fd >= 0) close(fd);
        fd = -1;
    }
    if (pipe2(sInfo->stop_pipe, O_CLOEXEC) < 0) {
        ALOGE("pipe2: %s", strerror(errno));
        return WIFI_ERROR_UNKNOWN;
    }
    refreshIfaces(sInfo);
    *handle = reinterpret_cast<wifi_handle>(sInfo);
    ALOGI("initialized, %zu wireless interface(s)", sInfo->handles.size());
    return WIFI_SUCCESS;
}

/*
 * The loop lives until wifi_cleanup() asks it to end, because the service
 * treats a loop that returns on its own as fatal. Nothing is reported to the
 * service from here: the loop only keeps the WoWLAN triggers current.
 */
void wifi_event_loop(wifi_handle handle) {
    wifi_info* info = asInfo(handle);
    if (!info) return;
    Wowlan wowlan;
    MlmeEvents events;
    pollfd pfd[2] = {{info->stop_pipe[0], POLLIN, 0}, {events.fd(), POLLIN, 0}};
    nfds_t count = events.fd() >= 0 ? 2 : 1;
    wowlan.update();
    while (true) {
        int ret = poll(pfd, count, kWowlanRecheckMs);
        if (ret < 0) {
            if (errno == EINTR) continue;
            ALOGE("event loop poll: %s", strerror(errno));
            break;
        }
        if (pfd[0].revents) break;
        if (count > 1 && pfd[1].revents) events.drain();
        wowlan.update();
    }
    ALOGI("event loop terminated");
}

/*
 * Called with the service's global lock held, and the service expects the
 * handler on this same thread: ask the loop to end and report at once.
 */
void wifi_cleanup(wifi_handle handle, wifi_cleaned_up_handler handler) {
    wifi_info* info = asInfo(handle);
    if (!info) return;
    char c = 1;
    if (write(info->stop_pipe[1], &c, 1) != 1) ALOGE("cannot wake the event loop: %s", strerror(errno));
    if (handler) handler(handle);
}

wifi_error wifi_get_ifaces(wifi_handle handle, int* num, wifi_interface_handle** ifaces) {
    wifi_info* info = asInfo(handle);
    if (!info || !num || !ifaces) return WIFI_ERROR_INVALID_ARGS;
    std::lock_guard<std::mutex> guard(info->lock);
    refreshIfaces(info);
    *num = info->handles.size();
    *ifaces = info->handles.data();
    return WIFI_SUCCESS;
}

wifi_error wifi_get_iface_name(wifi_interface_handle iface, char* name, size_t size) {
    if (!iface || !name || !size) return WIFI_ERROR_INVALID_ARGS;
    strlcpy(name, iface->name, size);
    return WIFI_SUCCESS;
}

wifi_error wifi_virtual_interface_create(wifi_handle handle, const char* ifname,
                                         wifi_interface_type type) {
    wifi_info* info = asInfo(handle);
    if (!info || !ifname) return WIFI_ERROR_INVALID_ARGS;

    // An interface that already exists is fine: the service asks for wlan0 too.
    if (ifaceExists(ifname)) return WIFI_SUCCESS;

    uint32_t nltype = toNl80211Type(type);
    int wiphy = wiphyIndex(kPrimaryIface);
    uint8_t mac[6];
    if (nltype == NL80211_IFTYPE_UNSPECIFIED || wiphy < 0 || !readMac(kPrimaryIface, mac)) {
        ALOGE("cannot create %s (type %d, wiphy %d)", ifname, type, wiphy);
        return WIFI_ERROR_NOT_SUPPORTED;
    }

    /*
     * mac80211 refuses to bring up a station and an AP with the same address
     * (ieee80211_check_concurrent_iface): give each new interface a locally
     * administered address of its own, derived from wlan0's. The framework
     * randomizes the AP's address on top of it anyway.
     */
    uint8_t salt = 0;
    for (const char* p = ifname; *p; p++) salt = salt * 31 + *p;
    mac[0] |= 0x02;
    mac[5] ^= salt ? salt : 0x5a;

    Nl80211 nl;
    nl_msg* msg = nl.ok() ? nl.message(NL80211_CMD_NEW_INTERFACE) : nullptr;
    if (!msg) {
        ALOGE("nl80211 not available");
        return WIFI_ERROR_UNKNOWN;
    }
    nla_put_u32(msg, NL80211_ATTR_WIPHY, wiphy);
    nla_put_string(msg, NL80211_ATTR_IFNAME, ifname);
    nla_put_u32(msg, NL80211_ATTR_IFTYPE, nltype);
    nla_put(msg, NL80211_ATTR_MAC, sizeof(mac), mac);
    int err = nl.send(msg);
    if (err) {
        ALOGE("NEW_INTERFACE %s type %u: %s", ifname, nltype, nl_geterror(err));
        return WIFI_ERROR_UNKNOWN;
    }

    std::lock_guard<std::mutex> guard(info->lock);
    info->created.insert(ifname);
    ALOGI("created %s (nl80211 type %u) on phy%d", ifname, nltype, wiphy);
    return WIFI_SUCCESS;
}

wifi_error wifi_virtual_interface_delete(wifi_handle handle, const char* ifname) {
    wifi_info* info = asInfo(handle);
    if (!info || !ifname) return WIFI_ERROR_INVALID_ARGS;
    {
        std::lock_guard<std::mutex> guard(info->lock);
        // Only what this library added: wlan0 is the driver's.
        if (!info->created.count(ifname)) return WIFI_SUCCESS;
    }
    unsigned int index = ifaceExists(ifname) ? if_nametoindex(ifname) : 0;
    if (index) {
        Nl80211 nl;
        nl_msg* msg = nl.ok() ? nl.message(NL80211_CMD_DEL_INTERFACE) : nullptr;
        if (!msg) return WIFI_ERROR_UNKNOWN;
        nla_put_u32(msg, NL80211_ATTR_IFINDEX, index);
        int err = nl.send(msg);
        if (err) {
            ALOGE("DEL_INTERFACE %s: %s", ifname, nl_geterror(err));
            return WIFI_ERROR_UNKNOWN;
        }
    }
    std::lock_guard<std::mutex> guard(info->lock);
    info->created.erase(ifname);
    ALOGI("deleted %s", ifname);
    return WIFI_SUCCESS;
}

wifi_error wifi_set_country_code(wifi_interface_handle iface, const char* code) {
    if (!code || strlen(code) < 2) return WIFI_ERROR_INVALID_ARGS;
    char alpha2[3] = {code[0], code[1], 0};
    Nl80211 nl;
    nl_msg* msg = nl.ok() ? nl.message(NL80211_CMD_REQ_SET_REG) : nullptr;
    if (!msg) return WIFI_ERROR_UNKNOWN;
    nla_put_string(msg, NL80211_ATTR_REG_ALPHA2, alpha2);
    int err = nl.send(msg);
    if (err) {
        ALOGE("REQ_SET_REG %s: %s", alpha2, nl_geterror(err));
        return WIFI_ERROR_UNKNOWN;
    }
    return WIFI_SUCCESS;
}

wifi_error wifi_get_supported_feature_set(wifi_interface_handle iface, feature_set* set) {
    if (!set) return WIFI_ERROR_INVALID_ARGS;
    // What the chip does with the mainline driver, measured (§4, §53).
    *set = WIFI_FEATURE_INFRA | WIFI_FEATURE_INFRA_5G | WIFI_FEATURE_SOFT_AP;
    return WIFI_SUCCESS;
}

wifi_error init_wifi_vendor_hal_func_table(wifi_hal_fn* fn) {
    if (!fn) return WIFI_ERROR_INVALID_ARGS;
    fn->wifi_initialize = wifi_initialize;
    fn->wifi_event_loop = wifi_event_loop;
    fn->wifi_cleanup = wifi_cleanup;
    fn->wifi_get_ifaces = wifi_get_ifaces;
    fn->wifi_get_iface_name = wifi_get_iface_name;
    fn->wifi_virtual_interface_create = wifi_virtual_interface_create;
    fn->wifi_virtual_interface_delete = wifi_virtual_interface_delete;
    fn->wifi_set_country_code = wifi_set_country_code;
    fn->wifi_get_supported_feature_set = wifi_get_supported_feature_set;
    return WIFI_SUCCESS;
}
