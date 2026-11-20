//
//  AirportItlwmAWDLInterface.hpp
//  AirportItlwm
//
//  awdl0 for macOS 26 (Tahoe): the IO80211AWDLProtocol interface, a host-side
//  availability-window timeline, listen/transmit on Intel P2P Device dwells and a data path.
//  The contract was reverse-engineered from the macOS 26 IO80211Family and AppleBCMWLAN kexts.
//

#ifndef AirportItlwmAWDLInterface_hpp
#define AirportItlwmAWDLInterface_hpp

#if defined(AIRPORT_WCL) && __IO80211_TARGET != __MAC_15_2

#include <Airport/Apple80211.h>
#include <Airport/macOS26/IO80211VirtualInterface.h>
#include <Airport/macOS26/IO80211AWDLProtocol.h>
#include <Airport/macOS26/IO80211InfraInterface.h>
#include <Airport/IOSkywalkPacketQueue.h>
#include "AirportItlwmEthernetInterface.hpp"
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOLocks.h>
#include <kern/thread_call.h>

class AirportItlwm;
struct ieee80211_rxinfo;

// IO80211Family buffer objects, used only through their vtables / exported
// functions. Slot offsets are from the Tahoe kernel collection.
class AWDLBufferPoolABI;
class AWDLBufferABI;
extern "C" AWDLBufferPoolABI *IO80211VirtualInterface_getMgmtFramePool(IO80211VirtualInterface *)
    __asm__("__ZN23IO80211VirtualInterface16getMgmtFramePoolEv");
extern "C" void IO80211VirtualInterface_bpfTapInput(IO80211VirtualInterface *, AWDLBufferABI *,
                                                    unsigned int, void *, unsigned long)
    __asm__("__ZN23IO80211VirtualInterface11bpfTapInputEP13IO80211BufferjPvm");

// apple80211_data_path_interface_stats as IO80211SkywalkInterface::updateInterfaceDataStats
// and IO80211PeerManager::updateLocalCounters read it on Tahoe 26.x: +0x08 Tx packets and
// +0x18 Rx packets are the counters IO80211AWDLPeerManager::monitorAWDLDataMode watches;
// +0x150 is added to a running total on every call; +0x180 holds 4 x 0x140 TX latency
// histograms (one per AC). Everything else stays zero.
struct AWDLDataPathStatsABI {
    uint64_t hdr;          // 0x00
    uint64_t txPackets;    // 0x08
    uint64_t f10;          // 0x10
    uint64_t rxPackets;    // 0x18
    uint8_t  rest[0x800 - 0x20];
};
static_assert(sizeof(AWDLDataPathStatsABI) == 0x800, "AWDL data path stats size");
// Posts a firmware-style datapath event to the family's peer manager (event 51 = peer cache control).
extern "C" IOReturn IO80211SkywalkInterface_reportDataPathEvents(IO80211VirtualInterface *, uint32_t, void *, size_t, bool)
    __asm__("__ZN23IO80211SkywalkInterface20reportDataPathEventsEjPvmb");
extern "C" IOReturn IO80211SkywalkInterface_updateInterfaceDataStats(IO80211VirtualInterface *, AWDLDataPathStatsABI *)
    __asm__("__ZN23IO80211SkywalkInterface24updateInterfaceDataStatsEP36apple80211_data_path_interface_stats");

// Role values used by IO80211 on Tahoe (apple80211_virtual_if_role).
enum : unsigned int {
    kAirportItlwmRoleAWDL = 6,
};

// BSD side of awdl0. Same pairing as the station interface (legacy ifnet + Skywalk
// control interface), but it never touches the shared radio: interface flag changes
// are handled locally and output is dropped until the AWDL datapath exists.
class AirportItlwmAWDLEthernetInterface : public AirportItlwmEthernetInterface {
    OSDeclareDefaultStructors(AirportItlwmAWDLEthernetInterface)

public:
    const char *getNamePrefix() const override;
    // ifnet_allocate_extended rejects a uniqueid already used by another ifnet
    // (EBUSY), so awdl0 must not reuse the station's kIOMACAddress.
    bool initIfnetParams(struct ifnet_init_params *params) override;
    // ifnet_attach takes its link-layer address from the controller's
    // kIOMACAddress (the station MAC); replace it with awdl0's own.
    IOReturn attachToDataLinkLayer(IOOptionBits options, void *parameter) override;
    void setAWDLAddress(const ether_addr &mac) { memcpy(&fMac, &mac, sizeof(fMac)); }
    IOReturn getInterfaceAddress(IOEthernetAddress *addr) override
    {
        memcpy(addr, &fMac, sizeof(fMac));
        return kIOReturnSuccess;
    }
    SInt32 performCommand(IONetworkController *controller, unsigned long cmd,
                          void *arg0, void *arg1) override;
    UInt32 dropOutput(mbuf_t m, void *param);
    uint64_t droppedPackets() const { return fDropped; }
    // Stage 4: an AWDL data frame for awdl0; consumes m, returns 1.
    int inputAWDLData(mbuf_t m, class AirportItlwmAWDLInterface *awdl);
    // Stage 4 output: hand Ethernet frames to the AWDL interface's tx queue.
    UInt32 outputAWDL(mbuf_t m, void *param);
    void setAWDLOwner(class AirportItlwmAWDLInterface *owner) { fOwner = owner; }

private:
    class AirportItlwmAWDLInterface *fOwner;
    uint64_t fDropped;
    ether_addr fMac;
};

class AirportItlwmAWDLInterface : public IO80211AWDLProtocol {
    OSDeclareDefaultStructors(AirportItlwmAWDLInterface)

public:
    static AirportItlwmAWDLInterface *withController(AirportItlwm *controller, ether_addr *mac);

    // IO80211VirtualInterface
    bool init(IO80211Controller *controller, ether_addr *mac, unsigned int role, char const *prefix) override;
    void free() override;
    IOReturn setMacAddress(ether_addr &) override;
    // Diagnostic tunables for the listen dwells, settable at run time via setProperties.
    IOReturn setProperties(OSObject *properties) override;

    // IO80211AWDLProtocol: getters
    IOReturn getSSID(apple80211_ssid_data *) override;
    IOReturn getCHANNEL(apple80211_channel_data *) override;
    IOReturn getBSSID(apple80211_bssid_data *) override;
    IOReturn getSTATE(apple80211_state_data *) override;
    IOReturn getAWDL_SYNC_PARAMS(apple80211_awdl_sync_params *) override;
    IOReturn getAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled *) override;
    IOReturn getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(apple80211_awdl_extension_state_machine_parameter *) override;
    IOReturn getAWDL_ELECTION_ID(apple80211_awdl_election_id *) override;
    IOReturn getAWDL_ELECTION_METRIC(apple80211_awdl_election_metric *) override;
    IOReturn getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(apple80211_awdl_sync_channel_sequence *) override;
    IOReturn getPEER_CACHE_MAXIMUM_SIZE(apple80211_peer_cache_maximum_size *) override;
    IOReturn getAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel *) override;
    IOReturn getAWDL_SYNC_STATE(apple80211_awdl_sync_state *) override;
    IOReturn getAWDL_ELECTION_RSSI_THRESHOLDS(apple80211_awdl_election_rssi_thresholds *) override;
    IOReturn getAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode *) override;
    IOReturn getAWDL_ELECTION_PARAMS(apple80211_awdl_election_params *) override;
    IOReturn getAWDL_DEVICE_CAPABILITIES(apple80211_awdl_device_capabilities *) override;
    IOReturn getAWDL_SECONDARY_MASTER_CHANNEL(apple80211_awdl_secondary_master_channel *) override;
    IOReturn getAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode *) override;
    IOReturn getAWDL_MIN_RATE(apple80211_awdl_min_rate *) override;
    IOReturn getAWDL_ENCRYPTION_TYPE(apple80211_encrypt *) override;
    IOReturn getAWDL_TXCAL_PERIOD(apple80211_awdl_txcal_period *) override;
    IOReturn getAWDL_DEV_STATS(apple80211_awdl_dev_stats *) override;
    IOReturn getAWDL_DFSP_CONFIG(apple80211_awdl_dfsp_config *) override;
    IOReturn getAWDL_CCA(apple80211_awdl_cca *) override;
    IOReturn getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES(apple80211_awdl_cap *) override;
    IOReturn getLLW_PARAMS(apple80211_llw_params *) override;

    // IO80211AWDLProtocol: setters
    IOReturn setSSID(apple80211_ssid_data *) override;
    IOReturn setCHANNEL(apple80211_channel_data *) override;
    IOReturn setAWDL_SYNC_PARAMS(apple80211_awdl_sync_params *) override;
    IOReturn setAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled *) override;
    IOReturn setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(apple80211_awdl_extension_state_machine_parameter *) override;
    IOReturn setAWDL_ELECTION_ID(apple80211_awdl_election_id *) override;
    IOReturn setAWDL_BSSID(apple80211_awdl_bssid *) override;
    IOReturn setAWDL_ELECTION_METRIC(apple80211_awdl_election_metric *) override;
    IOReturn setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(apple80211_awdl_sync_channel_sequence *) override;
    IOReturn setAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel *) override;
    IOReturn setAWDL_SYNC_STATE(apple80211_awdl_sync_state *) override;
    IOReturn setAWDL_ELECTION_RSSI_THRESHOLDS(apple80211_awdl_election_rssi_thresholds *) override;
    IOReturn setAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode *) override;
    IOReturn setPEER_CACHE_CONTROL(apple80211_peer_cache_control *) override;
    uint64_t getLastQueuePacketTime(ether_addr *) override;
    uint64_t getLastRxUnicastLinkActivityTime(ether_addr *) override;
    void notePeerRx(const uint8_t *dst, const uint8_t *src);
    void notePeerTx(const uint8_t *mac);
    IOReturn setAWDL_OOB_REQUEST(apple80211_awdl_oob_request *) override;
    IOReturn setAWDL_SYNC_FRAME_TEMPLATE(apple80211_awdl_sync_frame_template *) override;
    IOReturn setAWDL_ELECTION_PARAMS(apple80211_awdl_election_params *) override;
    IOReturn setAWDL_PEER_TRAFFIC_REGISTRATION(apple80211_awdl_peer_traffic_registration *) override;
    IOReturn setAWDL_SECONDARY_MASTER_CHANNEL(apple80211_awdl_secondary_master_channel *) override;
    IOReturn setAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode *) override;
    IOReturn setAWDL_MIN_RATE(apple80211_awdl_min_rate *) override;
    IOReturn setAWDL_ENCRYPTION_KEYS(apple80211_key *) override;
    IOReturn setAWDL_ENCRYPTION_TYPE(apple80211_encrypt *) override;
    IOReturn setAWDL_OOB_AUTO_REQUEST(apple80211_awdl_oob_request *) override;
    IOReturn setAWDL_TXCAL_PERIOD(apple80211_awdl_txcal_period *) override;
    IOReturn setAWDL_AWDL_ADVERTISERS(awdl_peer_advert_add *) override;
    IOReturn setAWDL_DFSP_CONFIG(apple80211_awdl_dfsp_config *) override;
    IOReturn setAWDL_DFSP_UCSA_CONFIG(apple80211_awdl_dfsp_ucsa_config *) override;
    IOReturn setCOMPANION_SKYWALK_IF_CREATE(apple80211_companion_skywalk_if_create *) override;
    IOReturn setAWDL_OOB_SEC_PAYLOAD_REQUEST(apple80211_awdl_af_sec_payload_request *) override;
    IOReturn setLLW_PARAMS(apple80211_llw_params *) override;
    IOReturn setTX_RATE(apple80211_tx_rate *) override;
    IOReturn setP2P_COEX_PROTECTION(apple80211_p2p_coex_protection *) override;
    IOReturn setPSF_DWELL(apple80211_awdl_psf_dwell_params *) override;
    IOReturn setAWDL_CONFIG(apple80211_awdl_config_request *) override;
    IOReturn setAWDL_RTG_PEER_STATS_CONFIG(apple80211_rtg_peer_stats_cfg *) override;

    // Hand a received AWDL action frame (802.11 header included) to IO80211's
    // AWDL peer manager, packaged like AppleBCMWLANProximityInterface does.
    // Returns 1 when the frame was AWDL and has been consumed.
    int inputActionFrame(const uint8_t *frame, size_t len, const ieee80211_rxinfo *rxi);
    uint64_t rxActionFrames() const { return fRxAF; }
    // Stop and remove the AW timer; call before the interface is stopped.
    void teardownAW();
    void destroyLLWInterface();
    // Async create/destroy of llw0 (the ioctl runs on the controller workloop; start() waits for PM there).
    void requestLLW(bool create);
    void llwWork();
    bool createLLWInterface();
    void teardownLLWInterface();

private:
    // Host-side availability window timeline (what Broadcom firmware does with
    // its AW start/end events). One "slot" is one channel sequence entry:
    // presence_mode AWs of aw_period TU each; times are host microseconds.
    static void awTimerFired(OSObject *owner, IOTimerEventSource *sender);
    void startAWTimeline();
    void stopAWTimeline();
    void scheduleNextAW(uint64_t nowUs);
    void syncFromFrame(const uint8_t *af, size_t len, const uint8_t *src, uint64_t rxUs);
    void postAWEvents();
    uint64_t slotUs() const;
    uint16_t presenceMode() const;
    bool isSyncMaster() const;
    void publishSyncState();

    IOTimerEventSource *fAWTimer;
    bool fAWRunning;
    int64_t fRefUs;            // a slot boundary on the current timeline
    uint16_t fRefSeq;          // AW sequence number at fRefUs
    bool fLastValid;
    int64_t fLastStartUs;
    uint16_t fLastSeq;
    uint64_t fAWStarts;
    uint64_t fSyncUpdates;
    int64_t fLastSyncErrUs;
    ether_addr fSyncMaster;    // from setAWDL_SYNC_STATE; zero = none programmed
    uint32_t fSyncMode;
    uint32_t fPresenceMode;    // from setAWDL_PRESENCE_MODE, 0 = not set
    bool fInAW;                // an AW start was posted without its end
    uint32_t fTsfOffset;       // host_us - rx TSF (mod 2^32), min-latency estimate
    bool fTsfValid;
    uint32_t fSyncOutliers;      // consecutive large sync errors ignored
    uint64_t fSyncOutliersTotal;
    void maybeListen(uint32_t slotChannel, uint16_t seq);
    uint32_t fListenSlot;
    uint64_t fListenStarted;
    uint64_t fListenBusy;
    uint64_t fListenSkippedHome;
    uint64_t fListenInfraHold;      // off-channel listens held while infra is associated
    uint32_t fListenInfraSlot;
    unsigned fListenParkChan;       // P2P PHY parked on this infra channel (0: not parked)
    uint64_t fListenParked;
    uint64_t fHomeListenNotBeforeUs; // next infra-channel AWDL listen not before (host us)
    uint64_t fHomeListens;
    bool fListenNextHome;           // the scheduled own listen is on the infra channel
    uint32_t fListenDwellTU;        // AWDLListenDwellTU
    uint32_t fListenEveryNth;       // AWDLListenEveryNth: 1 = every eligible slot
    // Peer timeline, so a listen can start when the peer is on the channel.
    static void listenTimerFired(OSObject *owner, IOTimerEventSource *sender);
    void trackPeer(const uint8_t *af, size_t len, const uint8_t *src, uint64_t rxUs, unsigned rxChan);
    bool parsePeerChanSeq(const uint8_t *v, size_t len);
    unsigned peerChannelAt(uint64_t us) const;
    void scheduleAlignedListen(uint64_t afterUs);
    // -itlawdldata: dwell on our own channel sequence (what we advertise), as
    // Broadcom firmware does, instead of chasing the peer's sequence.
    bool scheduleOwnListen(uint64_t afterUs);
    uint8_t ownChannelAt(uint16_t seq) const;
    uint64_t fListenOwn;            // own-sequence dwells started
    uint64_t fBssidSets;
    uint64_t fPeerCacheOps;
    uint64_t fDataTxNotCommon;      // data held: peer not on our channel now
    void startListen(unsigned chan, uint32_t dwellTU, unsigned home, bool aligned);
    IOTimerEventSource *fListenTimer;
    uint32_t fListenAlign;          // AWDLListenAlign
    bool fListenAlignedActive;      // the listen timer drives the listens
    uint8_t fListenNextChan;
    uint32_t fListenNextDwellTU;
    uint64_t fListenAligned;
    uint64_t fListenNoSlot;
    bool fPeerValid;
    ether_addr fPeerMac;
    uint64_t fPeerSeenUs;
    int64_t fPeerRefUs;             // a slot boundary of the peer
    uint16_t fPeerRefSeq;
    uint16_t fPeerPm;
    uint32_t fPeerSlotUs;
    uint8_t fPeerChan[16];
    bool fPeerChanValid;
    uint64_t fPeerUpdates;
    uint64_t fPeerPredHit;
    uint64_t fPeerPredMiss;
    int64_t fPeerLastErrUs;
    uint64_t fRxAFChan6;
    uint64_t fRxAFChan44;
    uint64_t fRxAFChan149;
    uint64_t fRxAFChanOther;
    uint64_t fListenPeerSlots;      // dwells scheduled on the peer's channel in our home/off slots
    // Per transmitter: when its last AWDL action frame arrived and the longest
    // gap so far. IO80211Family drops a peer from NDP after ~6 s without one
    // (shouldRemovePeerFromNDP, lastAF), which kills a running AirDrop.
    struct RxGap { uint8_t mac[6]; uint64_t lastUs; uint64_t maxGapUs; uint32_t count; uint32_t over6s; };
    enum { kRxGapSlots = 8 };
    RxGap fRxGap[kRxGapSlots];
    void noteRxGap(const uint8_t *src, uint64_t nowUs);
    uint64_t fRxAFChan161;          // also counted in Other
    uint64_t fListenOnChan[4];      // listens started on 6, 44, 149, 161
    void publishListenState();
    // Slot pairing diagnostics (AWDLPairStats): each AW slot is classified by
    // our channel and the peer's predicted channel; per class: slots, slots
    // with any AWDL RX, data frames received, data frames sent.
    enum { kPairCommonOff, kPairCommonHome, kPairPeerOnHome, kPairPeerElsewhere,
           kPairOwnOnly, kPairIdle, kPairUnknown, kPairClasses };
    uint64_t fPairSlots[kPairClasses];
    uint64_t fPairHeard[kPairClasses];
    uint64_t fPairRx[kPairClasses];
    uint64_t fPairTx[kPairClasses];
    bool fPairOpen;
    uint8_t fPairCur;
    uint64_t fPairStartUs;
    uint64_t fPairRx0, fPairTx0, fPairAF0;
    uint32_t fPairQMax;
    uint32_t fPairTick;
    void notePairSlot(uint8_t own, uint64_t slotStartUs);
    void publishPairState();
    // AWDL data counts pushed to IO80211Family (debug.itlwm.awdl_dpstats).
    AWDLDataPathStatsABI fDPStats;
    uint64_t fDPStatsCalls;
    uint64_t fDPStatsLastTx, fDPStatsLastRx;
    IOReturn fDPStatsLastRet;
    void reportDataPathStats();
    uint64_t hostRxUs(const ieee80211_rxinfo *rxi);
    // Stage 3: transmit payloads handed down by the peer manager (recorded only).
    void recordOOB(const void *req, bool autoReq);
    uint8_t fSyncTmpl[512];         // PSF payload TLVs (SYNC_FRAME_TEMPLATE)
    uint16_t fSyncTmplLen;
    uint64_t fSyncTmplSets;
    uint8_t fMITmpl[1200];          // MI payload TLVs (OOB_AUTO_REQUEST)
    uint16_t fMITmplLen;
    uint8_t fOOBReqBuf[1200];       // immediate OOB_REQUEST payload TLVs
    uint16_t fOOBReqLen;
    uint8_t fOOBReqDst[6];
    bool fOOBReqPending;
    uint64_t fTxOOBSent;
    uint64_t fOOBSets;
    uint64_t fOOBAutoSets;
    uint32_t fAfTxMode;
    // Stage 3 step 1 (-itlawdltx): send a PSF and an MI in the peer's AW
    // during each aligned listen dwell.
    static void txTimerFired(OSObject *owner, IOTimerEventSource *sender);
    static void peerCacheTimerFired(OSObject *owner, IOTimerEventSource *sender);
    void flushPeerCacheDone();
    static void rocStartedThunk(void *arg, uint8_t chan);
    void scheduleTx(uint8_t chan, uint64_t slotStartUs);
    int64_t effectiveListenLeadUs() const;
    bool peerUsesChannel(uint8_t chan) const;
    bool canFlushDataOnChannel(uint8_t chan, uint64_t whenUs) const;
    void sendAWDLFrame(bool mi);
    bool sendAWDLPayload(bool mi, const uint8_t *dstMac, const uint8_t *tmpl, size_t tmplLen, uint8_t chan);
    void trySendPendingOOB();
    bool canTxOnChannel(uint8_t chan) const;
    unsigned currentHomeChannel() const;
    void notifyHalSync();
    size_t buildAWDLFrame(uint8_t *buf, size_t cap, bool mi, const uint8_t *dstMac, const uint8_t *tmpl, size_t tmplLen, uint64_t nowUs);
    void publishTxState();
    void publishAWDLAddress();
public:
    // Stage 4 datapath counters, updated by the BSD interface.
    uint64_t fDataRx;
    uint64_t fDataRxBad;
    uint64_t fDataRxAmsdu;
    uint64_t fDataRxNoBuf;
    uint32_t fDataRxLastLen;
    uint16_t fDataRxLastType;
    // Why frames land in fDataRxBad, plus what the last one looked like (2.7.8).
    uint64_t fDataRxBadPullup;
    uint64_t fDataRxBadShort;     // data frame with no room for LLC+AWDL headers
    uint64_t fDataRxBadSnap;      // not the Apple AWDL SNAP header
    uint64_t fDataRxNull;         // QoS-null / null data: no payload, expected
    uint32_t fDataRxBadLastLen;
    uint16_t fDataRxBadLastFc;    // fc[0] | fc[1] << 8
    uint16_t fDataRxBadLastHdr;   // 802.11 header length assumed
    uint64_t fDataRxBadLastHead;  // first 8 bytes after that header, big-endian
    uint16_t fDataRxBadLastQos;   // QoS control of the last bad frame (0 if not QoS)
    uint64_t fDataRxBadLastRaw[4]; // first 32 payload bytes after the 802.11 header, big-endian
    uint64_t fDataRxBadBig;       // bad frames longer than the 2304-byte MSDU limit
    // A-MSDU frames the peer sends for bulk data (2.7.11): frames split, subframes
    // delivered, and subframes whose DA/SA differ from the 802.11 addr1/addr2.
    uint64_t fDataRxSplitFrames;
    uint64_t fDataRxSplitSub;
    uint64_t fDataRxSplitAddrDiff;
    // Good frame length buckets: <128, <512, <1024, >=1024 (after SNAP strip).
    uint64_t fDataRxLen[4];
    uint32_t fDataRxMaxLen;
    void publishDataState();
    // Ethernet frames from awdl0, sent as AWDL data frames during a dwell.
    void enqueueData(mbuf_t m);
    void flushData(uint8_t chan = 0);
    // 32 overflowed during AirDrop (AWDLDataTxDropFull): data only
    // leaves in the peer's slots, so a burst from the stack has to wait.
    enum { kDataQueueLen = 256 };
    mbuf_t fDataQ[kDataQueueLen];
    unsigned fDataQHead;
    unsigned fDataQCount;
    uint16_t fDataSeq;
    uint64_t fDataTx;
    uint64_t fDataTxQueued;
    uint64_t fDataTxDropFull;
    uint64_t fDataTxDeferred;
    uint64_t fDataTxErr;
private:
    IOTimerEventSource *fTxTimer;
    // Peer-cache completions owed to IO80211Family (see setPEER_CACHE_CONTROL).
    static constexpr unsigned kPeerCacheQLen = 16;
    struct PeerCacheDone { uint32_t evt; uint32_t op; uint8_t mac[6]; };  // evt 51 = peer cache, 69 = unicast AF tx status
    // Last unicast RX time per peer; IO80211Family evicts peers whose time is stale (base returns 0).
    struct PeerRxTime { uint8_t mac[6]; uint64_t rxT; uint64_t txT; };
    static constexpr unsigned kPeerRxN = 8;
    PeerRxTime fPeerRx[kPeerRxN];
    uint64_t fPeerActQueries;
    IOTimerEventSource *fPeerCacheTimer;
    IOSimpleLock *fPeerCacheLock;
    PeerCacheDone fPeerCacheQ[kPeerCacheQLen];
    unsigned fPeerCacheQCount;
    uint64_t fPeerCacheDone;
    uint64_t fPeerCacheDrop;
    uint8_t fTxChan;
    uint8_t fTxPhase;               // 0 idle, 1 PSF next, 2 MI next, 3 data flush
    volatile uint8_t fRocNotifyChan;
    uint64_t fTxSlotUs;
    uint16_t fTxSeq;                // 802.11 sequence number
    uint32_t fMasterChannel;        // setAWDL_MASTER_CHANNEL
    uint32_t fSecondaryMasterChannel;
    uint32_t fElectionId;
    uint32_t fElectionMetric;
    apple80211_awdl_election_rssi_thresholds fElectionRssiThresholds;
    apple80211_awdl_min_rate fMinRate;
    apple80211_awdl_extension_state_machine_parameter fExtStateMachineParams;
    uint32_t fEncryptionKeySets;
    uint32_t fPeerTrafficActive;
    uint64_t fPeerTrafficRegs;
    uint64_t fTxPSF;
    uint64_t fTxMI;
    uint64_t fTxRefused;            // driver refused (not dwelling, ring full)
    uint64_t fTxSkipMode;           // AF tx mode said suppress
    uint32_t fTxLastErr;
    uint32_t fTxLastLen;
    uint8_t fChanSeq[400];     // raw apple80211_awdl_sync_channel_sequence
    bool fChanSeqValid;

    uint64_t fRxAF;
    uint64_t fRxAFDropped;
    AirportItlwm *fController;
    ether_addr fMac;
    // Cached configuration, returned by the matching getters.
    apple80211_awdl_sync_params fSyncParams;
    bool fSyncEnabled;
    uint32_t fSyncState;
    uint64_t fCallCount;
    class AirportItlwmLLWInterface *fLLWIf;
    thread_call_t fLLWCall;
    IOLock *fLLWLock;
    bool fLLWWant;
    bool fLLWDirty;
    bool fLLWPending;
    bool fLLWRunning;
    bool fLLWDying;
    char fLLWBsdName[16];
};


// llw0 (stage 2, EXPERIMENTAL, off unless boot-arg -itlllw): the low-latency Skywalk
// interface AppleBCMWLANLowLatencyInterface provides on a real Mac. Created from
// COMPANION_SKYWALK_IF_CREATE on awdl0. It owns a packet pool plus one TX submission and
// one RX completion queue so the family can build the logical link / nexus. No AWDL data is
// routed through it yet: TX packets are dropped back to the pool.
class IOSkywalkPacketBufferPool;
class IOSkywalkTxSubmissionQueue;
class IOSkywalkRxCompletionQueue;
class AirportItlwmLLWInterface : public IO80211InfraInterface {
    OSDeclareDefaultStructors(AirportItlwmLLWInterface)
public:
    static AirportItlwmLLWInterface *withController(AirportItlwm *controller, const ether_addr *mac);
    const char *getBSDNamePrefix() override;
    SInt32 initBSDInterfaceParameters(ifnet_init_eparams *params, sockaddr_dl **ll) override;
    void stop(IOService *provider) override;
    bool setupDatapath();
    void teardownDatapath();
    void freeDatapath();
    void free() override;
    IOReturn setMacAddress(mloAddrArray &) override { return kIOReturnSuccess; }
    // Registers the pool + queues with the family (builds logical link / nexus).
    bool registerDatapath();
    IOSkywalkPacketBufferPool *pool() const { return fPool; }
    // Link status as AppleBCMWLANLowLatencyInterface::setInterfaceEnable reports it:
    // reportLinkStatus(3 active / 1 inactive, 0x80 IEEE 802.11 medium). Needs the llw0 ifnet to exist.
    IOReturn reportLLWLink(bool active);
    // ifconfig's "status:" comes from IO80211InfraInterface::updateMediumStatus, which reports IFM_ACTIVE
    // only when the infra link state is kIO80211NetworkLinkUp; only setLinkState(Internal) sets it.
    // Returns the raw setLinkState result for LLWStatus.
    int setLLWLinkState(bool up);
private:
    static uint32_t txAction(OSObject *owner, IOSkywalkTxSubmissionQueue *q, IOSkywalkPacket *const *pkts, uint32_t n, void *ref);
    static uint32_t rxAction(OSObject *owner, IOSkywalkRxCompletionQueue *q, IOSkywalkPacket **pkts, uint32_t n, void *ref);
    AirportItlwm *fController;
    ether_addr fMac;
    IOSkywalkPacketBufferPool *fPool;
    IOSkywalkTxSubmissionQueue *fTxQ;
    IOSkywalkRxCompletionQueue *fRxQ;
    bool fEventsAdded;
    bool fRegistered;
public:
    const char *fLastErr; // last failing setup step, surfaced via the AWDL interface's LLWStatus property
    bool fServiceRegistered; // registerService() done, so IOSkywalkNetworkBSDClient can attach and create llw0
    uint64_t fUniqueId;      // ifnet uniqueid, see initBSDInterfaceParameters
};

#endif /* AIRPORT_WCL && Tahoe */
#endif /* AirportItlwmAWDLInterface_hpp */
