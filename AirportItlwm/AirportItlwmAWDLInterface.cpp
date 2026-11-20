//
//  AirportItlwmAWDLInterface.cpp
//  AirportItlwm
//
//  awdl0 for macOS 26 (Tahoe), stage 1: accept and log the AWDL control plane.
//

#include <kern/thread_call.h>
#include "AirportItlwmV2.hpp"
#include "AirportItlwmAWDLInterface.hpp"
#include "AirportItlwmSkywalkInterface.hpp"

#if defined(AIRPORT_WCL) && __IO80211_TARGET != __MAC_15_2

#include <Airport/IOSkywalkPacketBufferPool.h>
#include <Airport/IOSkywalkPacketQueue.h>

extern IOCommandGate *_fCommandGate;

OSDefineMetaClassAndStructors(AirportItlwmAWDLInterface, IO80211AWDLProtocol);
OSDefineMetaClassAndStructors(AirportItlwmAWDLEthernetInterface, AirportItlwmEthernetInterface);

#define AWDL_LOG(fmt, ...) XYLog("AWDL %s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__)

#pragma mark - awdl0 BSD interface

const char *AirportItlwmAWDLEthernetInterface::
getNamePrefix() const
{
    return "awdl";
}

bool AirportItlwmAWDLEthernetInterface::
initIfnetParams(struct ifnet_init_params *params)
{
    // Publish awdl0's own address before IOEthernetInterface reads it as the
    // ifnet uniqueid; en6 already owns the station MAC.
    setProperty(kIOMACAddress, (void *)&fMac, kIOEthernetAddressSize);
    if (!AirportItlwmEthernetInterface::initIfnetParams(params))
        return false;
    params->uniqueid = &fMac;
    params->uniqueid_len = sizeof(fMac);
    return true;
}

IOReturn AirportItlwmAWDLEthernetInterface::
attachToDataLinkLayer(IOOptionBits options, void *parameter)
{
    setProperty("HiddenInterface", kOSBooleanTrue);
    setProperty("HiddenConfiguration", kOSBooleanTrue);
    IOReturn ret = AirportItlwmEthernetInterface::attachToDataLinkLayer(options, parameter);
    if (ret == kIOReturnSuccess && getIfnet()) {
        errno_t err = ifnet_set_lladdr(getIfnet(), &fMac, sizeof(fMac));
        AWDL_LOG("lladdr %s err %d", ether_sprintf(fMac.octet), err);
    }
    return ret;
}

SInt32 AirportItlwmAWDLEthernetInterface::
performCommand(IONetworkController *controller, unsigned long cmd, void *arg0, void *arg1)
{
    // The default SIOCSIFFLAGS path calls controller->enable(this), which would
    // reinitialise the shared radio. The flags are already set on the ifnet.
    if (cmd == SIOCSIFFLAGS) {
        AWDL_LOG("flags 0x%x", ifnet_flags(getIfnet()));
        // Link state is what starts IO80211AWDLPeerManager::configure(). It has
        // to run in the gate but off the work queue thread.
        uintptr_t up = (ifnet_flags(getIfnet()) & IFF_UP) != 0;
        if (_fCommandGate)
            _fCommandGate->runAction(AirportItlwm::setAWDLLinkGated, (void *)up);
        return 0;
    }
    return AirportItlwmEthernetInterface::performCommand(controller, cmd, arg0, arg1);
}

static bool awDataAllowed();
static uint64_t hostUs();

#ifndef ETHER_ALIGN
#define ETHER_ALIGN 2
#endif

// The peer aggregates bulk data into A-MSDUs: after the 802.11 header come
// subframes of DA(6) SA(6) len(2, BE) then LLC/SNAP + AWDL header + packet, each
// padded to 4 bytes except the last. The Intel receive path hands these over whole
// with the QoS A-MSDU bit already cleared, so they only show up as a SNAP mismatch.
// Validate every subframe first so a frame is either split completely or left alone.
static bool awdlAmsduValid(mbuf_t m, size_t hdr, unsigned *nsub)
{
    static const uint8_t kSnap[6] = { 0xaa, 0xaa, 0x03, 0x00, 0x17, 0xf2 };
    size_t total = mbuf_pkthdr_len(m), off = hdr;
    unsigned n = 0;
    while (off + 30 <= total) {
        uint8_t sh[30];
        if (mbuf_copydata(m, off, sizeof(sh), sh))
            return false;
        size_t sublen = ((size_t)sh[12] << 8) | sh[13];
        if (sublen < 16 || sublen > 2304 || off + 14 + sublen > total)
            return false;
        if (memcmp(sh + 14, kSnap, sizeof(kSnap)) || sh[20] != 0x08 || sh[21] != 0x00)
            return false;
        n++;
        off += (14 + sublen + 3) & ~(size_t)3;
    }
    *nsub = n;
    return n > 0 && (off >= total || total - off < 14);
}

int AirportItlwmAWDLEthernetInterface::
inputAWDLData(mbuf_t m, AirportItlwmAWDLInterface *awdl)
{
    if (!awDataAllowed() || !getIfnet()) {
        return 0;
    }
    if (mbuf_pullup(&m, mbuf_pkthdr_len(m) < 64 ? mbuf_pkthdr_len(m) : 64) != 0 || !m) {
        awdl->fDataRxBad++;
        awdl->fDataRxBadPullup++;
        return 1;   // mbuf_pullup freed it
    }
    const uint8_t *p = (const uint8_t *)mbuf_data(m);
    size_t len = mbuf_len(m);
    const struct ieee80211_frame *wh = (const struct ieee80211_frame *)p;
    size_t hdr = sizeof(*wh);
    if (len >= sizeof(*wh) && !(wh->i_addr1[0] & 1))
        awdl->notePeerRx(wh->i_addr1, wh->i_addr2);
    if (ieee80211_has_qos(wh)) {
        uint16_t qos = p[hdr] | (p[hdr + 1] << 8);
        hdr += 2;
        if (qos & IEEE80211_QOS_AMSDU) {
            // Not produced by the peer so far; count and drop.
            awdl->fDataRxAmsdu++;
            mbuf_freem(m);
            return 1;
        }
    }
    static const uint8_t kSnap[6] = { 0xaa, 0xaa, 0x03, 0x00, 0x17, 0xf2 };
    const size_t llc = 8, awdlHdr = 8;
    bool tooShort = len < hdr + llc + awdlHdr;
    if (tooShort || memcmp(p + hdr, kSnap, sizeof(kSnap)) ||
        p[hdr + 6] != 0x08 || p[hdr + 7] != 0x00) {
        unsigned nsub = 0;
        if (!tooShort && awdlAmsduValid(m, hdr, &nsub)) {
            if (memcmp(wh->i_addr2, fMac.octet, 6) == 0) {
                mbuf_freem(m);
                return 1;
            }
            uint8_t dst[6], src[6];
            memcpy(dst, wh->i_addr1, 6);
            memcpy(src, wh->i_addr2, 6);
            size_t off = hdr;
            awdl->fDataRxSplitFrames++;
            for (unsigned i = 0; i < nsub; i++) {
                uint8_t sh[30];
                if (mbuf_copydata(m, off, sizeof(sh), sh))
                    break;
                size_t sublen = ((size_t)sh[12] << 8) | sh[13];
                size_t next = (14 + sublen + 3) & ~(size_t)3;
                size_t ethLen = sublen - 2;   // 14-byte Ethernet header + packet
                if (memcmp(sh, dst, 6) || memcmp(sh + 6, src, 6))
                    awdl->fDataRxSplitAddrDiff++;
                mbuf_t out = NULL;
                unsigned chunks = 1;
                if (mbuf_allocpacket(MBUF_DONTWAIT, ethLen + ETHER_ALIGN, &chunks, &out) || !out) {
                    awdl->fDataRxNoBuf++;
                    off += next;
                    continue;
                }
                uint8_t *d = (uint8_t *)mbuf_data(out) + ETHER_ALIGN;
                memcpy(d, dst, 6);
                memcpy(d + 6, src, 6);
                d[12] = sh[28];
                d[13] = sh[29];
                if (sublen > 16 && mbuf_copydata(m, off + 30, sublen - 16, d + 14)) {
                    mbuf_freem(out);
                    break;
                }
                mbuf_setdata(out, d, ethLen);
                mbuf_pkthdr_setlen(out, ethLen);
                mbuf_pkthdr_setrcvif(out, getIfnet());
                awdl->fDataRx++;
                awdl->fDataRxSplitSub++;
                awdl->fDataRxLastLen = (uint32_t)ethLen;
                awdl->fDataRxLen[ethLen < 128 ? 0 : ethLen < 512 ? 1 : ethLen < 1024 ? 2 : 3]++;
                if (ethLen > awdl->fDataRxMaxLen)
                    awdl->fDataRxMaxLen = (uint32_t)ethLen;
                awdl->fDataRxLastType = (uint16_t)((sh[28] << 8) | sh[29]);
                if (awdl->fDataRx <= 4 || (awdl->fDataRx & 0x1f) == 0)
                    awdl->publishDataState();
                ifnet_stat_increment_in(getIfnet(), 1, (u_int32_t)ethLen, 0);
                inputPacket(out, (UInt32)ethLen, 0, out);
                off += next;
            }
            mbuf_freem(m);
            return 1;
        }
        // Null / QoS-null frames carry no payload; keep them out of "bad".
        if (tooShort && (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_DATA &&
            (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_NODATA)) {
            awdl->fDataRxNull++;
            mbuf_freem(m);
            return 1;
        }
        awdl->fDataRxBad++;
        if (tooShort)
            awdl->fDataRxBadShort++;
        else
            awdl->fDataRxBadSnap++;
        awdl->fDataRxBadLastLen = (uint32_t)mbuf_pkthdr_len(m);
        awdl->fDataRxBadLastFc = (uint16_t)(wh->i_fc[0] | (wh->i_fc[1] << 8));
        awdl->fDataRxBadLastHdr = (uint16_t)hdr;
        uint64_t head = 0;
        for (size_t i = 0; i < 8; i++)
            head = (head << 8) | (hdr + i < len ? p[hdr + i] : 0);
        awdl->fDataRxBadLastHead = head;
        awdl->fDataRxBadLastQos = ieee80211_has_qos(wh) ? (uint16_t)(p[sizeof(*wh)] | (p[sizeof(*wh) + 1] << 8)) : 0;
        for (size_t w = 0; w < 4; w++) {
            uint64_t r = 0;
            for (size_t i = 0; i < 8; i++)
                r = (r << 8) | (hdr + w * 8 + i < len ? p[hdr + w * 8 + i] : 0);
            awdl->fDataRxBadLastRaw[w] = r;
        }
        if (mbuf_pkthdr_len(m) > 2304)
            awdl->fDataRxBadBig++;
        if ((awdl->fDataRxBad & 0xf) == 1)
            awdl->publishDataState();
        mbuf_freem(m);
        return 1;
    }
    const uint8_t *ad = p + hdr + llc;
    uint8_t ethType[2] = { ad[6], ad[7] };
    uint8_t dst[6], src[6];
    memcpy(dst, wh->i_addr1, 6);
    memcpy(src, wh->i_addr2, 6);
    if (memcmp(src, fMac.octet, 6) == 0) {
        mbuf_freem(m);
        return 1;
    }

    // Keep 14 bytes of the stripped headers for the Ethernet header.
    size_t strip = hdr + llc + awdlHdr - 14;
    mbuf_adj(m, (int)strip);
    uint8_t *eh = (uint8_t *)mbuf_data(m);
    memcpy(eh, dst, 6);
    memcpy(eh + 6, src, 6);
    eh[12] = ethType[0];
    eh[13] = ethType[1];
    mbuf_pkthdr_setrcvif(m, getIfnet());
    awdl->fDataRx++;
    awdl->fDataRxLastLen = (uint32_t)mbuf_pkthdr_len(m);
    {
        uint32_t l = awdl->fDataRxLastLen;
        awdl->fDataRxLen[l < 128 ? 0 : l < 512 ? 1 : l < 1024 ? 2 : 3]++;
        if (l > awdl->fDataRxMaxLen)
            awdl->fDataRxMaxLen = l;
    }
    awdl->fDataRxLastType = (uint16_t)((ethType[0] << 8) | ethType[1]);
    if (awdl->fDataRx <= 4 || (awdl->fDataRx & 0x1f) == 0)
        awdl->publishDataState();
    // IONetworkInterface does not account these on awdl0 (netstat showed 0 in,
    // bytes included), so count them on the ifnet here.
    ifnet_stat_increment_in(getIfnet(), 1, (u_int32_t)mbuf_pkthdr_len(m), 0);
    inputPacket(m, (UInt32)mbuf_pkthdr_len(m), 0, m);
    return 1;
}

// Runs on the network output thread: move the frame onto the work loop.
UInt32 AirportItlwmAWDLEthernetInterface::
outputAWDL(mbuf_t m, void *param)
{
    if (!awDataAllowed() || !fOwner || !_fCommandGate)
        return dropOutput(m, param);
    _fCommandGate->runAction([](OSObject *, void *owner, void *pkt, void *, void *) -> IOReturn {
        static_cast<AirportItlwmAWDLInterface *>(owner)->enqueueData((mbuf_t)pkt);
        return kIOReturnSuccess;
    }, fOwner, m);
    return kIOReturnOutputSuccess;
}

UInt32 AirportItlwmAWDLEthernetInterface::
dropOutput(mbuf_t m, void *param)
{
    fDropped++;
    mbuf_freem(m);
    return kIOReturnOutputDropped;
}

// Stage 2c is opt-in while it is being brought up: a test machine boot-looped
// with it always on. Without -itlawdlaw awdl0 only receives action frames.
// Stage 2b, also opt-in: visit the social channel when the station is not on it.
static bool awListenAllowed()
{
    static int allowed = -1;
    if (allowed < 0) {
        int v;
        allowed = PE_parse_boot_argn("-itlawdllisten", &v, sizeof(v)) ? 1 : 0;
    }
    return allowed == 1;
}

// While the station is associated, an AWDL listen on another channel takes the
// radio off the infra channel for up to a dwell (160 TU). Back to back, that
// starves the infra link: on an iPhone hotspot on channel 36 with AWDL on 149,
// 60-90% loss, 0.5-10 s RTT and CCMP errors, but 0% with awdl0 down. So hold
// those listens, except during active peer traffic (AirDrop) and, with
// itlawdlinfranth=<n>, one in every n to keep some peer discovery.
static const uint16_t kListenParkTU = 16;

// Listens on the infra channel itself (see itlwm_awdl_home_listen in ItlIwx.cpp):
// each at most kHomeListenMaxTU, then at least as long again on the station
// (three times as long outside peer traffic).
extern int itlwm_awdl_home_listen;
static const uint32_t kHomeListenMaxTU = 64;

static bool awHomeListenAllowed(uint32_t peerTraffic, int wantData)
{
    int mode = itlwm_awdl_home_listen;
    if (mode <= 0)
        return false;
    if (peerTraffic > 0)
        return true;
    return mode >= 2 && wantData;
}

extern int itlwm_awdl_infra_nth;
extern int itlwm_awdl_dpstats;

/*
 * 1 in every n off-channel listens passes the infra hold; 0 holds them all.
 * While AWDL wants data (AirDrop etc.), n is debug.itlwm.awdl_infra_nth:
 * those listens are the only time the Mac is on a peer's channel (an iPhone
 * on 149 while the Mac's infra is on 36), so with all of them held the Mac
 * cannot send to it. Otherwise n is the itlawdlinfranth=<n> boot-arg.
 */
static unsigned awInfraListenNth(bool wantData)
{
    static int nth = -1;
    if (nth < 0) {
        int v = 0;
        nth = PE_parse_boot_argn("itlawdlinfranth", &v, sizeof(v)) && v > 0 ? v : 0;
    }
    if (wantData && itlwm_awdl_infra_nth >= 0)
        return (unsigned)itlwm_awdl_infra_nth;
    return (unsigned)nth;
}

// Diagnostic: itlawdllistench=<n> sends every listen dwell to channel n
// instead of the AW slot channel, e.g. the channel the phone sits on.
static unsigned awListenChannelOverride()
{
    static int chan = -1;
    if (chan < 0) {
        int v = 0;
        chan = PE_parse_boot_argn("itlawdllistench", &v, sizeof(v)) && v > 0 && v <= 196 ? v : 0;
    }
    return (unsigned)chan;
}

// -itlawdlnopeerlisten turns off listening on the tracked peer's channel in
// slots where our own sequence stays home.
static bool awPeerListenAllowed()
{
    static int allowed = -1;
    if (allowed < 0) {
        int v = 0;
        allowed = PE_parse_boot_argn("-itlawdlnopeerlisten", &v, sizeof(v)) ? 0 : 1;
    }
    return allowed == 1;
}

// Stage 4: AWDL data frames to and from awdl0's BSD interface.
static bool awDataAllowed()
{
    static int allowed = -1;
    if (allowed < 0) {
        int v;
        allowed = PE_parse_boot_argn("-itlawdldata", &v, sizeof(v)) ? 1 : 0;
    }
    return allowed == 1;
}

// Stage 3: transmit AWDL frames during the listen dwells.
static bool awTxAllowed()
{
    static int allowed = -1;
    if (allowed < 0) {
        int v;
        allowed = PE_parse_boot_argn("-itlawdltx", &v, sizeof(v)) ? 1 : 0;
    }
    return allowed == 1;
}

static bool awTimelineAllowed()
{
    static int allowed = -1;
    if (allowed < 0) {
        int v;
        allowed = PE_parse_boot_argn("-itlawdlaw", &v, sizeof(v)) ? 1 : 0;
    }
    return allowed == 1;
}

#pragma mark - receive path, mirrors AppleBCMWLANProximityInterface::handleActionFrame_rx

// IO80211BufferPool / IO80211Buffer vtable slots (Tahoe KC, address point relative).
enum : size_t {
    kPoolGetBuffer   = 0x118,
    kBufReturn       = 0x118,
    kBufSetLength    = 0x138,
    kBufGetCapacity  = 0x140,
    kBufGetBytes     = 0x158,
};
template <typename R, typename... A>
static inline R vcall(const void *obj, size_t slot, A... args)
{
    auto fn = *reinterpret_cast<R (* const *)(const void *, A...)>(*reinterpret_cast<const uint8_t * const *>(obj) + slot);
    return fn(obj, args...);
}

// Metadata AppleBCMWLANProximityInterface passes to bpfTapInput (0x48 bytes).
struct AWDLRxMeta {
    uint8_t  addr[6];          // 0x00 transmitter
    uint8_t  pad0[6];
    apple80211_channel chan;   // 0x0c
    uint8_t  pad1[0x28 - 0x18];
    uint32_t rate;             // 0x28
    int32_t  rssi;             // 0x2c, peer manager drops new peers below a threshold
    uint32_t tsf;              // 0x30 receive timestamp (us)
    uint8_t  pad2[0x48 - 0x34];
};
static_assert(sizeof(AWDLRxMeta) == 0x48, "AWDL rx meta size");
static_assert(offsetof(AWDLRxMeta, chan) == 0x0c, "AWDL rx meta chan");
static_assert(offsetof(AWDLRxMeta, rssi) == 0x2c, "AWDL rx meta rssi");

// Category 127, OUI 00:17:f2, type 8 (AWDL), version 0: checked by
// IO80211AWDLPeerManager::actionFrameHasCorrectOUI over the first 16 bytes.
static const uint8_t kAWDLFixedHeaderLen = 16;

void AirportItlwmAWDLInterface::
noteRxGap(const uint8_t *src, uint64_t nowUs)
{
    RxGap *e = nullptr, *oldest = &fRxGap[0];
    for (unsigned i = 0; i < kRxGapSlots; i++) {
        if (!memcmp(fRxGap[i].mac, src, 6) && fRxGap[i].lastUs) {
            e = &fRxGap[i];
            break;
        }
        if (fRxGap[i].lastUs < oldest->lastUs)
            oldest = &fRxGap[i];
    }
    if (e) {
        uint64_t gap = nowUs - e->lastUs;
        if (gap > e->maxGapUs)
            e->maxGapUs = gap;
        if (gap > 6000000)
            e->over6s++;
    } else {
        e = oldest;
        memset(e, 0, sizeof(*e));
        memcpy(e->mac, src, 6);
    }
    e->lastUs = nowUs;
    e->count++;
}

int AirportItlwmAWDLInterface::
inputActionFrame(const uint8_t *frame, size_t len, const ieee80211_rxinfo *rxi)
{
    const size_t hdr = sizeof(struct ieee80211_frame);
    if (len < hdr + kAWDLFixedHeaderLen)
        return 0;
    // The firmware path delivers the frame body, starting at the category byte.
    const uint8_t *af = frame + hdr;
    if (af[0] != 127 || af[1] != 0x00 || af[2] != 0x17 || af[3] != 0xf2 || af[4] != 0x08)
        return 0;

    fRxAF++;
    noteRxGap(((const struct ieee80211_frame *)frame)->i_addr2, hostUs());
    uint64_t *chanCount = rxi->rxi_chan == 6 ? &fRxAFChan6 :
        rxi->rxi_chan == 44 ? &fRxAFChan44 :
        rxi->rxi_chan == 149 ? &fRxAFChan149 : &fRxAFChanOther;
    if (rxi->rxi_chan == 161)
        fRxAFChan161++;
    bool firstOnChan = (*chanCount)++ == 0;
    // A frame on a social channel is rare and is the result 2b is looking for.
    bool socialChan = chanCount != &fRxAFChanOther;
    // XYLog goes to kprintf, so publish progress where ioreg can see it.
    if (firstOnChan || socialChan || fRxAF <= 8 || (fRxAF & 0xf) == 0) {
        publishListenState();
        setProperty("AWDLRxFrames", fRxAF, 64);
        setProperty("AWDLRxDropped", fRxAFDropped, 64);
        setProperty("AWDLRxLastChannel", rxi->rxi_chan, 32);
    }

    AWDLBufferPoolABI *pool = IO80211VirtualInterface_getMgmtFramePool(this);
    AWDLBufferABI *buf = pool ? vcall<AWDLBufferABI *>(pool, kPoolGetBuffer) : nullptr;
    if (!buf) {
        fRxAFDropped++;
        return 1;
    }
    size_t payload = len - hdr;
    if (payload > vcall<size_t>(buf, kBufGetCapacity)) {
        vcall<void>(buf, kBufReturn);
        fRxAFDropped++;
        return 1;
    }
    // Copied verbatim: bytes 8..15 are phy_tx_time / target_tx_time, which the
    // peer manager uses for time sync.
    memcpy(vcall<uint8_t *>(buf, kBufGetBytes), af, payload);
    vcall<void>(buf, kBufSetLength, payload);

    const struct ieee80211_frame *wh = (const struct ieee80211_frame *)frame;
    AWDLRxMeta meta = {};
    memcpy(meta.addr, wh->i_addr2, sizeof(meta.addr));
    meta.chan.version = APPLE80211_VERSION;
    meta.chan.channel = rxi->rxi_chan;
    meta.chan.flags = APPLE80211_C_FLAG_20MHZ |
        (rxi->rxi_chan <= 14 ? APPLE80211_C_FLAG_2GHZ : APPLE80211_C_FLAG_5GHZ);
    // rxi_rssi is normalized to 0..67 (dBm - IWX_MIN_DBM); the peer manager
    // compares dBm against its threshold.
    meta.rssi = rxi->rxi_rssi - 100;
    // Same clock as the AW start times we report: the peer manager subtracts
    // one from the other (IO80211AWDLPeer::calculatePeerTimeDiff).
    uint64_t rxUs = awTimelineAllowed() ? hostRxUs(rxi) : 0;
    meta.tsf = awTimelineAllowed() ? (uint32_t)rxUs : rxi->rxi_tstamp;

    if (fRxAF <= 8 || (fRxAF & 0xff) == 0) {
        // Fixed header bytes 5..7 (version, subtype, reserved) of real frames.
        char hv[8];
        snprintf(hv, sizeof(hv), "%02x%02x%02x", af[5], af[6], af[7]);
        setProperty("AWDLRxHdrVer", hv);
    }
    trackPeer(af, payload, wh->i_addr2, rxUs, rxi->rxi_chan);
    syncFromFrame(af, payload, wh->i_addr2, rxUs);

    // Consumes buf (peer manager input + bpf, then returnBuffer).
    IO80211VirtualInterface_bpfTapInput(this, buf, 12, &meta, sizeof(meta));
    return 1;
}

#pragma mark - availability window timeline (host side of the firmware AW engine)

// Layout as read by AppleBCMWLANProximityInterface::buildAWDLChanSequence and
// IO80211AWDLPeerManager::setAwdlChannelSequence (the header's struct is stale):
// +5 entry count, +6 encoding, +7 step count (presence mode - 1),
// +12 entries of apple80211_channel (12 bytes each). 400 bytes in total.
enum : size_t {
    kChanSeqCount = 5,
    kChanSeqStep = 7,
    kChanSeqEntries = 12,
    kChanSeqMax = 32,
};

// Payloads of the AW messages, as built by AppleBCMWLANProximityInterface::
// handleAvailabilityWindowStart/End and read by IO80211AWDLPeerManager::
// reportDataPathEvents / monitorTimeline / logAWStart / logAWEnd. Times are
// big-endian microseconds; the peer manager subtracts the AW start time from
// the rx timestamp in the action frame meta, so both use hostUs().
// Posted through IO80211VirtualInterface::postMessage, which hands them to
// reportDataPathEvents -> IO80211AWDLPeerManager::reportDataPathEvents.
enum : unsigned int {
    kAWDLMsgAWStart = 41,
    kAWDLMsgAWExtension = 42,
    kAWDLMsgAWEnd = 43,
};

struct AWDLAWStartEvent {
    uint32_t seq;              // 0x00 AW sequence number (AWSeqNum)
    uint32_t role;             // 0x04 1 master, 2 slave, 3 non-election master
    uint8_t  master[6];        // 0x08 master we are synced to
    uint8_t  topMaster[6];     // 0x0e root of the sync tree
    uint32_t startUsBE;        // 0x14 AW start time
    apple80211_channel chan;   // 0x18 channel of this slot
} __attribute__((packed));
static_assert(sizeof(AWDLAWStartEvent) == 0x24, "AW start event size");

struct AWDLAWEndEvent {
    uint32_t extCount;         // 0x00
    uint32_t endUsBE;          // 0x04 AW end time
    uint8_t  pad[0x28 - 8];    // 0x08 extension and CCA statistics
} __attribute__((packed));
static_assert(sizeof(AWDLAWEndEvent) == 0x28, "AW end event size");

static const uint8_t kAWDLSyncParamsTLV = 4;
static const uint8_t kAWDLChanSeqTLV = 18;
static const int64_t kTimerEarlyUs = 2000;     // timer may fire this much early

static uint64_t hostUs()
{
    uint64_t ns;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    return ns / 1000;
}

uint64_t AirportItlwmAWDLInterface::
hostRxUs(const ieee80211_rxinfo *rxi)
{
    uint64_t now = hostUs();
    if (!rxi->rxi_tstamp)
        return now;
    // The NIC TSF and the host clock drift slowly and the TSF can jump when it
    // follows the AP. Track the smallest host-minus-TSF offset (least rx
    // latency), let it creep up slowly for drift and reset on large jumps.
    uint32_t off = (uint32_t)now - rxi->rxi_tstamp;
    int32_t diff = (int32_t)(off - fTsfOffset);
    if (!fTsfValid || diff < 0 || diff > 20000) {
        fTsfOffset = off;
        fTsfValid = true;
    } else {
        fTsfOffset += diff < 20 ? diff : 20;
    }
    uint32_t latency = off - fTsfOffset;
    return latency < 1000000 ? now - latency : now;
}

uint16_t AirportItlwmAWDLInterface::
presenceMode() const
{
    // The peer manager steps AWSeqNum by its presence mode per channel slot and
    // tells us that through the channel sequence step count.
    if (fChanSeqValid && fChanSeq[kChanSeqStep] < 16)
        return fChanSeq[kChanSeqStep] + 1;
    if (fPresenceMode >= 1 && fPresenceMode <= 16)
        return fPresenceMode;
    return 4;
}

uint64_t AirportItlwmAWDLInterface::
slotUs() const
{
    uint32_t period = fSyncParams.availability_window_period;
    if (period < 4 || period > 64)
        period = 16;
    return (uint64_t)period * presenceMode() * 1024;
}

bool AirportItlwmAWDLInterface::
isSyncMaster() const
{
    static const ether_addr zero = {};
    return !memcmp(&fSyncMaster, &zero, sizeof(zero)) || !memcmp(&fSyncMaster, &fMac, sizeof(fMac));
}

void AirportItlwmAWDLInterface::
startAWTimeline()
{
    if (fAWRunning || !fAWTimer || !awTimelineAllowed())
        return;
    uint64_t now = hostUs();
    uint16_t pm = presenceMode();
    // Free run until a master is programmed; any non-zero start works, the
    // peer manager only flags a sequence number stuck at 0.
    fRefUs = now;
    fRefSeq = (uint16_t)(((now / 1000) & 0x3fff) / pm * pm + pm);
    fLastValid = false;
    fInAW = false;
    fAWRunning = true;
    publishAWDLAddress();
    if (fController && fController->fHalService) {
        if (ieee80211com *ic = fController->fHalService->get80211Controller()) {
            ic->ic_awdl_roc_arg = this;
            ic->ic_awdl_roc_notify = &AirportItlwmAWDLInterface::rocStartedThunk;
            ic->ic_awdl_want_data = awDataAllowed() ? 1 : 0;
            notifyHalSync();
        }
    }
    publishSyncState();
    AWDL_LOG("seq %u slot %llu us", fRefSeq, slotUs());
    scheduleNextAW(now);
    if (awDataAllowed() && awListenAllowed())
        scheduleAlignedListen(now);
    publishSyncState();
}

void AirportItlwmAWDLInterface::
stopAWTimeline()
{
    if (!fAWRunning)
        return;
    fAWRunning = false;
    if (fAWTimer)
        fAWTimer->cancelTimeout();
    fListenAlignedActive = false;
    fPeerValid = false;
    fPeerChanValid = false;
    fOOBReqPending = false;
    fLastValid = false;
    __atomic_store_n(&fRocNotifyChan, 0, __ATOMIC_RELEASE);
    if (fController && fController->fHalService) {
        if (ieee80211com *ic = fController->fHalService->get80211Controller()) {
            if (ic->ic_awdl_roc_arg == this) {
                ic->ic_awdl_roc_notify = nullptr;
                ic->ic_awdl_roc_arg = nullptr;
            }
            ic->ic_awdl_want_data = 0;
            notifyHalSync();
        }
    }
    if (fListenTimer)
        fListenTimer->cancelTimeout();
    fTxPhase = 0;
    if (fTxTimer)
        fTxTimer->cancelTimeout();
    // Nothing can leave without the timeline; do not send stale frames later.
    while (fDataQCount) {
        mbuf_freem(fDataQ[fDataQHead]);
        fDataQ[fDataQHead] = nullptr;
        fDataQHead = (fDataQHead + 1) % kDataQueueLen;
        fDataQCount--;
    }
    if (fInAW) {
        AWDLAWEndEvent end = {};
        end.endUsBE = OSSwapHostToBigInt32((uint32_t)hostUs());
        postMessage(kAWDLMsgAWEnd, &end, sizeof(end), true);
    }
    fInAW = false;
    AWDL_LOG("starts %llu syncs %llu", fAWStarts, fSyncUpdates);
    publishSyncState();
}

void AirportItlwmAWDLInterface::
teardownAW()
{
    fAWRunning = false;
    fListenAlignedActive = false;
    __atomic_store_n(&fRocNotifyChan, 0, __ATOMIC_RELEASE);
    if (fController && fController->fHalService) {
        if (ieee80211com *ic = fController->fHalService->get80211Controller()) {
            if (ic->ic_awdl_roc_arg == this) {
                ic->ic_awdl_roc_notify = nullptr;
                ic->ic_awdl_roc_arg = nullptr;
            }
        }
    }
    if (fListenTimer) {
        fListenTimer->cancelTimeout();
        if (fListenTimer->getWorkLoop())
            fListenTimer->getWorkLoop()->removeEventSource(fListenTimer);
        OSSafeReleaseNULL(fListenTimer);
    }
    fTxPhase = 0;
    while (fDataQCount) {
        mbuf_freem(fDataQ[fDataQHead]);
        fDataQ[fDataQHead] = nullptr;
        fDataQHead = (fDataQHead + 1) % kDataQueueLen;
        fDataQCount--;
    }
    if (fTxTimer) {
        fTxTimer->cancelTimeout();
        if (fTxTimer->getWorkLoop())
            fTxTimer->getWorkLoop()->removeEventSource(fTxTimer);
        OSSafeReleaseNULL(fTxTimer);
    }
    if (fPeerCacheTimer) {
        fPeerCacheTimer->cancelTimeout();
        if (fPeerCacheTimer->getWorkLoop())
            fPeerCacheTimer->getWorkLoop()->removeEventSource(fPeerCacheTimer);
        OSSafeReleaseNULL(fPeerCacheTimer);
    }
    if (!fAWTimer)
        return;
    fAWTimer->cancelTimeout();
    if (fAWTimer->getWorkLoop())
        fAWTimer->getWorkLoop()->removeEventSource(fAWTimer);
    OSSafeReleaseNULL(fAWTimer);
}

void AirportItlwmAWDLInterface::
scheduleNextAW(uint64_t nowUs)
{
    if (!fAWRunning || !fAWTimer)
        return;
    int64_t slot = (int64_t)slotUs();
    int64_t since = (int64_t)nowUs - fRefUs + kTimerEarlyUs;
    int64_t k = since >= 0 ? since / slot + 1 : -((-since) / slot);
    int64_t when = fRefUs + k * slot;
    uint64_t abs;
    nanoseconds_to_absolutetime((uint64_t)when * 1000, &abs);
    fAWTimer->cancelTimeout();
    fAWTimer->wakeAtTime(abs);
}

void AirportItlwmAWDLInterface::
awTimerFired(OSObject *owner, IOTimerEventSource *)
{
    AirportItlwmAWDLInterface *that = OSDynamicCast(AirportItlwmAWDLInterface, owner);
    if (!that || !that->fAWRunning)
        return;
    that->postAWEvents();
    that->scheduleNextAW(hostUs());
}

void AirportItlwmAWDLInterface::
postAWEvents()
{
    uint64_t now = hostUs();
    int64_t slot = (int64_t)slotUs();
    uint16_t pm = presenceMode();
    int64_t since = (int64_t)now - fRefUs + kTimerEarlyUs;
    int64_t k = since >= 0 ? since / slot : -((-since + slot - 1) / slot);
    int64_t start = fRefUs + k * slot;
    uint16_t seq = (uint16_t)(fRefSeq + k * pm);
    // A re-sync can map this boundary to the slot we already reported.
    if (fLastValid && seq == fLastSeq)
        return;

    // The peer manager expects end then start (it clears its "in AW" and
    // back-to-back flags on the end event).
    if (fInAW) {
        AWDLAWEndEvent end = {};
        end.endUsBE = OSSwapHostToBigInt32((uint32_t)start);
        postMessage(kAWDLMsgAWEnd, &end, sizeof(end), true);
        fInAW = false;
    }

    // Channel 0 in the sequence is an idle slot. The firmware sends no AW start
    // there: sanityCheckAWStart counts the skipped slots and only complains
    // about skipped slots that have a channel.
    unsigned count = fChanSeqValid ? fChanSeq[kChanSeqCount] : 0;
    const apple80211_channel *slotChan = count ?
        (const apple80211_channel *)(fChanSeq + kChanSeqEntries) + (seq / pm) % count : nullptr;
    if (slotChan && !slotChan->channel) {
        fLastValid = true;
        fLastSeq = seq;
        notePairSlot(0, (uint64_t)start);
        unsigned home = currentHomeChannel();
        if (home && canTxOnChannel((uint8_t)home) &&
            canFlushDataOnChannel((uint8_t)home, start + slotUs() / 2)) {
            trySendPendingOOB();
            if (fDataQCount)
                flushData((uint8_t)home);
        }
        return;
    }

    AWDLAWStartEvent ev = {};
    ev.seq = seq;
    ev.role = isSyncMaster() ? 1 : 2;
    const ether_addr &master = isSyncMaster() ? fMac : fSyncMaster;
    memcpy(ev.master, &master, sizeof(ev.master));
    memcpy(ev.topMaster, &master, sizeof(ev.topMaster));
    ev.startUsBE = OSSwapHostToBigInt32((uint32_t)start);
    ev.chan.version = APPLE80211_VERSION;
    if (slotChan) {
        ev.chan.channel = slotChan->channel;
        ev.chan.flags = slotChan->flags;
    }
    if (!ev.chan.channel) {
        ev.chan.channel = 6;
        ev.chan.flags = APPLE80211_C_FLAG_2GHZ | APPLE80211_C_FLAG_20MHZ;
    }
    notePairSlot((uint8_t)ev.chan.channel, (uint64_t)start);
    postMessage(kAWDLMsgAWStart, &ev, sizeof(ev), true);
    maybeListen(ev.chan.channel, seq);
    if (ev.chan.channel && ev.chan.channel == currentHomeChannel() &&
        canTxOnChannel((uint8_t)ev.chan.channel))
        scheduleTx((uint8_t)ev.chan.channel, start);

    fInAW = true;
    fLastValid = true;
    fLastSeq = seq;
    fLastStartUs = start;
    fAWStarts++;
    if (fAWStarts <= 4 || (fAWStarts & 0x3f) == 0)
        publishSyncState();
}

// Follow the master's timeline from its Synchronization Parameters TLV, as
// OWL does (seemoo-lab/owl src/sync.c, rx.c): the next extended AW (presence
// mode AWs) starts tx_down_counter TU after the frame, and its sequence number
// is next_aw_seq rounded down to the presence mode, plus one presence mode
// (same arithmetic as IO80211AWDLPeer::calculatePeerTimeDiff).
void AirportItlwmAWDLInterface::
syncFromFrame(const uint8_t *af, size_t len, const uint8_t *src, uint64_t rxUs)
{
    if (!fAWRunning || isSyncMaster() || memcmp(src, &fSyncMaster, sizeof(fSyncMaster)))
        return;
    size_t off = kAWDLFixedHeaderLen;
    while (off + 3 <= len) {
        uint8_t type = af[off];
        uint16_t tlen = af[off + 1] | (af[off + 2] << 8);
        const uint8_t *v = af + off + 3;
        if (off + 3 + tlen > len)
            return;
        if (type == kAWDLSyncParamsTLV) {
            if (tlen < 31)
                return;
            uint16_t down = v[1] | (v[2] << 8);
            uint16_t tlvPm = v[27];
            uint16_t nextSeq = v[29] | (v[30] << 8);
            uint16_t pm = presenceMode();
            if (tlvPm && tlvPm != pm)
                setProperty("AWDLMasterPresenceMode", tlvPm, 32);
            // Fixed header bytes 8..11 / 12..15 are phy_tx / target_tx (us,
            // little-endian). Their difference may be the delay between building
            // the frame (tx_down_counter) and sending it [inferred]; recorded
            // for now, not applied.
            uint32_t phyTx = af[8] | (af[9] << 8) | (af[10] << 16) | ((uint32_t)af[11] << 24);
            uint32_t targetTx = af[12] | (af[13] << 8) | (af[14] << 16) | ((uint32_t)af[15] << 24);
            int64_t txDelay = (int32_t)(phyTx - targetTx);
            if (txDelay < 0 || txDelay > 100000)
                txDelay = 0;
            int64_t boundary = (int64_t)rxUs + (int64_t)down * 1024 - txDelay;
            uint16_t seq = (uint16_t)(nextSeq / pm * pm + pm);

            // Error of our timeline at that boundary.
            int64_t slot = (int64_t)slotUs();
            int64_t d = boundary - fRefUs;
            int64_t k = d >= 0 ? (d + slot / 2) / slot : -((-d + slot / 2) / slot);
            int64_t err = d - k * slot;
            int16_t seqErr = (int16_t)(seq - (uint16_t)(fRefSeq + k * pm));
            fLastSyncErrUs = err;
            setProperty("AWDLSyncTxDelayUs", (uint64_t)txDelay, 32);

            // The peer manager tolerates +/-10 ms between AW starts. Ignore a
            // single large outlier (rx latency spike); re-anchor only when the
            // error persists, the sequence differs or we are not locked yet.
            const int64_t kOutlierUs = 6000;
            bool large = err > kOutlierUs || err < -kOutlierUs;
            if (fSyncUpdates && !seqErr && large && ++fSyncOutliers < 3) {
                setProperty("AWDLSyncOutliers", fSyncOutliersTotal + 1, 64);
                fSyncOutliersTotal++;
                return;
            }
            fSyncOutliers = 0;

            fRefUs = boundary;
            fRefSeq = seq;
            fSyncUpdates++;
            if (seqErr) {
                fLastValid = false;
                publishSyncState();
            }
            if (fSyncUpdates <= 4 || (fSyncUpdates & 0x3f) == 0 || seqErr) {
                setProperty("AWDLSyncSeqErr", (uint64_t)(int64_t)seqErr, 32);
                publishSyncState();
            }
            scheduleNextAW(hostUs());
            return;
        }
        off += 3 + tlen;
    }
}

// 2b: the station stays on the AP channel. When that is not the channel of
// this AW slot, ask the firmware for a short off-channel visit (iwx listen
// scan) so AWDL frames sent in the AW can be heard. Only every
// kListenEveryNthSlot-th such slot, to keep the station link usable.
// Defaults; both can be changed at run time (setProperties). A new listen is
// refused while one is pending or running, which bounds the duty cycle.
enum : unsigned {
    kListenDwellTU = 160,
    kListenEveryNthSlot = 1,
    kListenDwellMinTU = 16,
    kListenDwellMaxTU = 1000,
};

IOReturn AirportItlwmAWDLInterface::
setProperties(OSObject *properties)
{
    OSDictionary *d = OSDynamicCast(OSDictionary, properties);
    // Only while the experimental listen is on, so a stock -itlawdl boot
    // exposes no new writable state.
    if (!d || !awListenAllowed())
        return IO80211AWDLProtocol::setProperties(properties);
    bool handled = false;
    if (OSNumber *n = OSDynamicCast(OSNumber, d->getObject("AWDLListenDwellTU"))) {
        uint32_t v = n->unsigned32BitValue();
        if (v < kListenDwellMinTU || v > kListenDwellMaxTU)
            return kIOReturnBadArgument;
        fListenDwellTU = v;
        handled = true;
    }
    if (OSNumber *n = OSDynamicCast(OSNumber, d->getObject("AWDLListenEveryNth"))) {
        uint32_t v = n->unsigned32BitValue();
        if (v < 1 || v > 64)
            return kIOReturnBadArgument;
        fListenEveryNth = v;
        handled = true;
    }
    if (OSNumber *n = OSDynamicCast(OSNumber, d->getObject("AWDLListenAlign"))) {
        // Called on a user thread; the timers and peer state belong to the work loop.
        uintptr_t align = n->unsigned32BitValue() != 0;
        handled = true;
        if (_fCommandGate)
            _fCommandGate->runAction([](OSObject *, void *self, void *value, void *, void *) -> IOReturn {
                auto that = static_cast<AirportItlwmAWDLInterface *>(self);
                that->fListenAlign = (uint32_t)(uintptr_t)value;
                if (!that->fListenAlign) {
                    that->fListenAlignedActive = false;
                    if (that->fListenTimer)
                        that->fListenTimer->cancelTimeout();
                } else if (!that->fListenAlignedActive) {
                    that->scheduleAlignedListen(hostUs());
                }
                return kIOReturnSuccess;
            }, this, (void *)align);
    }
    if (!handled)
        return IO80211AWDLProtocol::setProperties(properties);
    setProperty("AWDLListenDwellTU", fListenDwellTU, 32);
    setProperty("AWDLListenEveryNth", fListenEveryNth, 32);
    setProperty("AWDLListenAlign", fListenAlign, 32);
    // Counters are published in batches; this gives callers an exact snapshot.
    publishListenState();
    AWDL_LOG("listen dwell %u TU every %u align %u", fListenDwellTU, fListenEveryNth, fListenAlign);
    return kIOReturnSuccess;
}

void AirportItlwmAWDLInterface::
publishListenState()
{
    {
        // "mac frames maxGapMs over6s ageMs" for each transmitter heard recently.
        char g[512];
        size_t o = 0;
        uint64_t now = hostUs();
        for (unsigned i = 0; i < kRxGapSlots && o < sizeof(g) - 64; i++) {
            const RxGap &e = fRxGap[i];
            if (!e.lastUs || now - e.lastUs > 120000000)
                continue;
            o += snprintf(g + o, sizeof(g) - o, "%02x%02x %u %llu %u %llu; ", e.mac[4], e.mac[5], e.count,
                          e.maxGapUs / 1000, e.over6s, (now - e.lastUs) / 1000);
        }
        g[o] = 0;
        setProperty("AWDLRxGap", g);
    }

    setProperty("AWDLRxChan6", fRxAFChan6, 64);
    setProperty("AWDLRxChan44", fRxAFChan44, 64);
    setProperty("AWDLRxChan149", fRxAFChan149, 64);
    setProperty("AWDLRxChanOther", fRxAFChanOther, 64);
    setProperty("AWDLListenStarted", fListenStarted, 64);
    setProperty("AWDLListenBusy", fListenBusy, 64);
    setProperty("AWDLListenAligned", fListenAligned, 64);
    setProperty("AWDLListenNoSlot", fListenNoSlot, 64);
    setProperty("AWDLListenAlignedActive", fListenAlignedActive);
    setProperty("AWDLPeerUpdates", fPeerUpdates, 64);
    setProperty("AWDLPeerPredHit", fPeerPredHit, 64);
    setProperty("AWDLPeerPredMiss", fPeerPredMiss, 64);
    setProperty("AWDLPeerErrUs", (uint64_t)(fPeerLastErrUs < 0 ? -fPeerLastErrUs : fPeerLastErrUs), 32);
    if (fPeerChanValid) {
        char seq[16 * 4 + 1] = {};
        for (unsigned i = 0, n = 0; i < 16; i++)
            n += snprintf(seq + n, sizeof(seq) - n, " %u", fPeerChan[i]);
        setProperty("AWDLPeerChanSeq", seq);
    }
    ieee80211com *ic = fController ? fController->fHalService->get80211Controller() : nullptr;
    if (!ic)
        return;
    setProperty("AWDLListenDone", ic->ic_awdl_listen_done, 32);
    setProperty("AWDLListenRxAll", ic->ic_awdl_listen_rx_all, 32);
    setProperty("AWDLListenRxBeacon", ic->ic_awdl_listen_rx_beacon, 32);
    setProperty("AWDLListenRxAction", ic->ic_awdl_listen_rx_action, 32);
    setProperty("AWDLRocSent", ic->ic_awdl_roc_sent, 32);
    setProperty("AWDLRocRespErr", ic->ic_awdl_roc_resp_err, 32);
    setProperty("AWDLRocStarted", ic->ic_awdl_roc_started, 32);
    setProperty("AWDLRocEnded", ic->ic_awdl_roc_ended, 32);
    setProperty("AWDLRocNotScheduled", ic->ic_awdl_roc_not_sched, 32);
    setProperty("AWDLRocStartFail", ic->ic_awdl_roc_start_fail, 32);
    setProperty("AWDLRocEndFail", ic->ic_awdl_roc_end_fail, 32);
    setProperty("AWDLRocEndFailShort", ic->ic_awdl_roc_end_fail_short, 32);
    setProperty("AWDLRocEndFailLastUs", ic->ic_awdl_roc_end_fail_last_us, 32);
    setProperty("AWDLRocChanChanges", ic->ic_awdl_roc_chan_changes, 32);
    setProperty("AWDLRocBandChanges", ic->ic_awdl_roc_band_changes, 32);
    char per[96];
    snprintf(per, sizeof(per), "6:%llu/%llu 44:%llu/%llu 149:%llu/%llu 161:%llu/%llu",
             fListenOnChan[0], fRxAFChan6, fListenOnChan[1], fRxAFChan44,
             fListenOnChan[2], fRxAFChan149, fListenOnChan[3], fRxAFChan161);
    setProperty("AWDLListenByChan", per);
    if (awTxAllowed())
        publishTxState();
    if (awDataAllowed())
        publishDataState();
    setProperty("AWDLRocStaErr", ic->ic_awdl_roc_sta_err, 32);
    setProperty("AWDLRocStaStage", ic->ic_awdl_roc_sta_stage, 32);
    setProperty("AWDLRocStaCode", ic->ic_awdl_roc_sta_code, 32);
    setProperty("AWDLRocQueue", ic->ic_awdl_roc_qid, 32);
    setProperty("AWDLRocLastUs", ic->ic_awdl_roc_last_us, 32);
    setProperty("AWDLRocMinUs", ic->ic_awdl_roc_min_us, 32);
    setProperty("AWDLRocMaxUs", ic->ic_awdl_roc_max_us, 32);
    setProperty("AWDLRocDelayUs", ic->ic_awdl_roc_delay_us, 32);
    setProperty("AWDLRocRxTarget", ic->ic_awdl_roc_rx_target, 32);
    setProperty("AWDLRocRxHome", ic->ic_awdl_roc_rx_home, 32);
    setProperty("AWDLRocRxOther", ic->ic_awdl_roc_rx_other, 32);
    setProperty("AWDLRocRxLastChannel", ic->ic_awdl_roc_rx_last_chan, 32);
}

void AirportItlwmAWDLInterface::
maybeListen(uint32_t slotChannel, uint16_t seq)
{
    if (!awListenAllowed() || !fController || !slotChannel)
        return;
    ieee80211com *ic = fController->fHalService->get80211Controller();
    if (!ic || !ic->ic_awdl_listen || ic->ic_state != IEEE80211_S_RUN || !ic->ic_bss)
        return;
    unsigned home = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
    if (unsigned forced = awListenChannelOverride())
        slotChannel = forced;
    uint32_t dwell = fListenDwellTU;
    if (home == slotChannel) {
        if (!awHomeListenAllowed(fPeerTrafficActive, ic->ic_awdl_want_data) ||
            hostUs() < fHomeListenNotBeforeUs) {
            fListenSkippedHome++;
            return;
        }
        if (dwell > kHomeListenMaxTU)
            dwell = kHomeListenMaxTU;
    }
    // The peer's timeline is known: the listen timer picks the moments instead.
    if (fListenAlignedActive)
        return;
    if (++fListenSlot % fListenEveryNth)
        return;
    startListen(slotChannel, dwell, home, false);
}

void AirportItlwmAWDLInterface::
startListen(unsigned chan, uint32_t dwellTU, unsigned home, bool aligned)
{
    ieee80211com *ic = fController->fHalService->get80211Controller();
    if (home && chan != home && ic->ic_opmode == IEEE80211_M_STA &&
        ic->ic_state == IEEE80211_S_RUN && fPeerTrafficActive == 0) {
        unsigned nth = awInfraListenNth(ic->ic_awdl_want_data != 0);
        if (!nth || ++fListenInfraSlot % nth) {
            fListenInfraHold++;
            if ((fListenInfraHold & 0xff) == 1)
                setProperty("AWDLListenInfraHold", fListenInfraHold, 64);
            /*
             * Holding listens is not enough: after the last one the P2P MAC
             * stays bound to a PHY on the AWDL channel, and the firmware
             * keeps sharing time with it (2.7.33: listens held, still 100%
             * loss). One short listen on the infra channel moves that PHY
             * there; repeat until it is accepted.
             */
            if (fListenParkChan != home &&
                ic->ic_awdl_listen(ic, (uint8_t)home, (uint16_t)kListenParkTU) == 0) {
                fListenParkChan = home;
                fListenParked++;
                setProperty("AWDLListenParked", fListenParked, 64);
            }
            return;
        }
    }
    int err = ic->ic_awdl_listen(ic, (uint8_t)chan, (uint16_t)dwellTU);
    if (err == 0)
        fListenParkChan = (chan == home) ? home : 0;
    if (err == 0 && home && chan == home) {
        uint64_t d = (uint64_t)dwellTU * 1024;
        fHomeListenNotBeforeUs = hostUs() + d + d * (fPeerTrafficActive > 0 ? 1 : 3);
        fHomeListens++;
        setProperty("AWDLListenHome", fHomeListens, 64);
    }
    if (err) {
        fListenBusy++;
    } else {
        fListenStarted++;
        if (aligned)
            fListenAligned++;
        int idx = chan == 6 ? 0 : chan == 44 ? 1 : chan == 149 ? 2 : chan == 161 ? 3 : -1;
        if (idx >= 0)
            fListenOnChan[idx]++;
        if (canTxOnChannel((uint8_t)chan) &&
            !__atomic_load_n(&fRocNotifyChan, __ATOMIC_ACQUIRE))
            scheduleTx((uint8_t)chan, hostUs());
    }
    if (((fListenStarted + fListenBusy) & 0x1f) == 1) {
        publishListenState();
        setProperty("AWDLListenHomeChannel", home, 32);
        setProperty("AWDLListenChannel", chan, 32);
        setProperty("AWDLListenLastErr", err, 32);
    }
}

// A listen goes out this long before the slot: the firmware took about 4.4 ms
// from the command to its START notification.
static const int64_t kListenLeadUs = 6000;
// Without a frame from the peer for this long, go back to listening blindly.
static const uint64_t kPeerStaleUs = 30000000;
static const uint64_t kPeerReplaceUs = 10000000;

static int64_t floorDiv(int64_t a, int64_t b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

// Channel sequence as in a TLV 18 value (seemoo-lab/owl rx.c): count - 1,
// encoding, duplicate count, step count, fill channel (2), then the entries.
bool AirportItlwmAWDLInterface::
parsePeerChanSeq(const uint8_t *v, size_t len)
{
    if (len < 6 || v[0] + 1 != 16)
        return false;
    size_t size = v[1] == 0 ? 1 : (v[1] == 1 || v[1] == 3) ? 2 : 0;
    if (!size || len < 6 + 16 * size)
        return false;
    // Legacy keeps the channel in the second byte, op class in the first.
    size_t at = v[1] == 1 ? 1 : 0;
    for (unsigned i = 0; i < 16; i++)
        fPeerChan[i] = v[6 + i * size + at];
    fPeerChanValid = true;
    return true;
}

unsigned AirportItlwmAWDLInterface::
peerChannelAt(uint64_t us) const
{
    if (!fPeerValid || !fPeerChanValid || !fPeerSlotUs || !fPeerPm)
        return 0;
    int64_t k = floorDiv((int64_t)us - fPeerRefUs, fPeerSlotUs);
    uint16_t seq = (uint16_t)(fPeerRefSeq + k * fPeerPm);
    return fPeerChan[(seq / fPeerPm) % 16];
}

// Keep the timeline of the peer we hear, whoever our sync master is: when we
// are master ourselves syncFromFrame learns nothing about it.
void AirportItlwmAWDLInterface::
trackPeer(const uint8_t *af, size_t len, const uint8_t *src, uint64_t rxUs, unsigned rxChan)
{
    if (!fAWRunning || !awListenAllowed())
        return;
    if (fPeerValid && memcmp(src, &fPeerMac, sizeof(fPeerMac))) {
        if ((int64_t)(rxUs - fPeerSeenUs) < (int64_t)kPeerReplaceUs)
            return;
        fPeerValid = false;
        fPeerChanValid = false;
    }
    // Checks the slot-to-channel mapping, which is inferred for a peer.
    if (unsigned predicted = peerChannelAt(rxUs)) {
        if (predicted == rxChan)
            fPeerPredHit++;
        else
            fPeerPredMiss++;
    }
    size_t off = kAWDLFixedHeaderLen;
    bool sync = false;
    while (off + 3 <= len) {
        uint8_t type = af[off];
        uint16_t tlen = af[off + 1] | (af[off + 2] << 8);
        const uint8_t *v = af + off + 3;
        if (off + 3 + tlen > len)
            break;
        if (type == kAWDLSyncParamsTLV && tlen >= 31) {
            uint16_t down = v[1] | (v[2] << 8);
            uint16_t period = v[5] | (v[6] << 8);
            uint16_t pm = v[27];
            uint16_t nextSeq = v[29] | (v[30] << 8);
            if (period >= 4 && period <= 64 && pm >= 1 && pm <= 16) {
                int64_t boundary = (int64_t)rxUs + (int64_t)down * 1024;
                uint32_t slot = (uint32_t)period * pm * 1024;
                if (fPeerValid && fPeerSlotUs == slot) {
                    int64_t d = boundary - fPeerRefUs;
                    fPeerLastErrUs = d - floorDiv(d + slot / 2, slot) * slot;
                }
                fPeerRefUs = boundary;
                fPeerRefSeq = (uint16_t)(nextSeq / pm * pm + pm);
                fPeerPm = pm;
                fPeerSlotUs = slot;
                sync = true;
            }
            if (tlen > 33)
                parsePeerChanSeq(v + 33, tlen - 33);
        } else if (type == kAWDLChanSeqTLV) {
            parsePeerChanSeq(v, tlen);
        }
        off += 3 + tlen;
    }
    if (!sync)
        return;
    memcpy(&fPeerMac, src, sizeof(fPeerMac));
    fPeerValid = true;
    fPeerSeenUs = rxUs;
    fPeerUpdates++;
    if (awDataAllowed() && fAWRunning && fController) {
        if (ieee80211com *ic = fController->fHalService->get80211Controller()) {
            if (!ic->ic_awdl_want_data) {
                ic->ic_awdl_want_data = 1;
                notifyHalSync();
            }
        }
    }
    if (!fListenAlignedActive)
        scheduleAlignedListen(hostUs());
}

// Arm the listen timer for the next run of peer slots we should visit.
uint8_t AirportItlwmAWDLInterface::
ownChannelAt(uint16_t seq) const
{
    unsigned count = fChanSeqValid ? fChanSeq[kChanSeqCount] : 0;
    if (!count)
        return 0;
    uint16_t pm = presenceMode();
    const apple80211_channel *c = (const apple80211_channel *)(fChanSeq + kChanSeqEntries) + (seq / pm) % count;
    return (uint8_t)c->channel;
}

// Next slot of our own timeline whose channel is not 0 (0 = not on AWDL, the
// firmware stays on the infra channel and posts no AW start) and is not the
// station channel. Dwell for the run of equal-channel slots.
bool AirportItlwmAWDLInterface::
scheduleOwnListen(uint64_t afterUs)
{
    if (!fListenTimer || !fAWRunning || !fChanSeqValid || !fController)
        return false;
    ieee80211com *ic = fController->fHalService->get80211Controller();
    if (!ic || ic->ic_state != IEEE80211_S_RUN || !ic->ic_bss)
        return false;
    unsigned home = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
    uint64_t now = hostUs();
    bool wantData = awDataAllowed() && fAWRunning &&
        (fPeerTrafficActive > 0 || !fPeerValid || now - fPeerSeenUs <= kPeerStaleUs);
    if ((int)wantData != ic->ic_awdl_want_data) {
        ic->ic_awdl_want_data = wantData ? 1 : 0;
        notifyHalSync();
    }
    uint64_t from = afterUs > now ? afterUs : now;
    int64_t slot = (int64_t)slotUs();
    int64_t lead = effectiveListenLeadUs();
    uint16_t pm = presenceMode();
    int64_t k = floorDiv((int64_t)from + lead - fRefUs, slot) + 1;
    // The peer we track (the phone during AirDrop) must be heard at least every
    // few seconds or IO80211Family drops it from NDP (lastAF ~6 s). When our own
    // slot keeps us home (or off AWDL) but the peer's slot at that time is on a
    // non-home channel, listen there instead (on unless -itlawdlnopeerlisten).
    bool usePeer = awPeerListenAllowed() && fPeerValid && fPeerChanValid && fPeerSlotUs &&
        fPeerPm && now - fPeerSeenUs <= kPeerStaleUs;
    bool homeOk = awHomeListenAllowed(fPeerTrafficActive, ic->ic_awdl_want_data);
    for (int i = 0; i < 32; i++, k++) {
        uint16_t seq = (uint16_t)(fRefSeq + k * pm);
        uint8_t chan = ownChannelAt(seq);
        bool fromPeer = false;
        if ((!chan || chan == home) && usePeer) {
            unsigned pc = peerChannelAt((uint64_t)(fRefUs + k * slot + slot / 2));
            if (pc && pc != home) {
                chan = (uint8_t)pc;
                fromPeer = true;
            }
        }
        bool atHome = chan && chan == home;
        if (!chan || (atHome && (!homeOk ||
            (uint64_t)(fRefUs + k * slot) < fHomeListenNotBeforeUs)))
            continue;
        unsigned run = 1;
        if (!fromPeer) {
            while (run < 16 && ownChannelAt((uint16_t)(seq + run * pm)) == chan)
                run++;
        }
        if (fromPeer)
            fListenPeerSlots++;
        uint32_t dwell = (uint32_t)((run * slot + lead) / 1024);
        if (dwell > fListenDwellTU)
            dwell = fListenDwellTU;
        if (atHome && dwell > kHomeListenMaxTU)
            dwell = kHomeListenMaxTU;
        fListenNextChan = chan;
        fListenNextDwellTU = dwell;
        fListenNextHome = atHome;
        uint64_t abs;
        nanoseconds_to_absolutetime((uint64_t)(fRefUs + k * slot - lead) * 1000, &abs);
        fListenTimer->cancelTimeout();
        fListenTimer->wakeAtTime(abs);
        fListenAlignedActive = true;
        return true;
    }
    fListenNoSlot++;
    return false;
}

void AirportItlwmAWDLInterface::
scheduleAlignedListen(uint64_t afterUs)
{
    fListenAlignedActive = false;
    if (awDataAllowed()) {
        scheduleOwnListen(afterUs);
        return;
    }
    if (!fListenTimer || !fListenAlign || !fAWRunning || !fPeerValid || !fPeerChanValid || !fController)
        return;
    uint64_t now = hostUs();
    if (now - fPeerSeenUs > kPeerStaleUs)
        return;
    ieee80211com *ic = fController->fHalService->get80211Controller();
    if (!ic || ic->ic_state != IEEE80211_S_RUN || !ic->ic_bss)
        return;
    unsigned home = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
    unsigned forced = awListenChannelOverride();
    int64_t slot = fPeerSlotUs;
    uint64_t from = afterUs > now ? afterUs : now;
    int64_t k = floorDiv((int64_t)from + kListenLeadUs - fPeerRefUs, slot) + 1;
    for (int i = 0; i < 32; i++, k++) {
        uint16_t seq = (uint16_t)(fPeerRefSeq + k * fPeerPm);
        unsigned chan = fPeerChan[(seq / fPeerPm) % 16];
        if (!chan || chan == home || (forced && chan != forced))
            continue;
        // Stay for the whole run of slots on this channel, up to the dwell limit.
        unsigned run = 1;
        while (run < 16 && fPeerChan[((seq / fPeerPm) + run) % 16] == chan)
            run++;
        int64_t lead = effectiveListenLeadUs();
        uint32_t dwell = (uint32_t)((run * slot + lead) / 1024);
        if (dwell > fListenDwellTU)
            dwell = fListenDwellTU;
        fListenNextChan = (uint8_t)chan;
        fListenNextDwellTU = dwell;
        fListenNextHome = false;
        uint64_t abs;
        nanoseconds_to_absolutetime((uint64_t)(fPeerRefUs + k * slot - lead) * 1000, &abs);
        fListenTimer->cancelTimeout();
        fListenTimer->wakeAtTime(abs);
        fListenAlignedActive = true;
        return;
    }
    fListenNoSlot++;
}

void AirportItlwmAWDLInterface::
listenTimerFired(OSObject *owner, IOTimerEventSource *)
{
    AirportItlwmAWDLInterface *that = OSDynamicCast(AirportItlwmAWDLInterface, owner);
    if (!that || !that->fListenAlignedActive || !that->fAWRunning || !that->fController)
        return;
    uint64_t now = hostUs();
    uint32_t dwell = that->fListenNextDwellTU;
    ieee80211com *ic = that->fController->fHalService->get80211Controller();
    if (ic && ic->ic_awdl_listen && ic->ic_state == IEEE80211_S_RUN && ic->ic_bss) {
        unsigned home = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
        bool ok = home != that->fListenNextChan ||
            (that->fListenNextHome && awHomeListenAllowed(that->fPeerTrafficActive, ic->ic_awdl_want_data));
        if (ok && !(++that->fListenSlot % that->fListenEveryNth)) {
            uint64_t before = that->fListenStarted;
            that->startListen(that->fListenNextChan, dwell, home, true);
            if (that->fListenStarted != before && awDataAllowed())
                that->fListenOwn++;
        }
    }
    that->scheduleAlignedListen(now + (uint64_t)dwell * 1024);
}

#pragma mark - stage 3 step 1: transmit (host side of the firmware PSF / MI engine)

// The peer manager hands down TLVs only (see updateSyncFrameTemplate). Broadcom firmware
// adds the 802.11 header, the AWDL fixed header, the synchronization
// parameters (TLV 4) and the channel sequence (TLV 18). We do the same and
// send during an aligned listen dwell, on the P2P station's queue, in the
// peer's AW: PSF 1 ms and MI 4 ms after the peer's slot boundary.
static const uint8_t kAWDLBssid[6] = { 0x00, 0x25, 0x00, 0xff, 0x94, 0x73 };
static const int64_t kTxPSFAtUs = 1000;
static const int64_t kTxMIAtUs = 4000;
enum : uint8_t { kAWDLSubtypePSF = 0, kAWDLSubtypeMI = 3 };

unsigned AirportItlwmAWDLInterface::
currentHomeChannel() const
{
    if (!fController || !fController->fHalService)
        return 0;
    ieee80211com *ic = fController->fHalService->get80211Controller();
    if (!ic || ic->ic_state != IEEE80211_S_RUN || !ic->ic_bss ||
        !ic->ic_bss->ni_chan || ic->ic_bss->ni_chan == IEEE80211_CHAN_ANYC)
        return 0;
    return ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
}

bool AirportItlwmAWDLInterface::
canTxOnChannel(uint8_t chan) const
{
    if (!chan || !fController || !fController->fHalService)
        return false;
    ieee80211com *ic = fController->fHalService->get80211Controller();
    if (!ic || !ic->ic_awdl_tx)
        return false;
    if (ic->ic_awdl_listen_active && ic->ic_awdl_listen_chan == chan)
        return true;
    if (!ic->ic_awdl_listen_active && currentHomeChannel() == chan)
        return true;
    return false;
}

void AirportItlwmAWDLInterface::
notifyHalSync()
{
    if (!fController || !fController->fHalService)
        return;
    if (ieee80211com *ic = fController->fHalService->get80211Controller()) {
        if (ic->ic_awdl_sync_notify)
            ic->ic_awdl_sync_notify(ic);
    }
}

void AirportItlwmAWDLInterface::
scheduleTx(uint8_t chan, uint64_t slotStartUs)
{
    if (!awTxAllowed() || !fTxTimer || !fAWRunning)
        return;
    // Note: IO80211AWDLPeerManager sets fAfTxMode = 3 during Scan + Data (host
    // OOB/UMI mode). Never abort TX on mode == 3 — PSF, OOB/UMI, and queued
    // awdl0 data frames must still go out.
    fTxChan = chan;
    fTxSlotUs = slotStartUs;
    fTxTimer->cancelTimeout();
    if (chan == currentHomeChannel() || canTxOnChannel(chan)) {
        fTxPhase = 1;
        uint64_t abs;
        nanoseconds_to_absolutetime((slotStartUs + kTxPSFAtUs) * 1000, &abs);
        fTxTimer->wakeAtTime(abs);
    } else {
        // Off-channel ROC is granted asynchronously by firmware (up to ~55 ms
        // when waiting for an imminent AP beacon); rocStartedThunk wakes
        // fTxTimer as soon as the session-protection START notification fires.
        fTxPhase = 0;
    }
}

int64_t AirportItlwmAWDLInterface::
effectiveListenLeadUs() const
{
    if (fController && fController->fHalService) {
        if (ieee80211com *ic = fController->fHalService->get80211Controller()) {
            // ic_awdl_roc_delay_us is a peak-held start latency; add margin for jitter.
            uint32_t d = ic->ic_awdl_roc_delay_us + 1500;
            if (d > (uint32_t)kListenLeadUs)
                return d > 25000 ? 25000 : (int64_t)d;
        }
    }
    return kListenLeadUs;
}

bool AirportItlwmAWDLInterface::
peerUsesChannel(uint8_t chan) const
{
    if (!chan || !fPeerChanValid)
        return true;
    for (unsigned i = 0; i < 16; i++)
        if (fPeerChan[i] == chan)
            return true;
    return false;
}

bool AirportItlwmAWDLInterface::
canFlushDataOnChannel(uint8_t chan, uint64_t whenUs) const
{
    if (!chan)
        return false;
    unsigned home = currentHomeChannel();
    if (chan == home) {
        if (fPeerChanValid) {
            unsigned pc = peerChannelAt(whenUs);
            return pc != 0 && pc == home;
        }
        return home == 6 || home == 44 || home == 149;
    }
    if (!fPeerChanValid)
        return true;
    unsigned pc = peerChannelAt(whenUs);
    return pc == chan || (pc == 0 && peerUsesChannel(chan));
}

void AirportItlwmAWDLInterface::
rocStartedThunk(void *arg, uint8_t chan)
{
    AirportItlwmAWDLInterface *that = static_cast<AirportItlwmAWDLInterface *>(arg);
    if (!that || !chan || !that->fAWRunning || !awTxAllowed())
        return;
    __atomic_store_n(&that->fRocNotifyChan, chan, __ATOMIC_RELEASE);
    if (IOTimerEventSource *t = that->fTxTimer)
        t->setTimeoutUS(1000);
}

void AirportItlwmAWDLInterface::
txTimerFired(OSObject *owner, IOTimerEventSource *)
{
    AirportItlwmAWDLInterface *that = OSDynamicCast(AirportItlwmAWDLInterface, owner);
    if (!that || !that->fAWRunning)
        return;
    uint8_t rocChan = __atomic_exchange_n(&that->fRocNotifyChan, 0, __ATOMIC_ACQUIRE);
    if (rocChan) {
        that->fTxChan = rocChan;
        that->fTxSlotUs = hostUs();
        that->fTxPhase = 1;
    }
    if (!that->fTxPhase)
        return;
    bool onRoc = that->fTxChan != that->currentHomeChannel();
    if (onRoc && !that->canTxOnChannel(that->fTxChan)) {
        that->fTxPhase = 0;
        return;
    }
    if (that->fTxPhase == 1) {
        if (that->fSyncTmplLen)
            that->sendAWDLFrame(false);
        that->trySendPendingOOB();
        if (awDataAllowed() && that->fDataQCount &&
            that->canFlushDataOnChannel(that->fTxChan, hostUs()))
            that->flushData(that->fTxChan);
        that->fTxPhase = 2;
        uint64_t abs;
        uint64_t miOffsetUs = onRoc ? 16000 : kTxMIAtUs;
        nanoseconds_to_absolutetime((that->fTxSlotUs + miOffsetUs) * 1000, &abs);
        that->fTxTimer->wakeAtTime(abs);
        return;
    }
    if (that->fTxPhase == 2) {
        that->trySendPendingOOB();
        if (that->fMITmplLen && that->fAfTxMode != 2)
            that->sendAWDLFrame(true);
        if (awDataAllowed()) {
            if (that->canFlushDataOnChannel(that->fTxChan, hostUs()))
                that->flushData(that->fTxChan);
            else if (that->fDataQCount)
                that->fDataTxNotCommon++;
        }
        if (onRoc) {
            that->fTxPhase = 3;
            uint64_t abs;
            nanoseconds_to_absolutetime((that->fTxSlotUs + 40000) * 1000, &abs);
            that->fTxTimer->wakeAtTime(abs);
        } else {
            that->fTxPhase = 0;
        }
        if (((that->fTxPSF + that->fTxMI + that->fTxRefused) & 0x1f) <= 1)
            that->publishTxState();
        return;
    }
    that->fTxPhase = 0;
    that->trySendPendingOOB();
    if (awDataAllowed() && that->fDataQCount &&
        that->canFlushDataOnChannel(that->fTxChan, hostUs()))
        that->flushData(that->fTxChan);
}

// Sync parameters TLV as parsed by IO80211AWDLPeer::parseAwdlSyncParamsTlv
// (same layout as OWL's awdl_sync_params_tlv), times from our AW timeline.
size_t AirportItlwmAWDLInterface::
buildAWDLFrame(uint8_t *buf, size_t cap, bool mi, const uint8_t *dstMac, const uint8_t *tmpl, size_t tmplLen, uint64_t nowUs)
{
    if (!tmpl || !tmplLen)
        return 0;
    // Channel entries are 2 bytes (op class encoding): the receiver's
    // IO80211AWDLPeerManager::isValidChanSeqTlv wants at least 2 * count + 2
    // bytes and drops the whole frame otherwise (iPhone: "bad chan seq tlv",
    // parse error reason 1, with the 1-byte encoding).
    const size_t syncLen = 3 + 33 + 6 + 32 + 2;
    const size_t seqLen = 3 + 6 + 32 + 3;
    size_t need = sizeof(struct ieee80211_frame) + kAWDLFixedHeaderLen + syncLen + seqLen + tmplLen;
    if (need > cap)
        return 0;
    memset(buf, 0, need);

    struct ieee80211_frame *wh = (struct ieee80211_frame *)buf;
    wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_ACTION;
    if (dstMac)
        memcpy(wh->i_addr1, dstMac, 6);
    else
        memset(wh->i_addr1, 0xff, 6);
    memcpy(wh->i_addr2, &fMac, 6);
    const uint8_t *bssid = kAWDLBssid;
    if (fController && fController->fHalService) {
        if (ieee80211com *ic = fController->fHalService->get80211Controller()) {
            if (ic->ic_awdl_bssid_valid)
                bssid = ic->ic_awdl_bssid;
        }
    }
    memcpy(wh->i_addr3, bssid, 6);
    uint16_t sc = (uint16_t)(fTxSeq++ << 4);
    wh->i_seq[0] = sc & 0xff;
    wh->i_seq[1] = sc >> 8;

    uint8_t *af = buf + sizeof(*wh);
    af[0] = 127;
    af[1] = 0x00; af[2] = 0x17; af[3] = 0xf2;
    af[4] = 8;
    af[5] = 0x10;           // version 1.0; IO80211 rejects 0 (actionFrameHasCorrectOUI)
    af[6] = mi ? kAWDLSubtypeMI : kAWDLSubtypePSF;
    af[7] = 0;
    uint32_t t = (uint32_t)nowUs;
    memcpy(af + 8, &t, 4);  // phy_tx
    memcpy(af + 12, &t, 4); // target_tx

    // Our timeline: the extended AW (slot) we are in and the next boundary.
    uint16_t pm = presenceMode();
    uint32_t period = fSyncParams.availability_window_period;
    if (period < 4 || period > 64)
        period = 16;
    int64_t slot = (int64_t)slotUs();
    int64_t k = floorDiv((int64_t)nowUs - fRefUs, slot);
    int64_t start = fRefUs + k * slot;
    uint16_t seq = (uint16_t)(fRefSeq + k * pm);
    uint16_t curAW = (uint16_t)(seq + ((int64_t)nowUs - start) / ((int64_t)period * 1024));
    uint16_t down = (uint16_t)((start + slot - (int64_t)nowUs + 1023) / 1024);
    unsigned count = fChanSeqValid ? fChanSeq[kChanSeqCount] : 0;
    auto chanAt = [&](uint16_t s) -> uint8_t {
        if (!count)
            return 6;
        const apple80211_channel *c = (const apple80211_channel *)(fChanSeq + kChanSeqEntries) + (s / pm) % count;
        return (uint8_t)c->channel;
    };
    const ether_addr &master = isSyncMaster() ? fMac : fSyncMaster;
    uint8_t masterChan = fMasterChannel ? (uint8_t)fMasterChannel : 6;

    uint8_t *p = af + kAWDLFixedHeaderLen;
    auto put16 = [](uint8_t *q, uint16_t v) { q[0] = v & 0xff; q[1] = v >> 8; };
    // Global operating class of a 20 MHz channel (IEEE 802.11 Annex E).
    auto opClass = [](uint8_t c) -> uint8_t {
        if (!c) return 0;
        if (c <= 13) return 81;
        if (c <= 48) return 115;
        if (c <= 64) return 118;
        if (c <= 144) return 121;
        return 124;
    };
    auto chanSeq = [&](uint8_t *q) {
        q[0] = 15;          // count - 1
        q[1] = 3;           // op class encoding: channel, op class
        // Header bytes as AppleBCMWLANProximityInterface::buildAWDLChanSequence
        // takes them from the system's sequence: step +7, duplicate +8,
        // fill +9 (255 = 0xffff, repeat the current channel).
        q[2] = fChanSeqValid ? fChanSeq[8] : 0;
        q[3] = fChanSeqValid ? fChanSeq[kChanSeqStep] : (uint8_t)(pm - 1);
        uint8_t fill = fChanSeqValid ? fChanSeq[9] : 255;
        put16(q + 4, fill == 255 ? 0xffff : (uint16_t)(fill | (opClass(fill) << 8)));
        for (unsigned i = 0; i < 16; i++) {
            uint8_t c = count ? chanAt((uint16_t)(i * pm)) : 6;
            q[6 + 2 * i] = c;
            q[7 + 2 * i] = opClass(c);
        }
    };
    p[0] = kAWDLSyncParamsTLV;
    put16(p + 1, (uint16_t)(syncLen - 3));
    uint8_t *v = p + 3;
    v[0] = chanAt((uint16_t)(seq + pm));    // next AW channel
    put16(v + 1, down);                     // tx_down_counter
    v[3] = masterChan;
    v[4] = 0;                               // guard time
    put16(v + 5, (uint16_t)period);         // aw_period
    put16(v + 7, (uint16_t)(fSyncParams.synchronization_frame_period ?: 110));
    put16(v + 9, 0x1800);                   // flags, as OWL
    put16(v + 11, (uint16_t)period);        // aw_ext_length (16, else "Zero AWExt")
    put16(v + 13, (uint16_t)period);        // aw_com_length
    int64_t rem = (int64_t)period - (int64_t)((nowUs - start) / 1024);
    put16(v + 15, (uint16_t)(rem > 0 ? rem : 0));
    v[17] = v[18] = v[19] = v[20] = (uint8_t)(pm - 1);
    memcpy(v + 21, &master, 6);
    v[27] = (uint8_t)pm;
    put16(v + 29, curAW);                   // next_aw_seq
    put16(v + 31, curAW);                   // ap_alignment
    chanSeq(v + 33);
    p += syncLen;

    p[0] = kAWDLChanSeqTLV;
    put16(p + 1, (uint16_t)(seqLen - 3));
    chanSeq(p + 3);
    p += seqLen;

    memcpy(p, tmpl, tmplLen);
    return need;
}

void AirportItlwmAWDLInterface::
sendAWDLFrame(bool mi)
{
    const uint8_t *tmpl = mi ? fMITmpl : fSyncTmpl;
    size_t tmplLen = mi ? fMITmplLen : fSyncTmplLen;
    if (sendAWDLPayload(mi, nullptr, tmpl, tmplLen, fTxChan)) {
        if (mi)
            fTxMI++;
        else
            fTxPSF++;
    }
}

bool AirportItlwmAWDLInterface::
sendAWDLPayload(bool mi, const uint8_t *dstMac, const uint8_t *tmpl, size_t tmplLen, uint8_t chan)
{
    ieee80211com *ic = fController ? fController->fHalService->get80211Controller() : nullptr;
    if (!ic || !ic->ic_awdl_tx || !chan)
        return false;
    static uint8_t frame[2048];             // work loop only
    size_t len = buildAWDLFrame(frame, sizeof(frame), mi, dstMac, tmpl, tmplLen, hostUs());
    if (!len) {
        fTxLastErr = 0x100;
        fTxRefused++;
        return false;
    }
    int err = ic->ic_awdl_tx(ic, chan, frame, len);
    if (err) {
        fTxRefused++;
        fTxLastErr = err;
        return false;
    }
    fTxLastLen = (uint32_t)len;
    return true;
}

void AirportItlwmAWDLInterface::
trySendPendingOOB()
{
    if (!fOOBReqPending || !awTxAllowed() || !fController)
        return;
    ieee80211com *ic = fController->fHalService->get80211Controller();
    if (!ic)
        return;
    uint8_t chan = 0;
    if (ic->ic_awdl_listen_active && ic->ic_awdl_listen_chan) {
        chan = ic->ic_awdl_listen_chan;
    } else if (unsigned home = currentHomeChannel()) {
        if (canFlushDataOnChannel((uint8_t)home, hostUs()))
            chan = (uint8_t)home;
    }
    if (!chan || !canTxOnChannel(chan))
        return;
    if (sendAWDLPayload(true, fOOBReqDst, fOOBReqBuf, fOOBReqLen, chan)) {
        fOOBReqPending = false;
        fTxOOBSent++;
        // Real firmware reports unicast action-frame TX status (event 69). The family waits for it
        // (EnsembleUMIStatusDone, 3 s) after sendUnicastMI; without it Continuity Camera's
        // RemoteCamera traffic registration fails. Broadcast MIs have no peer to complete.
        const bool nonZero = (fOOBReqDst[0] | fOOBReqDst[1] | fOOBReqDst[2] |
                              fOOBReqDst[3] | fOOBReqDst[4] | fOOBReqDst[5]) != 0;
        if (nonZero && !(fOOBReqDst[0] & 1) && fPeerCacheTimer && fPeerCacheLock) {
            bool queued = false;
            IOSimpleLockLock(fPeerCacheLock);
            if (fPeerCacheQCount < kPeerCacheQLen) {
                PeerCacheDone &e = fPeerCacheQ[fPeerCacheQCount++];
                e.evt = 69;
                e.op = 0;
                memcpy(e.mac, fOOBReqDst, 6);
                queued = true;
            } else {
                fPeerCacheDrop++;
            }
            IOSimpleLockUnlock(fPeerCacheLock);
            if (queued)
                fPeerCacheTimer->setTimeoutUS(300);
        }
        publishTxState();
    }
}

void AirportItlwmAWDLInterface::
enqueueData(mbuf_t m)
{
    if (!m)
        return;
    if (fDataQCount >= kDataQueueLen) {
        fDataTxDropFull++;
        mbuf_freem(m);
        return;
    }
    fDataQ[(fDataQHead + fDataQCount) % kDataQueueLen] = m;
    fDataQCount++;
    fDataTxQueued++;
    // Flush immediately when already on an active dwell or on the home channel
    // during a slot the peer can hear, avoiding multi-dwell RTT stalls during
    // NDP, mDNS, and TCP/TLS AirDrop handshakes.
    if (fController && fController->fHalService) {
        if (ieee80211com *ic = fController->fHalService->get80211Controller()) {
            if (ic->ic_awdl_listen_active && ic->ic_awdl_listen_chan) {
                flushData(ic->ic_awdl_listen_chan);
            } else if (unsigned home = currentHomeChannel()) {
                if (canTxOnChannel((uint8_t)home) &&
                    canFlushDataOnChannel((uint8_t)home, hostUs()))
                    flushData((uint8_t)home);
            }
        }
    }
}

// Ethernet -> 802.11 data (no QoS) + LLC/SNAP 00:17:f2 0x0800 + AWDL data
// header (0x0403 LE, sequence LE, pad, ethertype BE) + payload, as OWL's
// awdl_init_full_data_frame. Sent on the P2P queue while dwelling on the
// peer's channel or on the BSS queue when sharing the home channel.
void AirportItlwmAWDLInterface::
flushData(uint8_t chan)
{
    ieee80211com *ic = fController ? fController->fHalService->get80211Controller() : nullptr;
    if (!ic || !ic->ic_awdl_tx)
        return;
    uint8_t txChan = chan ? chan : fTxChan;
    if (!txChan) {
        if (ic->ic_awdl_listen_active && ic->ic_awdl_listen_chan)
            txChan = ic->ic_awdl_listen_chan;
        else
            txChan = (uint8_t)currentHomeChannel();
    }
    if (!txChan)
        return;
    const uint8_t *bssid = ic->ic_awdl_bssid_valid ? ic->ic_awdl_bssid : kAWDLBssid;
    static uint8_t frame[2048];             // work loop only
    while (fDataQCount) {
        mbuf_t m = fDataQ[fDataQHead];
        size_t plen = mbuf_pkthdr_len(m);
        const size_t hdr = sizeof(struct ieee80211_frame), llc = 8, ad = 8;
        if (plen < 14 || hdr + llc + ad + plen - 14 > sizeof(frame)) {
            fDataTxErr++;
        } else {
            uint8_t eh[14];
            mbuf_copydata(m, 0, sizeof(eh), eh);
            memset(frame, 0, hdr + llc + ad);
            struct ieee80211_frame *wh = (struct ieee80211_frame *)frame;
            wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_DATA | IEEE80211_FC0_SUBTYPE_DATA;
            memcpy(wh->i_addr1, eh, 6);
            memcpy(wh->i_addr2, &fMac, 6);
            memcpy(wh->i_addr3, bssid, 6);
            uint16_t sc = (uint16_t)(fTxSeq++ << 4);
            wh->i_seq[0] = sc & 0xff;
            wh->i_seq[1] = sc >> 8;
            uint8_t *l = frame + hdr;
            l[0] = 0xaa; l[1] = 0xaa; l[2] = 0x03;
            l[3] = 0x00; l[4] = 0x17; l[5] = 0xf2;
            l[6] = 0x08; l[7] = 0x00;
            uint8_t *a = l + llc;
            a[0] = 0x03; a[1] = 0x04;
            a[2] = fDataSeq & 0xff; a[3] = fDataSeq >> 8;
            a[6] = eh[12]; a[7] = eh[13];
            mbuf_copydata(m, 14, plen - 14, a + ad);
            int err = ic->ic_awdl_tx(ic, txChan, frame, hdr + llc + ad + plen - 14);
            if (err == EAGAIN) {
                fDataTxDeferred++;
                return;             // keep it for the next dwell
            }
            if (err) {
                fDataTxErr++;
            } else {
                fDataTx++;
                fDataSeq++;
                if (!(eh[0] & 1))
                    notePeerTx(eh);
                // if_output counts the bytes; packets come from the driver stats.
                if (fController && fController->fAWDLBsdIf && fController->fAWDLBsdIf->getIfnet())
                    ifnet_stat_increment_out(fController->fAWDLBsdIf->getIfnet(), 1, 0, 0);
            }
        }
        mbuf_freem(m);
        fDataQ[fDataQHead] = nullptr;
        fDataQHead = (fDataQHead + 1) % kDataQueueLen;
        fDataQCount--;
    }
}

void AirportItlwmAWDLInterface::
publishDataState()
{
    setProperty("AWDLDataRx", fDataRx, 64);
    setProperty("AWDLDataRxBad", fDataRxBad, 64);
    setProperty("AWDLDataRxAmsdu", fDataRxAmsdu, 64);
    setProperty("AWDLDataRxNoBuf", fDataRxNoBuf, 64);
    setProperty("AWDLDataRxLastLen", fDataRxLastLen, 32);
    setProperty("AWDLDataRxLastType", fDataRxLastType, 32);
    setProperty("AWDLDataRxBadPullup", fDataRxBadPullup, 64);
    setProperty("AWDLDataRxBadShort", fDataRxBadShort, 64);
    setProperty("AWDLDataRxBadSnap", fDataRxBadSnap, 64);
    setProperty("AWDLDataRxNull", fDataRxNull, 64);
    setProperty("AWDLDataRxBadLastLen", fDataRxBadLastLen, 32);
    setProperty("AWDLDataRxBadLastFc", fDataRxBadLastFc, 32);
    setProperty("AWDLDataRxBadLastHdr", fDataRxBadLastHdr, 32);
    setProperty("AWDLDataRxBadLastHead", fDataRxBadLastHead, 64);
    setProperty("AWDLDataRxBadLastQos", fDataRxBadLastQos, 32);
    setProperty("AWDLDataRxBadLastPay0", fDataRxBadLastRaw[0], 64);
    setProperty("AWDLDataRxBadLastPay1", fDataRxBadLastRaw[1], 64);
    setProperty("AWDLDataRxBadLastPay2", fDataRxBadLastRaw[2], 64);
    setProperty("AWDLDataRxBadLastPay3", fDataRxBadLastRaw[3], 64);
    setProperty("AWDLDataRxBadOver2304", fDataRxBadBig, 64);
    setProperty("AWDLDataRxSplitFrames", fDataRxSplitFrames, 64);
    setProperty("AWDLDataRxSplitSub", fDataRxSplitSub, 64);
    setProperty("AWDLDataRxSplitAddrDiff", fDataRxSplitAddrDiff, 64);
    setProperty("AWDLDataRxLenLt128", fDataRxLen[0], 64);
    setProperty("AWDLDataRxLenLt512", fDataRxLen[1], 64);
    setProperty("AWDLDataRxLenLt1024", fDataRxLen[2], 64);
    setProperty("AWDLDataRxLenGe1024", fDataRxLen[3], 64);
    setProperty("AWDLDataRxMaxLen", fDataRxMaxLen, 32);
    setProperty("AWDLDataTx", fDataTx, 64);
    setProperty("AWDLDataTxQueued", fDataTxQueued, 64);
    setProperty("AWDLDataTxDropFull", fDataTxDropFull, 64);
    setProperty("AWDLDataTxDeferred", fDataTxDeferred, 64);
    setProperty("AWDLDataTxErr", fDataTxErr, 64);
    setProperty("AWDLDataTxPending", fDataQCount, 32);
    setProperty("AWDLDataTxNotCommon", fDataTxNotCommon, 64);
    setProperty("AWDLListenOwn", fListenOwn, 64);
    setProperty("AWDLListenPeerSlots", fListenPeerSlots, 64);
}

void AirportItlwmAWDLInterface::
publishTxState()
{
    setProperty("AWDLTxPSF", fTxPSF, 64);
    setProperty("AWDLTxMI", fTxMI, 64);
    setProperty("AWDLTxOOBSent", fTxOOBSent, 64);
    setProperty("AWDLTxRefused", fTxRefused, 64);
    setProperty("AWDLTxSkipMode", fTxSkipMode, 64);
    setProperty("AWDLTxLastErr", fTxLastErr, 32);
    setProperty("AWDLTxLastLen", fTxLastLen, 32);
    ieee80211com *ic = fController ? fController->fHalService->get80211Controller() : nullptr;
    if (!ic)
        return;
    setProperty("AWDLTxFwSent", ic->ic_awdl_tx_sent, 32);
    setProperty("AWDLTxFwHome", ic->ic_awdl_tx_home, 32);
    setProperty("AWDLTxFwBusy", ic->ic_awdl_tx_busy, 32);
    setProperty("AWDLTxFwErr", ic->ic_awdl_tx_err, 32);
    setProperty("AWDLTxFwOk", ic->ic_awdl_tx_ok, 32);
    setProperty("AWDLTxFwFail", ic->ic_awdl_tx_fail, 32);
    setProperty("AWDLTxFwLastStatus", ic->ic_awdl_tx_last_status, 32);
    setProperty("AWDLRxUcAction", ic->ic_awdl_rx_uc_action, 32);
    setProperty("AWDLRxUcData", ic->ic_awdl_rx_uc_data, 32);
    setProperty("AWDLRxUcOther", ic->ic_awdl_rx_uc_other, 32);
    setProperty("AWDLRxMcData", ic->ic_awdl_rx_mc_data, 32);
    setProperty("AWDLRxUcLastChannel", ic->ic_awdl_rx_uc_last_chan, 32);
    setProperty("AWDLFwUcMgmt", ic->ic_awdl_fw_uc_mgmt, 32);
    setProperty("AWDLFwUcData", ic->ic_awdl_fw_uc_data, 32);
    setProperty("AWDLFwUcBadCrc", ic->ic_awdl_fw_uc_badcrc, 32);
    setProperty("AWDLFwUcDropDecrypt", ic->ic_awdl_fw_uc_drop_decrypt, 32);
    setProperty("AWDLFwUcDropDup", ic->ic_awdl_fw_uc_drop_dup, 32);
    setProperty("AWDLFwUcLastChannel", ic->ic_awdl_fw_uc_last_chan, 32);
    setProperty("AWDLFwUcLastFc", ic->ic_awdl_fw_uc_last_fc, 32);
    setProperty("AWDLFwForeignData", ic->ic_awdl_fw_foreign_data, 32);
    setProperty("AWDLPromisc", ic->ic_awdl_promisc ? true : false);
    setProperty("AWDLPromiscChanges", ic->ic_awdl_promisc_changes, 32);
    // Which net80211 drop path feeds the station's input errors.
    char st[256];
    snprintf(st, sizeof(st), "ccmp_dec %u unenc %u wepfail %u wrongdir %u wrongbss %u tooshort %u notassoc %u dup %u decap %u ctl %u",
             ic->ic_stats.is_ccmp_dec_errs, ic->ic_stats.is_rx_unencrypted, ic->ic_stats.is_rx_wepfail,
             ic->ic_stats.is_rx_wrongdir, ic->ic_stats.is_rx_wrongbss, ic->ic_stats.is_rx_tooshort,
             ic->ic_stats.is_rx_notassoc, ic->ic_stats.is_rx_dup, ic->ic_stats.is_rx_decap, ic->ic_stats.is_rx_ctl);
    setProperty("NetRxDropStats", st);
    setProperty("AWDLPeerActQueries", fPeerActQueries, 64);
    snprintf(st, sizeof(st), "bss_enc %u bss_mic %u other %u last_status 0x%08x last_src %02x:%02x:%02x:%02x:%02x:%02x",
             ic->ic_dbg_dec_bss_enc, ic->ic_dbg_dec_bss_mic, ic->ic_dbg_dec_other, ic->ic_dbg_dec_last_status,
             ic->ic_dbg_dec_last_src[0], ic->ic_dbg_dec_last_src[1], ic->ic_dbg_dec_last_src[2],
             ic->ic_dbg_dec_last_src[3], ic->ic_dbg_dec_last_src[4], ic->ic_dbg_dec_last_src[5]);
    setProperty("NetRxDecDiag", st);
    {
        int o = snprintf(st, sizeof(st), "data %u mgmt %u awdl %u last 0x%04x tries %u rate 0x%08x hist",
                         ic->ic_dbg_tx_fail_data, ic->ic_dbg_tx_fail_mgmt, ic->ic_dbg_tx_fail_awdl,
                         ic->ic_dbg_tx_fail_last_status, ic->ic_dbg_tx_fail_last_tries, ic->ic_dbg_tx_fail_last_rate);
        for (int s = 0; s < 256 && o > 0 && o < (int)sizeof(st) - 16; s++)
            if (ic->ic_dbg_tx_fail_hist[s])
                o += snprintf(st + o, sizeof(st) - o, " %02x:%u", s, ic->ic_dbg_tx_fail_hist[s]);
        setProperty("NetTxFailStats", st);
    }
    setProperty("AWDLP2PAddrChanges", ic->ic_awdl_p2p_addr_changes, 32);
    if (ic->ic_awdl_rx_uc_action + ic->ic_awdl_rx_uc_data + ic->ic_awdl_rx_uc_other) {
        char src[24];
        snprintf(src, sizeof(src), "%s", ether_sprintf(ic->ic_awdl_rx_uc_last_src));
        setProperty("AWDLRxUcLastSrc", src);
    }
    char me[24];
    snprintf(me, sizeof(me), "%s", ether_sprintf(ic->ic_awdl_addr));
    setProperty("AWDLRadioAddr", me);
    if (ic->ic_awdl_bssid_valid) {
        char bs[24];
        snprintf(bs, sizeof(bs), "%s", ether_sprintf(ic->ic_awdl_bssid));
        setProperty("AWDLRadioBssid", bs);
    }
}

void AirportItlwmAWDLInterface::
publishSyncState()
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%s", ether_sprintf(fSyncMaster.octet));
    setProperty("AWDLSyncMaster", buf);
    setProperty("AWDLSyncMode", fSyncMode, 32);
    setProperty("AWDLAWRunning", fAWRunning);
    setProperty("AWDLAWStarts", fAWStarts, 64);
    setProperty("AWDLAWSeq", fLastSeq, 32);
    setProperty("AWDLSyncUpdates", fSyncUpdates, 64);
    setProperty("AWDLSyncErrUs", (uint64_t)fLastSyncErrUs, 32);
    setProperty("AWDLPresenceMode", presenceMode(), 32);
}

// Close the slot that just ended (data moved during it, any AWDL RX) and
// classify the one starting: c = common channel, p = only the peer is on a
// channel (Home: our infra channel, where we stay in idle slots), own = only
// us, unk = no peer timeline yet.
void AirportItlwmAWDLInterface::
notePairSlot(uint8_t own, uint64_t slotStartUs)
{
    uint64_t slot = slotUs();
    if (fPairOpen && slotStartUs > fPairStartUs && slotStartUs - fPairStartUs <= 3 * slot) {
        uint64_t rx = fDataRx - fPairRx0, tx = fDataTx - fPairTx0, af = fRxAF - fPairAF0;
        fPairRx[fPairCur] += rx;
        fPairTx[fPairCur] += tx;
        if (rx || af)
            fPairHeard[fPairCur]++;
    }
    unsigned home = currentHomeChannel();
    unsigned peer = (fPeerValid && fPeerChanValid) ? peerChannelAt(slotStartUs + slot / 2) : 0;
    uint8_t cls;
    if (!fPeerValid || !fPeerChanValid)
        cls = kPairUnknown;
    else if (own && own == peer)
        cls = own == home ? kPairCommonHome : kPairCommonOff;
    else if (!own && peer && peer == home)
        cls = kPairPeerOnHome;
    else if (peer)
        cls = kPairPeerElsewhere;
    else if (own)
        cls = kPairOwnOnly;
    else
        cls = kPairIdle;
    fPairSlots[cls]++;
    fPairCur = cls;
    fPairOpen = true;
    fPairStartUs = slotStartUs;
    fPairRx0 = fDataRx;
    fPairTx0 = fDataTx;
    fPairAF0 = fRxAF;
    if (fDataQCount > fPairQMax)
        fPairQMax = fDataQCount;
    if ((++fPairTick & 0xf) == 0) {
        publishPairState();
        reportDataPathStats();
    }
}

void AirportItlwmAWDLInterface::
publishPairState()
{
    static const char *const names[kPairClasses] = { "cOff", "cHome", "pHome", "pElse", "own", "idle", "unk" };
    char buf[640];
    size_t o = 0;
    for (unsigned c = 0; c < kPairClasses && o < sizeof(buf); c++)
        o += snprintf(buf + o, sizeof(buf) - o, "%s%s=%llu/%llu/%llu/%llu", c ? " " : "", names[c],
                      (unsigned long long)fPairSlots[c], (unsigned long long)fPairHeard[c],
                      (unsigned long long)fPairRx[c], (unsigned long long)fPairTx[c]);
    if (o < sizeof(buf))
        snprintf(buf + o, sizeof(buf) - o, " qmax=%u dps=%llu/0x%x", fPairQMax,
                 (unsigned long long)fDPStatsCalls, (unsigned)fDPStatsLastRet);
    setProperty("AWDLPairStats", buf);
    fPairQMax = 0;
}

/*
 * IO80211AWDLPeerManager::monitorAWDLDataMode (about 1 Hz) moves the Mac to the
 * high-throughput AWDL sequence only when its peer-manager Tx/Rx counters move.
 * The Family's own peer-manager packet path fills them (this driver does not use
 * it), or updateInterfaceDataStats through IO80211PeerManager::updateLocalCounters.
 * Push our cumulative AWDL data frame counts through the latter.
 */
void AirportItlwmAWDLInterface::
reportDataPathStats()
{
    if (itlwm_awdl_dpstats <= 0)
        return;
    uint64_t tx = fDataTx, rx = fDataRx;
    if (fDPStatsCalls && tx == fDPStatsLastTx && rx == fDPStatsLastRx)
        return;
    memset(&fDPStats, 0, sizeof(fDPStats));
    fDPStats.txPackets = tx;
    fDPStats.rxPackets = rx;
    IOReturn ret = IO80211SkywalkInterface_updateInterfaceDataStats(this, &fDPStats);
    fDPStatsCalls++;
    fDPStatsLastRet = ret;
    fDPStatsLastTx = tx;
    fDPStatsLastRx = rx;
    if (fDPStatsCalls <= 2 || (ret != kIOReturnSuccess && (fDPStatsCalls & 0xff) == 1))
        IOLog("AirportItlwm: AWDL dp stats #%llu ret=0x%x tx=%llu rx=%llu\n",
              (unsigned long long)fDPStatsCalls, ret, (unsigned long long)tx, (unsigned long long)rx);
}

#pragma mark - creation, mirrors AppleBCMWLANCore::createProxInterface

IOReturn AirportItlwmSkywalkInterface::
setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data *data)
{
    AirportItlwm *controller = OSDynamicCast(AirportItlwm, getController());
    if (!controller || !data)
        return kIOReturnBadArgument;
    return controller->createAWDLInterface(data);
}

IOReturn AirportItlwm::
createAWDLInterface(apple80211_virt_if_create_data *data)
{
    XYLog("AWDL %s: role %u enabled %d\n", __FUNCTION__, data->role, fAWDLEnabled);
    if (!fAWDLEnabled || data->role != kAirportItlwmRoleAWDL)
        return kIOReturnUnsupported;
    if (fAWDLIf) {
        strlcpy((char *)data->bsd_name, fAWDLIf->getBSDName() ?: "awdl0", sizeof(data->bsd_name));
        return kIOReturnSuccess;
    }

    ether_addr mac;
    memcpy(mac.octet, data->mac, sizeof(mac.octet));
    AirportItlwmAWDLInterface *awdl = AirportItlwmAWDLInterface::withController(this, &mac);
    if (!awdl)
        return kIOReturnNoMemory;
    if (!awdl->attach(this)) {
        XYLog("AWDL %s: attach failed\n", __FUNCTION__);
        awdl->release();
        return kIOReturnError;
    }

    // Same registration sequence the station interface uses in start().
    IOSkywalkEthernetInterface::RegistrationInfo info;
    memset(&info, 0, sizeof(info));
    if (!awdl->initRegistrationInfo(&info, 1, sizeof(info))) {
        XYLog("AWDL %s: initRegistrationInfo failed\n", __FUNCTION__);
        awdl->detach(this);
        awdl->release();
        return kIOReturnError;
    }
    awdl->mExpansionData->fRegistrationInfo = (IOSkywalkNetworkInterface::RegistrationInfo *)IOMalloc(sizeof(IOSkywalkNetworkInterface::RegistrationInfo));
    awdl->mExpansionData2->fRegistrationInfo = (IOSkywalkEthernetInterface::RegistrationInfo *)IOMalloc(sizeof(IOSkywalkEthernetInterface::RegistrationInfo));
    if (!awdl->mExpansionData->fRegistrationInfo || !awdl->mExpansionData2->fRegistrationInfo) {
        awdl->detach(this);
        awdl->release();
        return kIOReturnNoMemory;
    }
    memcpy(awdl->mExpansionData->fRegistrationInfo, &info, sizeof(info));
    memcpy(awdl->mExpansionData2->fRegistrationInfo, &info, sizeof(info));
    awdl->deferBSDAttach(true);
    if (!awdl->start(this)) {
        XYLog("AWDL %s: start failed\n", __FUNCTION__);
        awdl->detach(this);
        awdl->release();
        return kIOReturnError;
    }
    fAWDLIf = awdl;

    // The BSD ifnet comes from a dedicated IOEthernetInterface, as for the station.
    IONetworkInterface *bsd = nullptr;
    fAWDLMac = mac;
    fCreatingAWDL = true;
    bool ok = IONetworkController::attachInterface(&bsd, false);
    fCreatingAWDL = false;
    if (!ok || !bsd) {
        XYLog("AWDL %s: BSD interface attach failed\n", __FUNCTION__);
        destroyAWDLInterface();
        return kIOReturnError;
    }
    fAWDLBsdIf = OSDynamicCast(AirportItlwmAWDLEthernetInterface, bsd);
    bsd->setProperty("HiddenInterface", kOSBooleanTrue);
    bsd->setProperty("HiddenConfiguration", kOSBooleanTrue);
    bsd->registerService();

    // The unit is assigned asynchronously by the network stack; awdl0 is the only
    // awdl interface on this machine, so report the name it will get.
    strlcpy((char *)data->bsd_name, "awdl0", sizeof(data->bsd_name));
    XYLog("AWDL %s: created %s\n", __FUNCTION__, data->bsd_name);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setAWDLLinkGated(OSObject *owner, void *arg0, void *, void *, void *)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, owner);
    if (!that || !that->fAWDLIf)
        return kIOReturnNotReady;
    // Same call as AppleBCMWLANProximityInterface::bringupLink.
    that->fAWDLIf->setLinkState(arg0 ? kIO80211NetworkLinkUp : kIO80211NetworkLinkDown, 1);
    // The IOKit path that normally sets IFF_RUNNING is bypassed in performCommand.
    // Without it every send on awdl0 fails with ENETDOWN before reaching the driver.
    if (awDataAllowed() && that->fAWDLBsdIf && that->fAWDLBsdIf->getIfnet())
        ifnet_set_flags(that->fAWDLBsdIf->getIfnet(), arg0 ? IFF_RUNNING : 0, IFF_RUNNING);
    return kIOReturnSuccess;
}

void AirportItlwm::
destroyAWDLInterface()
{
    if (fAWDLBsdIf) {
        XYLog("AWDL %s: dropped %llu packets\n", __FUNCTION__, fAWDLBsdIf->droppedPackets());
        detachInterface(fAWDLBsdIf, true);
        fAWDLBsdIf->release();
        fAWDLBsdIf = nullptr;
    }
    if (fAWDLIf) {
        fAWDLIf->destroyLLWInterface();
        fAWDLIf->teardownAW();
        fAWDLIf->stop(this);
        fAWDLIf->detach(this);
        fAWDLIf->release();
        fAWDLIf = nullptr;
    }
}

// Log a setter that is accepted but not acted on yet.
#define AWDL_SET_STUB(NAME, TYPE)                       \
IOReturn AirportItlwmAWDLInterface::set##NAME(TYPE *d)  \
{                                                       \
    fCallCount++;                                       \
    AWDL_LOG("%p", d);                                  \
    return kIOReturnSuccess;                            \
}

// Getter without a known layout: tell the caller we have nothing.
#define AWDL_GET_STUB(NAME, TYPE)                       \
IOReturn AirportItlwmAWDLInterface::get##NAME(TYPE *d)  \
{                                                       \
    AWDL_LOG("unsupported");                            \
    return kIOReturnUnsupported;                        \
}

AirportItlwmAWDLInterface *AirportItlwmAWDLInterface::
withController(AirportItlwm *controller, ether_addr *mac)
{
    AirportItlwmAWDLInterface *ifp = new AirportItlwmAWDLInterface;
    if (!ifp)
        return nullptr;
    if (!ifp->init(controller, mac, kAirportItlwmRoleAWDL, "awdl")) {
        ifp->release();
        return nullptr;
    }
    ifp->fController = controller;
    // Runs on the controller work loop, like the rx path and the ioctls, so the
    // timeline state needs no extra locking.
    if (!awTimelineAllowed())
        return ifp;
    ifp->fAWTimer = IOTimerEventSource::timerEventSource(kIOTimerEventSourceOptionsPriorityHigh,
                                                         ifp, &AirportItlwmAWDLInterface::awTimerFired);
    if (!ifp->fAWTimer || !controller->getWorkLoop() ||
        controller->getWorkLoop()->addEventSource(ifp->fAWTimer) != kIOReturnSuccess) {
        AWDL_LOG("AW timer setup failed");
        OSSafeReleaseNULL(ifp->fAWTimer);
    }
    ifp->fPeerCacheLock = IOSimpleLockAlloc();
    ifp->fPeerCacheTimer = IOTimerEventSource::timerEventSource(kIOTimerEventSourceOptionsPriorityHigh,
                                                                ifp, &AirportItlwmAWDLInterface::peerCacheTimerFired);
    if (!ifp->fPeerCacheLock || !ifp->fPeerCacheTimer || !controller->getWorkLoop() ||
        controller->getWorkLoop()->addEventSource(ifp->fPeerCacheTimer) != kIOReturnSuccess) {
        AWDL_LOG("peer cache timer setup failed");
        OSSafeReleaseNULL(ifp->fPeerCacheTimer);
    }
    if (awListenAllowed()) {
        ifp->fListenTimer = IOTimerEventSource::timerEventSource(kIOTimerEventSourceOptionsPriorityHigh,
                                                                 ifp, &AirportItlwmAWDLInterface::listenTimerFired);
        if (!ifp->fListenTimer || !controller->getWorkLoop() ||
            controller->getWorkLoop()->addEventSource(ifp->fListenTimer) != kIOReturnSuccess) {
            AWDL_LOG("listen timer setup failed");
            OSSafeReleaseNULL(ifp->fListenTimer);
        }
    }
    if (awListenAllowed() && awTxAllowed()) {
        ifp->fTxTimer = IOTimerEventSource::timerEventSource(kIOTimerEventSourceOptionsPriorityHigh,
                                                             ifp, &AirportItlwmAWDLInterface::txTimerFired);
        if (!ifp->fTxTimer || !controller->getWorkLoop() ||
            controller->getWorkLoop()->addEventSource(ifp->fTxTimer) != kIOReturnSuccess) {
            AWDL_LOG("tx timer setup failed");
            OSSafeReleaseNULL(ifp->fTxTimer);
        }
    }
    return ifp;
}

bool AirportItlwmAWDLInterface::
init(IO80211Controller *controller, ether_addr *mac, unsigned int role, char const *prefix)
{
    if (!IO80211AWDLProtocol::init(controller, mac, role, prefix)) {
        AWDL_LOG("super::init failed");
        return false;
    }
    fController = nullptr;
    memcpy(&fMac, mac, sizeof(fMac));
    memset(&fSyncParams, 0, sizeof(fSyncParams));
    fSyncEnabled = false;
    fSyncState = 0;
    fCallCount = 0;
    fLLWIf = nullptr;
    fLLWWant = false;
    fLLWScheduled = false;
    fLLWDying = false;
    fLLWLock = IOLockAlloc();
    fLLWCall = thread_call_allocate([](thread_call_param_t p0, thread_call_param_t) {
        static_cast<AirportItlwmAWDLInterface *>(p0)->llwWork();
    }, this);
    fRxAF = 0;
    fRxAFDropped = 0;
    fAWTimer = nullptr;
    fAWRunning = false;
    fRefUs = 0;
    fRefSeq = 0;
    fLastValid = false;
    fLastStartUs = 0;
    fLastSeq = 0;
    fAWStarts = 0;
    fSyncUpdates = 0;
    fLastSyncErrUs = 0;
    memset(&fSyncMaster, 0, sizeof(fSyncMaster));
    fSyncMode = 0;
    fPresenceMode = 0;
    memset(fChanSeq, 0, sizeof(fChanSeq));
    fChanSeqValid = false;
    fInAW = false;
    fTsfOffset = 0;
    fTsfValid = false;
    fSyncOutliers = 0;
    fSyncOutliersTotal = 0;
    fListenSlot = 0;
    fListenStarted = 0;
    fListenBusy = 0;
    fListenSkippedHome = 0;
    fListenInfraHold = 0;
    fListenInfraSlot = 0;
    fListenParkChan = 0;
    fListenParked = 0;
    fHomeListenNotBeforeUs = 0;
    fHomeListens = 0;
    fListenNextHome = false;
    fListenDwellTU = kListenDwellTU;
    fListenEveryNth = kListenEveryNthSlot;
    fListenTimer = nullptr;
    fListenAlign = 1;
    fListenAlignedActive = false;
    fListenNextChan = 0;
    fListenNextDwellTU = 0;
    fListenAligned = 0;
    fListenNoSlot = 0;
    fPeerValid = false;
    fPeerSeenUs = 0;
    fPeerRefUs = 0;
    fPeerRefSeq = 0;
    fPeerPm = 0;
    fPeerSlotUs = 0;
    fPeerChanValid = false;
    fPeerUpdates = 0;
    fPeerPredHit = 0;
    fPeerPredMiss = 0;
    fPeerLastErrUs = 0;
    fRxAFChan6 = 0;
    fRxAFChan44 = 0;
    fRxAFChan149 = 0;
    fRxAFChanOther = 0;
    fRxAFChan161 = 0;
    memset(fListenOnChan, 0, sizeof(fListenOnChan));
    fSyncTmplLen = 0;
    fSyncTmplSets = 0;
    fMITmplLen = 0;
    fOOBReqLen = 0;
    fOOBReqPending = false;
    fTxOOBSent = 0;
    fOOBSets = 0;
    fOOBAutoSets = 0;
    fAfTxMode = 0;
    fTxTimer = nullptr;
    fPeerCacheTimer = nullptr;
    fPeerCacheLock = nullptr;
    bzero(fPeerRx, sizeof(fPeerRx));
    fPeerActQueries = 0;
    fPeerCacheQCount = 0;
    fPeerCacheDone = 0;
    fPeerCacheDrop = 0;
    fTxChan = 0;
    fTxPhase = 0;
    fTxSlotUs = 0;
    fTxSeq = 0;
    fMasterChannel = 6;
    fSecondaryMasterChannel = 149;
    fElectionId = 0;
    fElectionMetric = 65;
    memset(&fElectionRssiThresholds, 0, sizeof(fElectionRssiThresholds));
    memset(&fMinRate, 0, sizeof(fMinRate));
    memset(&fExtStateMachineParams, 0, sizeof(fExtStateMachineParams));
    fEncryptionKeySets = 0;
    fPeerTrafficActive = 0;
    fPeerTrafficRegs = 0;
    fTxPSF = 0;
    fTxMI = 0;
    fTxRefused = 0;
    fTxSkipMode = 0;
    fTxLastErr = 0;
    fTxLastLen = 0;
    fDataRx = 0;
    fDataRxBad = 0;
    fDataRxAmsdu = 0;
    fDataRxNoBuf = 0;
    fDataRxLastLen = 0;
    fDataRxLastType = 0;
    fDataRxBadPullup = fDataRxBadShort = fDataRxBadSnap = fDataRxNull = 0;
    fDataRxBadLastLen = 0;
    fDataRxBadLastFc = fDataRxBadLastHdr = 0;
    fDataRxBadLastHead = 0;
    fDataRxBadLastQos = 0;
    memset(fDataRxBadLastRaw, 0, sizeof(fDataRxBadLastRaw));
    fDataRxBadBig = 0;
    fDataRxSplitFrames = fDataRxSplitSub = fDataRxSplitAddrDiff = 0;
    memset(fDataRxLen, 0, sizeof(fDataRxLen));
    fDataRxMaxLen = 0;
    memset(fDataQ, 0, sizeof(fDataQ));
    fDataQHead = 0;
    fDataQCount = 0;
    fDataSeq = 0;
    fDataTx = 0;
    fDataTxQueued = 0;
    fDataTxDropFull = 0;
    fDataTxDeferred = 0;
    fDataTxErr = 0;
    fListenOwn = 0;
    fListenPeerSlots = 0;
    memset(fRxGap, 0, sizeof(fRxGap));
    fBssidSets = 0;
    fPeerCacheOps = 0;
    fDataTxNotCommon = 0;
    memset(fPairSlots, 0, sizeof(fPairSlots));
    memset(fPairHeard, 0, sizeof(fPairHeard));
    memset(fPairRx, 0, sizeof(fPairRx));
    memset(fPairTx, 0, sizeof(fPairTx));
    fPairOpen = false;
    fPairQMax = 0;
    fPairTick = 0;
    fDPStatsCalls = 0;
    fDPStatsLastTx = fDPStatsLastRx = 0;
    fDPStatsLastRet = kIOReturnSuccess;
    AWDL_LOG("role %u prefix %s mac %s", role, prefix, ether_sprintf(mac->octet));
    return true;
}

void AirportItlwmAWDLInterface::
free()
{
    AWDL_LOG("calls %llu", fCallCount);
    teardownAW();
    destroyLLWInterface();
    if (fLLWCall) {
        thread_call_free(fLLWCall); // may report busy if the worker is still unwinding; harmless
        fLLWCall = nullptr;
    }
    if (fLLWLock) {
        IOLockFree(fLLWLock);
        fLLWLock = nullptr;
    }
    if (fPeerCacheLock) {
        IOSimpleLockFree(fPeerCacheLock);
        fPeerCacheLock = nullptr;
    }
    IO80211AWDLProtocol::free();
}

IOReturn AirportItlwmAWDLInterface::
setMacAddress(ether_addr &mac)
{
    memcpy(&fMac, &mac, sizeof(fMac));
    AWDL_LOG("%s", ether_sprintf(mac.octet));
    publishAWDLAddress();
    return kIOReturnSuccess;
}

// Give the radio awdl0's address (P2P device MAC, unicast rx accounting).
void AirportItlwmAWDLInterface::
publishAWDLAddress()
{
    if ((!awTxAllowed() && !awListenAllowed() && !awDataAllowed()) || !fController)
        return;
    ieee80211com *ic = fController->fHalService->get80211Controller();
    if (!ic)
        return;
    if (ic->ic_awdl_addr_gen && !memcmp(ic->ic_awdl_addr, &fMac, sizeof(fMac)))
        return;
    memcpy(ic->ic_awdl_addr, &fMac, sizeof(fMac));
    ic->ic_awdl_addr_gen++;
    notifyHalSync();
}

#pragma mark - getters with cached state

IOReturn AirportItlwmAWDLInterface::
getSSID(apple80211_ssid_data *d)
{
    memset(d, 0, sizeof(*d));
    d->version = APPLE80211_VERSION;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
getCHANNEL(apple80211_channel_data *d)
{
    // Social channel 6 until channel sequencing is implemented.
    memset(d, 0, sizeof(*d));
    d->version = APPLE80211_VERSION;
    d->channel.version = APPLE80211_VERSION;
    d->channel.channel = 6;
    d->channel.flags = APPLE80211_C_FLAG_2GHZ | APPLE80211_C_FLAG_20MHZ;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
getBSSID(apple80211_bssid_data *d)
{
    memset(d, 0, sizeof(*d));
    d->version = APPLE80211_VERSION;
    memcpy(&d->bssid, &fMac, sizeof(fMac));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
getSTATE(apple80211_state_data *d)
{
    memset(d, 0, sizeof(*d));
    d->version = APPLE80211_VERSION;
    d->state = APPLE80211_S_INIT;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
getAWDL_SYNC_PARAMS(apple80211_awdl_sync_params *d)
{
    memcpy(d, &fSyncParams, sizeof(*d));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
getAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled *d)
{
    memset(d, 0, sizeof(*d));
    d->version = APPLE80211_VERSION;
    d->enabled = fSyncEnabled;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
getAWDL_SYNC_STATE(apple80211_awdl_sync_state *d)
{
    memset(d, 0, sizeof(*d));
    d->version = APPLE80211_VERSION;
    d->state = fSyncState;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
getPEER_CACHE_MAXIMUM_SIZE(apple80211_peer_cache_maximum_size *d)
{
    memset(d, 0, sizeof(*d));
    d->version = APPLE80211_VERSION;
    d->max_peers = 32;
    return kIOReturnSuccess;
}

#pragma mark - setters with cached state

IOReturn AirportItlwmAWDLInterface::
setAWDL_SYNC_PARAMS(apple80211_awdl_sync_params *d)
{
    fCallCount++;
    memcpy(&fSyncParams, d, sizeof(fSyncParams));
    AWDL_LOG("aw_len %u aw_period %u ext_len %u sync_frame_period %u",
             d->availability_window_length, d->availability_window_period,
             d->extension_length, d->synchronization_frame_period);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
setAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled *d)
{
    fCallCount++;
    fSyncEnabled = d->enabled != 0;
    AWDL_LOG("enabled %u", d->enabled);
    if (!awTimelineAllowed())
        return kIOReturnSuccess;
    setProperty("AWDLSyncEnabled", fSyncEnabled);
    if (fSyncEnabled)
        startAWTimeline();
    else
        stopAWTimeline();
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
setAWDL_SYNC_STATE(apple80211_awdl_sync_state *d)
{
    fCallCount++;
    // IO80211AWDLPeerManager::programChipWithNewMaster: +4 mode, +8 the master
    // to follow (our own address when we are master), +16 op mode, +20 flags.
    const uint8_t *raw = (const uint8_t *)d;
    fSyncState = d->state;
    AWDL_LOG("state %u", d->state);
    if (!awTimelineAllowed())
        return kIOReturnSuccess;
    memcpy(&fSyncMaster, raw + 8, sizeof(fSyncMaster));
    fSyncMode = *(const uint32_t *)(raw + 16);
    AWDL_LOG("state %u master %s mode %u flags 0x%x", d->state, ether_sprintf(fSyncMaster.octet),
             fSyncMode, raw[20]);
    publishSyncState();
    return kIOReturnSuccess;
}

// +4 BSSID (Broadcom stores it at ivars +608 and creates its firmware AWDL
// interface with it, AppleBCMWLANProximityInterface::setAWDL_BSSID); all zero
// tears the interface down. +10: a second address, logged only.
IOReturn AirportItlwmAWDLInterface::
setAWDL_BSSID(apple80211_awdl_bssid *d)
{
    fCallCount++;
    AWDL_LOG("bssid %s", ether_sprintf(d->bssid));
    if (!awTimelineAllowed())
        return kIOReturnSuccess;
    char b[48];
    snprintf(b, sizeof(b), "%s", ether_sprintf(d->bssid));
    size_t n = strlen(b);
    snprintf(b + n, sizeof(b) - n, " %s", ether_sprintf(d->unk_mac));
    setProperty("AWDLBssid", b);
    fBssidSets++;
    setProperty("AWDLBssidSets", fBssidSets, 64);
    if (!awDataAllowed() || !fController)
        return kIOReturnSuccess;
    ieee80211com *ic = fController->fHalService->get80211Controller();
    if (!ic)
        return kIOReturnSuccess;
    static const uint8_t zero[6] = {};
    bool valid = memcmp(d->bssid, zero, 6) != 0;
    if (valid == (ic->ic_awdl_bssid_valid != 0) && (!valid || !memcmp(ic->ic_awdl_bssid, d->bssid, 6)))
        return kIOReturnSuccess;
    memcpy(ic->ic_awdl_bssid, d->bssid, 6);
    ic->ic_awdl_bssid_valid = valid;
    ic->ic_awdl_addr_gen++;     // the P2P MAC is re-programmed before the next dwell
    notifyHalSync();
    return kIOReturnSuccess;
}

// Broadcom: awdl_peer_op puts the peer (address, HT/VHT/HE caps, its channel
// sequence) into the firmware peer cache before unicast. Layout as read by
// AppleBCMWLANProximityInterface::setPEER_CACHE_CONTROL: +4 opcode, +8 MAC,
// +16 HT IE (45, 26, ...), +44 ampdu, +45 VHT present, +46 VHT IE, +60 HE,
// +68.. channel sequence. Recorded only for now.
IOReturn AirportItlwmAWDLInterface::
setPEER_CACHE_CONTROL(apple80211_peer_cache_control *d)
{
    fCallCount++;
    if (!awTimelineAllowed() || !d)
        return kIOReturnSuccess;
    const uint8_t *r = (const uint8_t *)d;
    uint32_t op = *(const uint32_t *)(r + 4);
    fPeerCacheOps++;
    char s[96];
    snprintf(s, sizeof(s), "op %u %s ht %u/%u vht %u he %u", op, ether_sprintf((uint8_t *)r + 8),
             r[16], r[17], r[45], r[60]);
    setProperty("AWDLPeerCacheLast", s);
    setProperty("AWDLPeerCacheOps", fPeerCacheOps, 64);
    // IO80211Family sleeps (3 s) in waitForPeerManagerEvent after an add (op 0) or delete (op 2)
    // until firmware reports completion with event 51 / op+1. Without it every RemoteCamera
    // traffic registration stalls and Continuity Camera over AWDL times out. Complete from a
    // timer: the family only starts waiting after this ioctl returns.
    if ((op == 0 || op == 2) && fPeerCacheTimer && fPeerCacheLock) {
        bool queued = false;
        IOSimpleLockLock(fPeerCacheLock);
        if (fPeerCacheQCount < kPeerCacheQLen) {
            PeerCacheDone &e = fPeerCacheQ[fPeerCacheQCount++];
            e.evt = 51;
            e.op = op + 1;
            memcpy(e.mac, r + 8, 6);
            queued = true;
        } else {
            fPeerCacheDrop++;
        }
        IOSimpleLockUnlock(fPeerCacheLock);
        if (queued)
            fPeerCacheTimer->setTimeoutUS(300);
    }
    return kIOReturnSuccess;
}

void AirportItlwmAWDLInterface::
notePeerRx(const uint8_t *dst, const uint8_t *src)
{
    if (!dst || !src || !fPeerCacheLock || memcmp(dst, fMac.octet, 6) != 0)
        return;
    uint64_t now = mach_absolute_time();
    IOSimpleLockLock(fPeerCacheLock);
    PeerRxTime *slot = nullptr, *oldest = &fPeerRx[0];
    uint64_t oldestKey = oldest->rxT > oldest->txT ? oldest->rxT : oldest->txT;
    for (unsigned i = 0; i < kPeerRxN; i++) {
        if (!memcmp(fPeerRx[i].mac, src, 6)) { slot = &fPeerRx[i]; break; }
        uint64_t k = fPeerRx[i].rxT > fPeerRx[i].txT ? fPeerRx[i].rxT : fPeerRx[i].txT;
        if (k < oldestKey) { oldest = &fPeerRx[i]; oldestKey = k; }
    }
    if (!slot) {
        slot = oldest;
        bzero(slot, sizeof(*slot));
        memcpy(slot->mac, src, 6);
    }
    slot->rxT = now;
    IOSimpleLockUnlock(fPeerCacheLock);
}

void AirportItlwmAWDLInterface::
notePeerTx(const uint8_t *mac)
{
    if (!mac || !fPeerCacheLock)
        return;
    uint64_t now = mach_absolute_time();
    IOSimpleLockLock(fPeerCacheLock);
    PeerRxTime *slot = nullptr, *oldest = &fPeerRx[0];
    uint64_t oldestKey = oldest->rxT > oldest->txT ? oldest->rxT : oldest->txT;
    for (unsigned i = 0; i < kPeerRxN; i++) {
        if (!memcmp(fPeerRx[i].mac, mac, 6)) { slot = &fPeerRx[i]; break; }
        uint64_t k = fPeerRx[i].rxT > fPeerRx[i].txT ? fPeerRx[i].rxT : fPeerRx[i].txT;
        if (k < oldestKey) { oldest = &fPeerRx[i]; oldestKey = k; }
    }
    if (!slot) {
        slot = oldest;
        bzero(slot, sizeof(*slot));
        memcpy(slot->mac, mac, 6);
    }
    slot->txT = now;
    IOSimpleLockUnlock(fPeerCacheLock);
}

uint64_t AirportItlwmAWDLInterface::
getLastQueuePacketTime(ether_addr *a)
{
    uint64_t t = 0;
    if (!a || !fPeerCacheLock)
        return 0;
    IOSimpleLockLock(fPeerCacheLock);
    fPeerActQueries++;
    for (unsigned i = 0; i < kPeerRxN; i++) {
        if (fPeerRx[i].txT && !memcmp(fPeerRx[i].mac, a->octet, 6)) { t = fPeerRx[i].txT; break; }
    }
    IOSimpleLockUnlock(fPeerCacheLock);
    return t;
}

uint64_t AirportItlwmAWDLInterface::
getLastRxUnicastLinkActivityTime(ether_addr *a)
{
    uint64_t t = 0;
    if (!a || !fPeerCacheLock)
        return 0;
    IOSimpleLockLock(fPeerCacheLock);
    fPeerActQueries++;
    for (unsigned i = 0; i < kPeerRxN; i++) {
        if (fPeerRx[i].rxT && !memcmp(fPeerRx[i].mac, a->octet, 6)) { t = fPeerRx[i].rxT; break; }
    }
    IOSimpleLockUnlock(fPeerCacheLock);
    return t;
}

void AirportItlwmAWDLInterface::
peerCacheTimerFired(OSObject *owner, IOTimerEventSource *)
{
    if (auto *self = OSDynamicCast(AirportItlwmAWDLInterface, owner))
        self->flushPeerCacheDone();
}

void AirportItlwmAWDLInterface::
flushPeerCacheDone()
{
    for (;;) {
        PeerCacheDone e;
        IOSimpleLockLock(fPeerCacheLock);
        if (!fPeerCacheQCount) {
            IOSimpleLockUnlock(fPeerCacheLock);
            return;
        }
        e = fPeerCacheQ[0];
        fPeerCacheQCount--;
        memmove(&fPeerCacheQ[0], &fPeerCacheQ[1], fPeerCacheQCount * sizeof(PeerCacheDone));
        IOSimpleLockUnlock(fPeerCacheLock);
        uint8_t ind[0x1d0] = {};
        size_t indLen = sizeof(ind);
        if (e.evt == 69) {
            // apple80211_actionFrame_status (7 bytes): peer MAC at +0..5, status at +6
            // (0 = WLC_E_STATUS_SUCCESS / acked; non-zero = DID NOT ack).
            memcpy(ind, e.mac, 6);
            ind[6] = 0;
            indLen = 7;
        } else {
            // struct apple80211_peer_cache_control (0x1d0 bytes): version, opcode, peer MAC at +8.
            *(uint32_t *)(ind + 0) = 1;
            *(uint32_t *)(ind + 4) = e.op;
            memcpy(ind + 8, e.mac, 6);
        }
        IO80211SkywalkInterface_reportDataPathEvents(this, e.evt, ind, indLen, false);
        fPeerCacheDone++;
        setProperty("AWDLPeerCacheDone", fPeerCacheDone, 64);
    }
}

IOReturn AirportItlwmAWDLInterface::
setAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode *d)
{
    fCallCount++;
    fPresenceMode = d->mode;
    AWDL_LOG("mode %u", d->mode);
    return kIOReturnSuccess;
}

#pragma mark - stage 3: what the system asks us to transmit (recorded only)

// Payload pointers come from IO80211AWDLPeerManager's own buffers
// (updateSyncFrameTemplate: 512 bytes on its stack; buildMasterIndicationTemplate:
// 0x4b0 bytes in its ivars). Only copy from kernel addresses.
static bool awdlKernelPtr(const void *p)
{
    return (uintptr_t)p >= 0xffffff8000000000ULL;
}

// "type:len type:len ..." of a TLV buffer, to check which TLVs the system
// supplies and which the firmware is expected to add.
static void awdlTLVList(const uint8_t *b, size_t len, char *out, size_t outLen)
{
    size_t off = 0, n = 0;
    out[0] = 0;
    while (off + 3 <= len && n + 12 < outLen) {
        uint16_t tlen = b[off + 1] | (b[off + 2] << 8);
        n += snprintf(out + n, outLen - n, "%s%u:%u", n ? " " : "", b[off], tlen);
        off += 3 + tlen;
    }
    if (off != len && n + 12 < outLen)
        snprintf(out + n, outLen - n, " !%zu/%zu", off, len);
}

static void awdlHex(const uint8_t *b, size_t len, char *out, size_t outLen)
{
    size_t n = 0;
    out[0] = 0;
    for (size_t i = 0; i < len && n + 3 < outLen; i++)
        n += snprintf(out + n, outLen - n, "%02x", b[i]);
}

// ioctl 151, from IO80211AWDLPeerManager::updateSyncFrameTemplate: +4 length,
// +8 pointer. TLVs only (service params, election v2, data path state, ...);
// Broadcom hands it to the firmware ("awdl_payload"), which adds the AWDL
// header, the sync parameters and the channel sequence when it sends a PSF.
IOReturn AirportItlwmAWDLInterface::
setAWDL_SYNC_FRAME_TEMPLATE(apple80211_awdl_sync_frame_template *d)
{
    fCallCount++;
    const uint8_t *raw = (const uint8_t *)d;
    uint32_t len = *(const uint32_t *)(raw + 4);
    const uint8_t *p = *(const uint8_t * const *)(raw + 8);
    AWDL_LOG("version %u len %u", d->version, len);
    if (!awTimelineAllowed() || !len || len > sizeof(fSyncTmpl) || !awdlKernelPtr(p))
        return kIOReturnSuccess;
    memcpy(fSyncTmpl, p, len);
    fSyncTmplLen = (uint16_t)len;
    fSyncTmplSets++;
    char tlvs[160];
    awdlTLVList(fSyncTmpl, len, tlvs, sizeof(tlvs));
    setProperty("AWDLTxSyncTmplLen", len, 32);
    setProperty("AWDLTxSyncTmplSets", fSyncTmplSets, 64);
    setProperty("AWDLTxSyncTmplTLVs", tlvs);
    return kIOReturnSuccess;
}

// ioctl 149 (send now; also IO80211AWDLPeerManager::sendUnicastMI) and 225
// (auto, from updatePrimaryPayloadMI). Layout from those builders and from
// AppleBCMWLANProximityInterface::setAWDL_OOB_(AUTO_)REQUEST:
// +4 destination MAC (ff.. for broadcast MI), +12 tx offset in the AW (us,
// auto), +16 MI bitmap (auto), +20 u16 (6, or 14 for UMI on the dwell
// channel), +22 6 bytes from PeerManager ivars (inferred: our address or the
// AWDL BSSID), +40 payload length, +48 payload pointer. The payload is TLVs
// (election v2, data path state, AirDrop, arpa, service params/responses).
void AirportItlwmAWDLInterface::
recordOOB(const void *req, bool autoReq)
{
    const uint8_t *raw = (const uint8_t *)req;
    uint16_t len = *(const uint16_t *)(raw + 40);
    const uint8_t *p = *(const uint8_t * const *)(raw + 48);
    char hdr[48 * 2 + 1];
    awdlHex(raw, 48, hdr, sizeof(hdr));
    setProperty(autoReq ? "AWDLTxOOBAutoHdr" : "AWDLTxOOBHdr", hdr);
    if (autoReq) {
        fOOBAutoSets++;
        setProperty("AWDLTxOOBAutoSets", fOOBAutoSets, 64);
        setProperty("AWDLTxMIBitmap", *(const uint32_t *)(raw + 16), 32);
        setProperty("AWDLTxMIOffsetUs", *(const uint32_t *)(raw + 12), 32);
    } else {
        fOOBSets++;
        setProperty("AWDLTxOOBSets", fOOBSets, 64);
    }
    if (!len || len > sizeof(fMITmpl) || !awdlKernelPtr(p))
        return;
    char tlvs[200];
    awdlTLVList(p, len, tlvs, sizeof(tlvs));
    setProperty(autoReq ? "AWDLTxOOBAutoTLVs" : "AWDLTxOOBTLVs", tlvs);
    setProperty(autoReq ? "AWDLTxOOBAutoLen" : "AWDLTxOOBLen", len, 32);
    if (autoReq) {
        memcpy(fMITmpl, p, len);
        fMITmplLen = len;
    } else if (len > 0) {
        struct OOBCopyCtx { const uint8_t *dst; const uint8_t *payload; uint16_t len; } ctx = {
            raw + 4, p, len
        };
        auto act = [](OSObject *, void *self, void *arg, void *, void *) -> IOReturn {
            auto that = static_cast<AirportItlwmAWDLInterface *>(self);
            auto c = static_cast<const OOBCopyCtx *>(arg);
            memcpy(that->fOOBReqDst, c->dst, 6);
            memcpy(that->fOOBReqBuf, c->payload, c->len);
            that->fOOBReqLen = c->len;
            that->fOOBReqPending = true;
            that->trySendPendingOOB();
            return kIOReturnSuccess;
        };
        if (fController && fController->getWorkLoop() && fController->getWorkLoop()->inGate())
            act(nullptr, this, &ctx, nullptr, nullptr);
        else if (_fCommandGate)
            _fCommandGate->runAction(act, this, &ctx);
    }
}

IOReturn AirportItlwmAWDLInterface::
setAWDL_OOB_REQUEST(apple80211_awdl_oob_request *d)
{
    fCallCount++;
    if (awTimelineAllowed())
        recordOOB(d, false);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
setAWDL_OOB_AUTO_REQUEST(apple80211_awdl_oob_request *d)
{
    fCallCount++;
    if (awTimelineAllowed())
        recordOOB(d, true);
    return kIOReturnSuccess;
}

// ioctl 208, IO80211AWDLPeerManager::setAFTXMode: +4 mode (3 = suppress, e.g.
// infra on 2.4 GHz; 0 = allow). Broadcom: iovar awdl_aftxmode.
// +4 channel number (AppleBCMWLANProximityInterface::setAWDL_MASTER_CHANNEL).
IOReturn AirportItlwmAWDLInterface::
setAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel *d)
{
    fCallCount++;
    fMasterChannel = *(const uint32_t *)((const uint8_t *)d + 4);
    AWDL_LOG("channel %u", fMasterChannel);
    if (awTimelineAllowed())
        setProperty("AWDLMasterChannel", fMasterChannel, 32);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
setAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode *d)
{
    fCallCount++;
    fAfTxMode = *(const uint32_t *)((const uint8_t *)d + 4);
    AWDL_LOG("mode %u", fAfTxMode);
    if (awTimelineAllowed())
        setProperty("AWDLTxAfMode", fAfTxMode, 32);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(apple80211_awdl_sync_channel_sequence *d)
{
    fCallCount++;
    if (!awTimelineAllowed())
        return kIOReturnSuccess;
    const uint8_t *raw = (const uint8_t *)d;
    if (raw[kChanSeqCount] < 1 || raw[kChanSeqCount] > kChanSeqMax)
        return kIOReturnBadArgument;
    memcpy(fChanSeq, raw, sizeof(fChanSeq));
    fChanSeqValid = true;
    char seq[kChanSeqMax * 4 + 1] = {};
    for (unsigned i = 0, n = 0; i < raw[kChanSeqCount] && n + 4 < sizeof(seq); i++) {
        const apple80211_channel *c = (const apple80211_channel *)(raw + kChanSeqEntries) + i;
        n += snprintf(seq + n, sizeof(seq) - n, " %u", c->channel);
    }
    AWDL_LOG("count %u enc %u step %u:%s", raw[kChanSeqCount], raw[6], raw[kChanSeqStep], seq);
    setProperty("AWDLChanSeq", seq);
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "enc %u step %u dup %u fill %u", raw[6], raw[kChanSeqStep], raw[8], raw[9]);
    setProperty("AWDLChanSeqHdr", hdr);
    if (awDataAllowed() && awListenAllowed() && fAWRunning)
        scheduleAlignedListen(hostUs());
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(apple80211_awdl_sync_channel_sequence *d)
{
    if (!fChanSeqValid)
        return kIOReturnUnsupported;
    memcpy(d, fChanSeq, sizeof(fChanSeq));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
getAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode *d)
{
    if (!awTimelineAllowed())
        return kIOReturnUnsupported;
    memset(d, 0, sizeof(*d));
    d->version = APPLE80211_VERSION;
    d->mode = presenceMode();
    return kIOReturnSuccess;
}

#pragma mark - implemented AWDL IOCTLs + remaining stubs

IOReturn AirportItlwmAWDLInterface::
getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES(apple80211_awdl_cap *p)
{
    fCallCount++;
    if (!p)
        return kIOReturnBadArgument;
    p->version = APPLE80211_VERSION;
    p->cap = APPLE80211_AWDL_CAP_CCA_STATS;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
getAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode *p)
{
    fCallCount++;
    if (!p)
        return kIOReturnBadArgument;
    p->version = APPLE80211_VERSION;
    p->mode = fAfTxMode;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
getAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel *p)
{
    fCallCount++;
    if (!p)
        return kIOReturnBadArgument;
    p->version = APPLE80211_VERSION;
    p->master_channel = fMasterChannel ? fMasterChannel : 6;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
setAWDL_SECONDARY_MASTER_CHANNEL(apple80211_awdl_secondary_master_channel *p)
{
    fCallCount++;
    if (p) {
        fSecondaryMasterChannel = p->secondary_master_channel;
        setProperty("AWDLSecondaryMasterChannel", fSecondaryMasterChannel, 32);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
getAWDL_SECONDARY_MASTER_CHANNEL(apple80211_awdl_secondary_master_channel *p)
{
    fCallCount++;
    if (!p)
        return kIOReturnBadArgument;
    p->version = APPLE80211_VERSION;
    p->secondary_master_channel = fSecondaryMasterChannel ? fSecondaryMasterChannel : 149;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
setAWDL_ELECTION_ID(apple80211_awdl_election_id *p)
{
    fCallCount++;
    if (p) {
        fElectionId = p->election_id;
        setProperty("AWDLElectionId", fElectionId, 32);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
getAWDL_ELECTION_ID(apple80211_awdl_election_id *p)
{
    fCallCount++;
    if (!p)
        return kIOReturnBadArgument;
    p->version = APPLE80211_VERSION;
    p->election_id = fElectionId;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
setAWDL_ELECTION_METRIC(apple80211_awdl_election_metric *p)
{
    fCallCount++;
    if (p) {
        fElectionMetric = p->metric;
        setProperty("AWDLElectionMetric", fElectionMetric, 32);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
getAWDL_ELECTION_METRIC(apple80211_awdl_election_metric *p)
{
    fCallCount++;
    if (!p)
        return kIOReturnBadArgument;
    p->version = APPLE80211_VERSION;
    p->metric = fElectionMetric;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
setAWDL_ELECTION_RSSI_THRESHOLDS(apple80211_awdl_election_rssi_thresholds *p)
{
    fCallCount++;
    if (p)
        fElectionRssiThresholds = *p;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
getAWDL_ELECTION_RSSI_THRESHOLDS(apple80211_awdl_election_rssi_thresholds *p)
{
    fCallCount++;
    if (!p)
        return kIOReturnBadArgument;
    *p = fElectionRssiThresholds;
    p->version = APPLE80211_VERSION;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
setAWDL_MIN_RATE(apple80211_awdl_min_rate *p)
{
    fCallCount++;
    if (p)
        fMinRate = *p;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
getAWDL_MIN_RATE(apple80211_awdl_min_rate *p)
{
    fCallCount++;
    if (!p)
        return kIOReturnBadArgument;
    *p = fMinRate;
    p->version = APPLE80211_VERSION;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(apple80211_awdl_extension_state_machine_parameter *p)
{
    fCallCount++;
    if (p)
        fExtStateMachineParams = *p;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(apple80211_awdl_extension_state_machine_parameter *p)
{
    fCallCount++;
    if (!p)
        return kIOReturnBadArgument;
    *p = fExtStateMachineParams;
    p->version = APPLE80211_VERSION;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
setAWDL_ENCRYPTION_KEYS(apple80211_key *p)
{
    fCallCount++;
    if (p) {
        fEncryptionKeySets++;
        setProperty("AWDLEncryptionKeySets", fEncryptionKeySets, 32);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::
setAWDL_PEER_TRAFFIC_REGISTRATION(apple80211_awdl_peer_traffic_registration *p)
{
    fCallCount++;
    if (!p)
        return kIOReturnBadArgument;
    fPeerTrafficRegs++;
    if (p->active) {
        fPeerTrafficActive++;
    } else if (fPeerTrafficActive > 0) {
        fPeerTrafficActive--;
    }
    setProperty("AWDLPeerTrafficActive", fPeerTrafficActive, 32);
    setProperty("AWDLPeerTrafficRegs", fPeerTrafficRegs, 64);
    if (awDataAllowed() && fAWRunning && fController) {
        auto act = [](OSObject *, void *self, void *, void *, void *) -> IOReturn {
            auto that = static_cast<AirportItlwmAWDLInterface *>(self);
            if (that->fController && that->fController->fHalService) {
                if (ieee80211com *ic = that->fController->fHalService->get80211Controller()) {
                    bool want = awDataAllowed() && that->fAWRunning;
                    if ((int)want != ic->ic_awdl_want_data) {
                        ic->ic_awdl_want_data = want ? 1 : 0;
                        that->notifyHalSync();
                    }
                }
            }
            if (!that->fListenAlignedActive)
                that->scheduleAlignedListen(hostUs());
            return kIOReturnSuccess;
        };
        if (fController->getWorkLoop() && fController->getWorkLoop()->inGate())
            act(nullptr, this, nullptr, nullptr, nullptr);
        else if (_fCommandGate)
            _fCommandGate->runAction(act, this);
    }
    return kIOReturnSuccess;
}

#pragma mark - getters without a known layout

AWDL_GET_STUB(AWDL_ELECTION_PARAMS, apple80211_awdl_election_params)
AWDL_GET_STUB(AWDL_DEVICE_CAPABILITIES, apple80211_awdl_device_capabilities)
AWDL_GET_STUB(AWDL_ENCRYPTION_TYPE, apple80211_encrypt)
AWDL_GET_STUB(AWDL_TXCAL_PERIOD, apple80211_awdl_txcal_period)
AWDL_GET_STUB(AWDL_DEV_STATS, apple80211_awdl_dev_stats)
AWDL_GET_STUB(AWDL_DFSP_CONFIG, apple80211_awdl_dfsp_config)
AWDL_GET_STUB(AWDL_CCA, apple80211_awdl_cca)
AWDL_GET_STUB(LLW_PARAMS, apple80211_llw_params)

#pragma mark - setters accepted but not acted on yet

AWDL_SET_STUB(SSID, apple80211_ssid_data)
AWDL_SET_STUB(CHANNEL, apple80211_channel_data)
AWDL_SET_STUB(AWDL_ELECTION_PARAMS, apple80211_awdl_election_params)
AWDL_SET_STUB(AWDL_ENCRYPTION_TYPE, apple80211_encrypt)
AWDL_SET_STUB(AWDL_TXCAL_PERIOD, apple80211_awdl_txcal_period)
AWDL_SET_STUB(AWDL_AWDL_ADVERTISERS, awdl_peer_advert_add)
AWDL_SET_STUB(AWDL_DFSP_CONFIG, apple80211_awdl_dfsp_config)
AWDL_SET_STUB(AWDL_DFSP_UCSA_CONFIG, apple80211_awdl_dfsp_ucsa_config)
// Placeholder; real implementation below (needs the LLW class definition).
AWDL_SET_STUB(AWDL_OOB_SEC_PAYLOAD_REQUEST, apple80211_awdl_af_sec_payload_request)
AWDL_SET_STUB(LLW_PARAMS, apple80211_llw_params)
AWDL_SET_STUB(TX_RATE, apple80211_tx_rate)
AWDL_SET_STUB(P2P_COEX_PROTECTION, apple80211_p2p_coex_protection)
AWDL_SET_STUB(PSF_DWELL, apple80211_awdl_psf_dwell_params)
AWDL_SET_STUB(AWDL_CONFIG, apple80211_awdl_config_request)
AWDL_SET_STUB(AWDL_RTG_PEER_STATS_CONFIG, apple80211_rtg_peer_stats_cfg)

// ---------------------------------------------------------------------------
// llw0 (stage 2, experimental): see AirportItlwmLLWInterface in the header.
// ---------------------------------------------------------------------------
#define LLW_LOG(fmt, ...) IOLog("AirportItlwm: LLW %s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__)

OSDefineMetaClassAndStructors(AirportItlwmLLWInterface, IO80211InfraInterface);

// Role 9 from AppleBCMWLANProximityInterface::setCOMPANION_SKYWALK_IF_CREATE (params +0x10 = 9, +0x18 = type 2).
#define kAirportItlwmLLWRole 9

AirportItlwmLLWInterface *AirportItlwmLLWInterface::
withController(AirportItlwm *controller, const ether_addr *mac)
{
    if (!controller || !mac)
        return nullptr;
    AirportItlwmLLWInterface *llw = OSTypeAlloc(AirportItlwmLLWInterface);
    if (!llw)
        return nullptr;
    if (!llw->init()) {
        llw->release();
        return nullptr;
    }
    llw->fController = controller;
    memcpy(&llw->fMac, mac, sizeof(ether_addr));
    llw->fPool = nullptr;
    llw->fTxQ = nullptr;
    llw->fRxQ = nullptr;
    llw->fEventsAdded = false;
    llw->fRegistered = false;
    llw->setInterfaceRole(kAirportItlwmLLWRole);
    return llw;
}

const char *AirportItlwmLLWInterface::
getBSDNamePrefix()
{
    return "llw";
}

uint32_t AirportItlwmLLWInterface::
txAction(OSObject *owner, IOSkywalkTxSubmissionQueue *, IOSkywalkPacket *const *pkts, uint32_t n, void *)
{
    AirportItlwmLLWInterface *self = OSDynamicCast(AirportItlwmLLWInterface, owner);
    if (self && self->fPool && pkts) {
        for (uint32_t i = 0; i < n; i++) {
            if (pkts[i])
                self->fPool->deallocatePacket(pkts[i]);
        }
    }
    return n;
}

uint32_t AirportItlwmLLWInterface::
rxAction(OSObject *, IOSkywalkRxCompletionQueue *, IOSkywalkPacket **, uint32_t, void *)
{
    // gatedEnqueue treats packets[] as an output array populated by rxAction; return 0 until RX is wired.
    return 0;
}

bool AirportItlwmLLWInterface::
setupDatapath()
{
    IOSkywalkPacketBufferPool::PoolOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.packetCount = 128;
    opts.bufferCount = 128;
    opts.bufferSize = 2048;
    opts.maxBuffersPerPacket = 1;
    opts.memorySegmentSize = 0;
    opts.poolFlags = 0;
    fPool = IOSkywalkPacketBufferPool::withName("AirportItlwmLLWPool", this, 0, &opts);
    if (!fPool) {
        IOLog("LLW %s: pool create failed\n", __FUNCTION__);
        return false;
    }
    fTxQ = IOSkywalkTxSubmissionQueue::withPool(fPool, 0, 64, this, txAction, this, 0);
    fRxQ = IOSkywalkRxCompletionQueue::withPool(fPool, 1, 64, this, rxAction, this, 0);
    if (!fTxQ || !fRxQ) {
        IOLog("LLW %s: queue create failed\n", __FUNCTION__);
        freeDatapath();
        return false;
    }
    return true;
}

void AirportItlwmLLWInterface::
teardownDatapath()
{
    if (fRegistered) {
        deregisterEthernetInterface(0);
        fRegistered = false;
    }
    if (fEventsAdded) {
        IOWorkLoop *wl = getWorkLoop();
        if (!wl && fController)
            wl = fController->getWorkLoop();
        if (wl) {
            if (fRxQ)
                wl->removeEventSource(fRxQ);
            if (fTxQ)
                wl->removeEventSource(fTxQ);
        }
        fEventsAdded = false;
    }
}

void AirportItlwmLLWInterface::
freeDatapath()
{
    teardownDatapath();
    OSSafeReleaseNULL(fTxQ);
    OSSafeReleaseNULL(fRxQ);
    OSSafeReleaseNULL(fPool);
}

bool AirportItlwmLLWInterface::
registerDatapath()
{
    if (!fPool || !fTxQ || !fRxQ)
        return false;
    IOWorkLoop *wl = getWorkLoop();
    if (!wl && fController)
        wl = fController->getWorkLoop();
    if (!wl) {
        IOLog("LLW %s: no workloop\n", __FUNCTION__);
        return false;
    }
    if (wl->addEventSource(fTxQ) != kIOReturnSuccess) {
        IOLog("LLW %s: addEventSource(txQ) failed\n", __FUNCTION__);
        return false;
    }
    if (wl->addEventSource(fRxQ) != kIOReturnSuccess) {
        wl->removeEventSource(fTxQ);
        IOLog("LLW %s: addEventSource(rxQ) failed\n", __FUNCTION__);
        return false;
    }
    fEventsAdded = true;

    IOSkywalkEthernetInterface::RegistrationInfo info;
    static_assert(sizeof(info) >= 0x130, "RegistrationInfo ABI size");
    memset(&info, 0, sizeof(info));
    if (!initRegistrationInfo(&info, 1, sizeof(info))) {
        IOLog("LLW %s: initRegistrationInfo failed\n", __FUNCTION__);
        teardownDatapath();
        return false;
    }
    // Populate RegistrationInfo like AppleBCMWLANCore::createSkywalkInterface (type 2 / "llw").
    uint8_t *raw = reinterpret_cast<uint8_t *>(&info);
    *reinterpret_cast<uint32_t *>(raw + 0x0c) = 3;
    *reinterpret_cast<uint32_t *>(raw + 0x14) = 2;
    *reinterpret_cast<uint32_t *>(raw + 0x24) = 0x80;
    *reinterpret_cast<uint32_t *>(raw + 0x28) = 0x80;
    *reinterpret_cast<const char **>(raw + 0x30) = "llw";
    *reinterpret_cast<uint32_t *>(raw + 0x38) = 0;
    *reinterpret_cast<uint16_t *>(raw + 0x3c) = 0x40;
    *reinterpret_cast<uint32_t *>(raw + 0x40) = 4;
    memcpy(raw + 0x108, &fMac, sizeof(ether_addr));

    IOSkywalkPacketQueue *queues[2] = { (IOSkywalkPacketQueue *)fTxQ, (IOSkywalkPacketQueue *)fRxQ };
    bool ok = registerInfraEthernetInterface(&info, queues, 2, fPool, fPool);
    IOLog("LLW %s: registerInfraEthernetInterface -> %d\n", __FUNCTION__, ok);
    if (!ok) {
        teardownDatapath();
        return false;
    }
    fRegistered = true;
    return true;
}

void AirportItlwmLLWInterface::
free()
{
    freeDatapath();
    IO80211InfraInterface::free();
}


// Experimental llw0 creation/teardown, enabled only with boot-arg -itlllw.
// From AppleBCMWLANProximityInterface::setCOMPANION_SKYWALK_IF_CREATE:
// +0x00 version, +0x04 enable (1 = create, 0 = destroy), +0x08 16-byte BSD name.
struct AirportItlwmCompanionCreate {
    uint32_t version;
    uint32_t enable;
    char     bsd_name[16];
};

void AirportItlwmAWDLInterface::
teardownLLWInterface()
{
    if (!fLLWIf)
        return;
    AirportItlwmLLWInterface *llw = fLLWIf;
    setCompanionLowLatencySkywalkInterface(nullptr);
    llw->setParentInterface(nullptr);
    llw->teardownDatapath();
    if (fController) {
        llw->stop(fController);
        llw->detach(fController);
    }
    llw->release();
    fLLWIf = nullptr;
}

// Final teardown (from stop/free): refuse new requests and wait for the worker to finish.
void AirportItlwmAWDLInterface::
destroyLLWInterface()
{
    if (fLLWLock) {
        IOLockLock(fLLWLock);
        fLLWDying = true;
        fLLWWant = false;
        IOLockUnlock(fLLWLock);
        for (int i = 0; i < 3000; i++) { // up to 30 s
            IOLockLock(fLLWLock);
            bool busy = fLLWScheduled;
            IOLockUnlock(fLLWLock);
            if (!busy)
                break;
            IOSleep(10);
        }
    }
    teardownLLWInterface();
}

bool AirportItlwmAWDLInterface::
createLLWInterface()
{
    if (fLLWIf)
        return true;
    if (!fController)
        return false;
    AirportItlwmLLWInterface *llw = AirportItlwmLLWInterface::withController(fController, &fMac);
    if (!llw)
        return false;
    llw->setLowLatencyEnabled(true);
    llw->setParentInterface(this);
    llw->setCompanionInterfaceId(getInterfaceId());
    llw->setInitMacAddress(fMac);
    if (!llw->attach(fController)) {
        LLW_LOG("llw attach failed");
        llw->setParentInterface(nullptr);
        llw->release();
        return false;
    }
    if (!llw->start(fController)) {
        LLW_LOG("llw start failed");
        llw->detach(fController);
        llw->setParentInterface(nullptr);
        llw->release();
        return false;
    }
    if (!llw->setupDatapath() || !llw->registerDatapath()) {
        LLW_LOG("llw datapath setup failed");
        llw->freeDatapath();
        llw->stop(fController);
        llw->detach(fController);
        llw->setParentInterface(nullptr);
        llw->release();
        return false;
    }
    setCompanionLowLatencySkywalkInterface(llw);
    fLLWIf = llw;
    LLW_LOG("llw created, bsd %s", llw->getBSDName() ?: "(null)");
    return true;
}

// Runs on a thread_call, never on the controller workloop. Converges on the latest requested state.
void AirportItlwmAWDLInterface::
llwWork()
{
    for (;;) {
        IOLockLock(fLLWLock);
        bool want = fLLWWant;
        IOLockUnlock(fLLWLock);
        if (want)
            createLLWInterface();
        else
            teardownLLWInterface();
        IOLockLock(fLLWLock);
        if (fLLWWant == want) {
            fLLWScheduled = false;
            IOLockUnlock(fLLWLock);
            break;
        }
        IOLockUnlock(fLLWLock);
    }
    release(); // balances the retain in requestLLW()
}

void AirportItlwmAWDLInterface::
requestLLW(bool create)
{
    if (!fLLWLock || !fLLWCall)
        return;
    IOLockLock(fLLWLock);
    if (fLLWDying) {
        IOLockUnlock(fLLWLock);
        return;
    }
    fLLWWant = create;
    bool schedule = !fLLWScheduled;
    fLLWScheduled = true;
    IOLockUnlock(fLLWLock);
    if (schedule) {
        retain();
        thread_call_enter(fLLWCall);
    }
}

IOReturn AirportItlwmAWDLInterface::
setCOMPANION_SKYWALK_IF_CREATE(apple80211_companion_skywalk_if_create *d)
{
    uint32_t en = 0;
    LLW_LOG("ioctl received d=%p", d);
    if (!d || !PE_parse_boot_argn("-itlllw", &en, sizeof(en))) {
        LLW_LOG("COMPANION_SKYWALK_IF_CREATE ignored (no -itlllw)");
        return kIOReturnSuccess;
    }
    if (!fController)
        return kIOReturnNotReady;
    AirportItlwmCompanionCreate *out = (AirportItlwmCompanionCreate *)d;
    LLW_LOG("enable=%u version=%u", out->enable, out->version);
    if (out->enable == 0) {
        requestLLW(false);
        return kIOReturnSuccess;
    }
    if (out->enable != 1)
        return kIOReturnSuccess;
    // start() can't complete on this (gated) thread, so create asynchronously and report the expected name.
    requestLLW(true);
    strlcpy(out->bsd_name, fLLWIf ? (fLLWIf->getBSDName() ?: "llw0") : "llw0", sizeof(out->bsd_name));
    return kIOReturnSuccess;
}

#endif /* AIRPORT_WCL && Tahoe */
