# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.7.96] — 2026-11-23

### Fixed
- `debug.itlwm.ranging=3` now sends the FTM request to the peer named in the `RANGING_START` request (AWDL MAC at +0x32, channel at +0x38) instead of the last AWDL transmitter, which was a different device. The all-zero stop request is ignored.
- `iwx_ftm_task` runs under the command gate like the other tasks, since `iwx_send_cmd_pdu` sleeps on it.

## [2.7.95] — 2026-11-23

### Fixed
- Guard `iwx_ftm_request`, `iwx_ftm_task`, and `debug.itlwm.ftm` against sending `TOF_RANGE_REQ_CMD` when Wi-Fi is not running (`!IFF_RUNNING`, returning `ENETDOWN` from the sysctl), validate channel `1–233` in `iwx_ftm_request` and `debug.itlwm.ftm` even when `debug.itlwm.ftm_chan` is used as fallback, and avoid overwriting `itlwm_ftm_buf` on invalid sysctl input.

## [2.7.94] — 2026-11-23

### Added
- Auto Unlock ranging, step 2: an Intel FTM (802.11mc) range request, ported from Linux iwlwifi (`mvm/ftm-initiator.c`, `TOF_RANGE_REQ_CMD` v13 / `TOF_RANGE_RESPONSE_NOTIF` v9) in `itlwm/hal_iwx/if_iwx_tof.h` and `ItlIwx.cpp`. It is only sent when the firmware reports command version 13. Results are logged as `AirportItlwm: ftm ...` (rtt in picoseconds) and are not yet reported to macOS.
- `debug.itlwm.ftm` (write `aa:bb:cc:dd:ee:ff[,channel]` to send one request), `debug.itlwm.ftm_chan` (default 6) and `debug.itlwm.ranging=3` (also send a request to the last AWDL peer whenever macOS starts ranging). The default stays `2`, so nothing is sent unless you ask for it.

## [2.7.93] — 2026-11-23

### Added
- `debug.itlwm.ranging` (default `2`), step 1 of the Apple Watch Auto Unlock ranging work. The Wi-Fi ranging ioctls (`RANGING_ENABLE`/`START`/`AUTHENTICATE` sets, `RANGING_ENABLE`/`START`/`CAPS` gets) are logged to the kernel log as `AirportItlwm: ranging …` (read with `dmesg`). The `ENABLE` and `START` requests are hex-dumped (256 and 512 bytes, never past the end of the page that holds the request) to learn their layout. `AUTHENTICATE` carries the ranging key and is never dumped. Mode `2` returns success for the three sets so airportd/nearbyd go on; Auto Unlock still ends in `Ranging Timeout` because no measurement is produced yet. `1` logs only (still unsupported), `0` restores the previous behaviour.

## [2.7.92] — 2026-11-23

### Added
- README: documented the `AirPlay to Mac` status (`ControlCenter` AirPlay Receiver / `AirTunes/960.13.25`, `isAirPlayReceiverSupported: yes`, `_airplay._tcp` and `_raop._tcp` on port 7000, BLE access control via `BlueToolFixup` `-btlfxadvsets`, and three iPhone-to-Mac video and 48 kHz buffered audio sessions over the local network `en0` with 0 audio glitches), along with what has not been tested yet (`en1`-only, Screen Mirroring, and P2P over `awdl0`).

## [2.7.91] — 2026-11-23

### Changed
- README: the Continuity Camera entry now gives the 2.7.90 status: 36–38 fps over 4.5 minutes, no firmware crash, home Wi-Fi usable while streaming, against 2.7.88's ~20 s home Wi-Fi drops at each firmware crash. It also lists what has not been tested yet, plus the ~20 s connect time and the need to keep Photo Booth in front. `-itlllw` was added to the "Boot-args and kexts for all Continuity features" list and table.

## [2.7.90] — 2026-11-23

### Added
- `debug.itlwm.awdl_p2p_flush` (default 1, read-write at runtime): the P2P queue is flushed before a MAC/PHY move only when at least this many frames wait; 0 turns the flush off. It allows an A/B test of the 2.7.89 flush without a reboot.
- `AWDLP2PFlushes` and `AWDLP2PFlushSkipped` properties count flushes done and skipped (frames were waiting but the flush was off or below the threshold).

### Fixed
- `iwx_p2p_setup` failure paths (stages 5, 6, 10, 11) now return ENXIO when a firmware reset happened during the command. Before, they restored `sc_p2p_shared` over the state `iwx_stop()` had cleared and counted a station error.

### Notes
- A/B test with Continuity Camera (Photo Booth, iPhone 16 Pro, 4.5 min in one stream): `awdl_p2p_flush` 1 gave 35.7 and 37.0 fps, 8 gave 37.1 fps and 0 gave 38.5 fps. Every phase had 0 `NMI_INTERRUPT_LMAC_FATAL` and 0 `DRIVER_RESET`, and the gateway ping stayed at a 0.5 ms median (p99 1 ms, no loss); 2.7.88 had a 450 ms median with 17% loss. The flush costs nothing measurable, so the default stays 1. The 9.5 fps seen in the 2.7.89 test did not reproduce. The 60 s phase with the flush off also stayed clean, so this test alone does not prove that the flush prevents the crash.

## [2.7.89] — 2026-11-23

### Fixed
- The iwx firmware asserted (`NMI_INTERRUPT_LMAC_FATAL`, then `DRIVER_RESET` and a ~20 s `en1` outage) during Continuity Camera. Every crash followed a P2P MAC binding/PHY change and SESSION_PROTECTION_CMD while the P2P broadcast station's TX queue (txq 3) still held frames. The queue is now flushed (TXPATH_FLUSH + ring advance) before the P2P MAC moves between the shared and the separate binding, before it leaves the station's binding when that binding is removed, and before the separate PHY changes channel. New counter field `sc_p2p_flushes`.

## [2.7.88] — 2026-11-22

### Fixed
- Unicast AWDL data could fill the station's own TX queue, which it shares with the station's management frames. In the 2.7.87 test with `awdl_share_phy` 0 and Continuity Camera streaming, that queue (txq 1) stayed at its high-water mark (896 of 1024 frames) for the whole stream. It drained at about 13 frames/s because of retries to the iPhone, and 36 AWDL submissions/s were rejected with ENOBUFS. The queue still held 707 frames 15 s after the camera closed. Unicast AWDL data now waits in `fDataQ` once that queue holds `debug.itlwm.awdl_home_qmax` [8] frames (0 = no limit). Broadcast AWDL action frames are not limited. New counter `AWDLTxFwHomeDefer`.

### Notes
- The same test with `awdl_share_phy` 1 (default) kept that queue at 1–3 frames, but gateway RTT on en1 still had a median of 464 ms (p90 1451 ms, max 2293 ms) at 38.7 fps. It fell to a median of 8 ms (p90 135 ms) 5–25 s after the camera stopped, so this fix does not address that latency. en1 also reassociated 4 times (link inactive, then active again 1–3 s later) around camera start and stop. `awdl_home_tx` 0 is not usable: it stalled the stream and dropped en1.

## [2.7.87] — 2026-11-22

### Added
- `debug.itlwm.awdl_home_tx` [1], a runtime switch to test the home Wi-Fi slowdown during wireless Continuity Camera. In 2.7.85/2.7.86 tests with `awdl_share_phy` 1, gateway RTT on en1 had a median of 350–515 ms, p90 1.5–2.7 s and max 2.3–3.8 s while streaming. With the camera off it was 3 ms (p90 69 ms). AWDL data frames sent on the infra channel outside a listen (37–46/s) go on the station's own TX queue (`first_data_qid`). The ones the iPhone misses are retried to the long limit (TX status 0x83), and station frames queue behind them. Setting `debug.itlwm.llw_tx` to 0 roughly halved the RTT (median 177 ms, p90 579 ms) but broke the camera stream. With `awdl_home_tx` 0, those data frames wait for the next AWDL listen, which uses the P2P queue. Management frames are not affected. The default stays 1 until a live A/B confirms it.

## [2.7.86] — 2026-11-22

### Changed
- `debug.itlwm.awdl_share_phy` now defaults to 1. In a 2.7.85 A/B, one Continuity Camera session was toggled live, with home Wi-Fi and AWDL both on channel 36 and `awdl_rt_cool` 1 / `awdl_rt_dwell` 64. With sharing on, llw0 received 1.8–1.9 Mbit/s (200–205 pkt/s) at 33–40 fps (VideoReceiver average 39–40, FrameCounter 33–37), with 44–55 missing RTP packets/s. With sharing off it received 0.7 Mbit/s (85 pkt/s) at 5–9 fps, with 78 missing/s. The two 60 s sharing runs made 576 shared listens, with 0 `AWDLListenShareFail` and no real link-down. Gateway ping lost 0–3.9% in both modes, but averaged 0.5–0.9 s while streaming (about 2 ms with the camera off). The 2.7.84 home Wi-Fi drop came from back-to-back listens (`awdl_rt_cool` 0), not from sharing.
- README: Continuity Camera status now gives the shared-binding frame rate and the higher home Wi-Fi latency while streaming.

## [2.7.85] — 2026-11-22

### Fixed
- Home Wi-Fi dropped during wireless Continuity Camera on 2.7.83–2.7.84. The firmware accepted the shared binding (318 shared listens, `AWDLListenShareFail` 0). But shared home-channel listens also ran back to back (cooldown forced to 0), and `en1` got a link-down indication 6 s after the first camera frame (involuntary leave, enhanced disassoc reason 11). With `debug.itlwm.awdl_share_phy=0` the same stream kept home Wi-Fi up (0% gateway ping loss) at the same ~1 Mbit/s. `awdl_share_phy` now defaults to 0. Shared listens no longer force cooldown 0; they use `debug.itlwm.awdl_rt_cool` like separate-PHY listens, so sharing and back-to-back listens can be tested separately.

## [2.7.84] — 2026-11-22

### Fixed
- `iwx_p2p_setup`: when leaving a shared station binding (`!share && sc->sc_p2p_shared`), check that the station binding is active and `in->in_phyctxt` is non-null before issuing `BINDING_CONTEXT_CMD` MODIFY, and restore `sc->sc_p2p_shared = 1` if MODIFY fails so subsequent off-channel listens don't skip unsharing and hit repeated P2P binding errors.

## [2.7.83] — 2026-11-22

### Fixed
- Continuity Camera frame rate on a shared infra/AWDL channel. While streaming, airportd puts both the Mac and the iPhone on a high-throughput AWDL sequence with 14 of 16 slots on the infra channel (36), but our AWDL listens ran the P2P MAC on its own PHY context. Even on the same channel, the firmware time-shared that PHY with the station's. So each listen cost the station airtime, needed a cooldown (≤50% duty, 20–90 packet gaps), and back-to-back listens (`awdl_rt_cool` 0) deauthenticated the station. When the listen channel is the station's channel, the P2P MAC is now added to the station's binding (`BINDING_CONTEXT_CMD` MODIFY with both MACs, like iwlwifi's ROC on an already-bound PHY), so a listen needs no channel switch. Realtime (llw0) home listens then run back to back. Off-channel listens still use the separate P2P PHY, and the P2P MAC moves between the two bindings as needed. If the MODIFY fails, the driver falls back to the separate PHY. `debug.itlwm.awdl_share_phy` [1] (0 = old behaviour). New counters `AWDLListenShared`, `AWDLListenShareFail`.

## [2.7.82] — 2026-11-22

### Fixed
- Continuity Camera packet gaps: the aligned AWDL listen scheduler followed the peer's schedule only for non-home channels, so the iPhone's home-channel slots were skipped whenever our own (low-power) sequence had 0 there. In the 2.7.81 test the iPhone advertised ` 36 36 149 0 0 0 0 36 6 36 149 36 0 0 0 36` (6/16 slots on home channel 36) and we advertised ` 36 0 149 0 0 0 0 0 6 0 149 0 0 0 0 0` (1/16). That gave about 2.8 home listens/s of 16 TU (about 4.5% duty) and 20–90 packet RTP gaps every 100–300 ms (macOS `DetectMissingPackets`). In realtime mode (llw0 active) the scheduler now also listens in the peer's home-channel slots, and sizes each home listen to the whole run of slots where either schedule is on the home channel (still capped by `awdl_rt_dwell` and its cooldown). New counter `AWDLListenPeerHome`.

## [2.7.81] — 2026-11-22

### Fixed
- `awdlDefrag`: pull up up to 64 contiguous bytes on the reassembled mbuf before returning it to `inputAWDLData`. When fragment 0 was shorter than 64 bytes and subsequent fragments were appended via `mbuf_copyback`, `mbuf_len(full)` remained at fragment 0's length instead of `mbuf_pkthdr_len(full)`, which could cause `inputAWDLData` to reject a valid reassembled frame as `AWDLDataRxBadShort`.

## [2.7.80] — 2026-11-22

### Fixed
- Continuity Camera packet loss: the iPhone splits some AWDL video frames into 802.11 fragments. The AWDL RX path did not reassemble them. The first fragment was dropped (an MSDU with no SNAP/subframe header after the cut) and later fragments were rejected as `AWDLDataRxBadSnap` (about 130 per 30 s of streaming: QoS TID 7, unprotected, high-entropy payload with no LLC header). Every lost fragment lost a video packet, so frames failed to reassemble and the iPhone fell back to keyframes at a lower bitrate. Unicast AWDL data is now defragmented (one slot keyed by TA/TID/seq, in-order fragments only, 100 ms timeout, 8 KB cap). New counters `AWDLDataRxFrag`, `AWDLDataRxDefrag`, `AWDLDataRxFragDrop`, `AWDLDataRxFragDup`.
- iwx AWDL unicast RX dedup ignored the fragment number, so a retried fragment N+1 that followed fragment N with the same seq could be dropped as a duplicate. It now matches on seq and fragment number.

### Changed
- `llw0` RX: `kIOReturnOffline` enqueue results are counted separately (`LLWRxOffline`, `LLWRxOfflineBursts`, `LLWRxOfflineLastIn`) instead of in `LLWRxEnqueueFail`. They come in short bursts (about 65–70 frames), most likely while IOSkywalkFamily attaches the nexus at session start; this boot had 0 in 96,000 frames.
- README: Continuity Camera now documents wireless support through `llw0` (`-itlllw`), its tunables and the current frame-rate limit.

## [2.7.79] — 2026-11-22

### Fixed
- Home Wi-Fi dropped during Continuity Camera with the 2.7.78 defaults (`awdl_rt_cool` 0, `awdl_rt_dwell` 128). Back-to-back home-channel AWDL listens starved the station: NET_MANAGER LINK_DOWN 25 s into the stream, then the camera session ended. Defaults are back to cool 1 / dwell 64; the 2.7.78 fix that applies the realtime policy even with airportd peer traffic stays. 2.7.78 A/B (30 s each): cool 1 / dwell 64 gave 1.15 Mbit/s; cool 3 gave 1.22; cool 2 / dwell 64 and cool 1 / dwell 96 were lower (cool 2 also had one link-down), and none fixed the frame rate. Listen duty is not the limit.

## [2.7.78] — 2026-11-21

### Fixed
- Continuity Camera realtime listen policy never applied: in the 2.7.77 test airportd did set `AWDLPeerTrafficActive` = 1 for the session, and 2.7.77 only used the realtime policy when it was 0. Changing `debug.itlwm.awdl_rt_cool` therefore had no effect, and the stream ran at about 0.85 Mbit/s and 3 fps. Now llw0 activity alone selects the policy.

### Changed
- Realtime defaults: `debug.itlwm.awdl_rt_cool` 0 (back-to-back home-channel listens) and `debug.itlwm.awdl_rt_dwell` 128 TU. Both the iPhone and the Mac advertise 14 of 16 slots on the infra channel, in two 7-slot (112 TU) runs per AWDL period, and the aligned scheduler caps each listen to its run.

## [2.7.77] — 2026-11-21

### Changed
- Continuity Camera frame rate: while `llw0` carries traffic (frames either way in the last 2 s), AWDL listens on the infra channel use a realtime policy. Continuity Camera frames arrive only during these listens, which were capped at 64 TU with a 3x cooldown (about 25% duty), because airportd never sets peer traffic for Continuity Camera. The cooldown is now `debug.itlwm.awdl_rt_cool` x the dwell (default 1; 0 = back to back, max 8), and the listen can last up to `debug.itlwm.awdl_rt_dwell` TU (default 64, range 16–512). Both apply live. Off-channel listens are no longer held for infra while llw0 is active. New counter `AWDLListenHomeRT`.

## [2.7.76] — 2026-11-21

### Fixed
- `llw0` TX bridge: free the allocated mbuf and count `LLWTxForwardFail` instead of `LLWTxForwarded` when `_fCommandGate->runAction` returns an error without invoking the enqueue action; explicitly initialize `fQueuesEnabled` in `withController`.

## [2.7.75] — 2026-11-21

### Fixed
- `llw0` RX: the IOSkywalk TX/RX queues are now enabled after registration and disabled before teardown, as `AppleBCMWLANSkywalkInterface::enableDatapath` does. `IOSkywalkPacketQueue::initWithPool` leaves them disabled. In the 2.7.74 test every llw0 frame (73088) failed with `kIOReturnOffline` (`LLWRxEnqueueFail`), and `txAction` was never called.

### Added
- `llw0` TX bridge: frames `llw0` transmits are copied into mbufs and sent to the AWDL peer through the AWDL data queue (same dwell/slot timing as awdl0), as 802.11 data with RFC 1042 SNAP and the frame's own ethertype (the iPhone's llw0 layout), without the AWDL data header. Switch `debug.itlwm.llw_tx` (default 1; 0 drops them as before). New counters `LLWTxForwarded`, `LLWTxForwardFail`, `AWDLDataTxLLW`, and property `LLWQueuesEnabled`.

## [2.7.74] — 2026-11-21

### Fixed

- `llw0` RX bridge: in the 2.7.73 test every received camera subframe was routed to `llw0` (`LLWRxIn` 15744, all to IPv6 ULA addresses), but every one failed as `LLWRxNoBuf`. `IOSkywalkPacketBufferPool::allocatePacket` only binds a kernel packet; the `IOSkywalkPacketBuffer` objects, and so the data address, are attached by `IOSkywalkPacket::prepareWithQueue`, as `IOSkywalkRxSubmissionQueue::refillRxRing` does. The RX path now prepares each packet (direction RX, no source queue) before writing it. With no source queue, completion skips the source queue's `packetCompletion` callback, which the base `IOSkywalkPacketQueue` implements as a panic. New counter `LLWRxPrepareFail`.
- `llw0` RX bridge: plain (non-A-MSDU) llw0 frames are now routed too. In the 2.7.73 test, 1455 frames were still dropped as `AWDLDataRxBadSnap`; they carry RFC 1042 SNAP + IPv6 (`aa aa 03 00 00 00 86 dd`) right after the 802.11 header. They are delivered with DA = addr1 and SA = addr2. New counter `AWDLDataRxLLWMsdu`.

## [2.7.73] — 2026-11-21

### Added

- `llw0` (`-itlllw`) RX bridge. The iPhone sends Continuity Camera media to the shared `awdl0`/`llw0` MAC as QoS data frames in A-MSDU subframe layout with standard RFC 1042 SNAP (`aa aa 03 00 00 00` + EtherType), while AWDL frames carry Apple's SNAP (`00 17 f2 08 00`). iwx clears the QoS A-MSDU bit, so 2.7.72 dropped all of them as `AWDLDataRxBadSnap`. `inputAWDLData` now recognises these frames, rebuilds each subframe as an Ethernet frame in a packet from `llw0`'s pool, and hands it to `llw0`'s Skywalk RX completion queue. The RX path never blocks: it takes the `llw0` pointer with `IOLockTryLock` and the queue's workloop gate with `tryCloseGate`, and drops the frame (counted) when either is busy.
- Counters on the AWDL interface: `AWDLDataRxLLW`, `AWDLDataRxLLWSub`, `AWDLDataRxLLWNoIf` and `AWDLDataRxLLWBusy`. On the `llw0` node: `LLWRxIn`, `LLWRxPackets`, `LLWRxBytes` and `LLWRxULA`, plus the failure counters `LLWRxNoPacket`, `LLWRxNoBuf`, `LLWRxTooBig`, `LLWRxCopyFail`, `LLWRxSetupFail`, `LLWRxNotReady`, `LLWRxGateBusy`, `LLWRxEnqueueFail` and `LLWRxLastError`. The first 8 delivered frames are logged in hex (`AirportItlwm: LLW rx`). These frames also feed `AWDLRxFlows` and `AWDLDataRxULA`.

### Changed

- The `llw0` TX action returns packets to the pool through the exported `IOSkywalkPacketBufferPool::deallocatePacket` symbol instead of the SDK's outdated vtable.

## [2.7.72] — 2026-11-21

### Added

- `llw0` (`-itlllw`), observation only, groundwork for the AWDL bridge. The TX action now records what the stack submits on `llw0` before returning each packet to the pool untransmitted, as before: `LLWTxCalls`, `LLWTxPackets`, `LLWTxBytes`, `LLWTxMultiBuf`, `LLWTxNoBuf` and `LLWTxNonIP`, plus `LLWTxFlows` (up to 8 IP flows) on the `AirportItlwmLLWInterface` node. The first 8 frames are logged in hex (`AirportItlwm: LLW tx`). Packet data is read through the exported `IOSkywalkPacket` accessors (`getPacketBufferCount`, `getDataVirtualAddress`, `getDataOffset`, `getDataLength`), called by symbol rather than through the SDK's outdated vtable.
- AWDL RX: `AWDLRxFlows` lists the 12 most recently seen unicast IP flows received over AWDL (protocol, addresses, ports, packets, bytes). `AWDLDataRxULA` counts frames sent to IPv6 ULA (fc00::/7) addresses, which low-latency `llw0` flows use. `AWDLRxFlowPkts`, `AWDLRxFlowNew` and `AWDLRxFlowReplaced` are also published.

## [2.7.71] — 2026-11-20

### Changed

- `llw0` (`-itlllw`): after reporting the link status, also set the interface link state to up with `setLinkState(kIO80211NetworkLinkUp, ...)`, as the station does. `ifconfig` gets `status: active` from `IO80211InfraInterface::updateMediumStatus`, which only checks that link state, so `reportLinkStatus` alone (2.7.70) left `llw0` inactive. For a LowLatency-role interface, `setLinkStateInternal` reports the link status, link speed and bandwidths, and skips the IPv6 enable and the peer manager. `LLWStatus` now also shows the `setLinkState` result.

## [2.7.70] — 2026-11-20

### Added

- `llw0` (`-itlllw`): report the link as active, the way `AppleBCMWLANLowLatencyInterface::setInterfaceEnable` does (`IOSkywalkNetworkInterface::reportLinkStatus` with status 3 = active and medium 0x80 = IEEE 802.11). `IOSkywalkNetworkBSDClient` creates the `llw0` ifnet asynchronously after `registerService()`, and `reportLinkStatus` returns `kIOReturnNotReady` until then, so the LLW worker thread waits up to about 3 s for the ifnet. `LLWStatus` now ends with the link result. No packets flow through `llw0` yet.

### Changed

- The `IOSkywalkNetworkInterface::reportLinkStatus` declaration now returns `IOReturn`, like the kernel function, so the result can be checked. The mangled name is unchanged.

## [2.7.69] — 2026-11-20

### Fixed

- `llw0` (`-itlllw`): 2.7.68 created `llw0`, but `airportd`/CoreWLAN made it the default Wi-Fi interface and Wi-Fi was lost (every Apple80211 ioctl on `llw0` failed). IO80211.framework's `Apple80211GetIfListCopy` only lists interfaces whose `IO80211InterfaceRole` registry property is `"Infrastructure"`, which is how a real Mac keeps `llw0` hidden. `IO80211SkywalkInterface::setInterfaceRole` maps role 1 to `Infrastructure` and role 2 to `LowLatency`; role 9, which we used, is the NAN role `WiFi-Aware Discovery+Data` under `IO80211VirtualInterfaceRole` and left `IO80211InterfaceRole` at `Infrastructure`. `llw0` now uses role 2, and the role is set again just before `registerService()`. `LLWStatus` reports the published role.

## [2.7.68] — 2026-11-20

### Fixed

- `llw0` (`-itlllw`): 2.7.67 registered the datapath (`LLWStatus = created`, no panic) but `llw0` never appeared, because the interface was never published with `registerService()`, so `IOSkywalkNetworkBSDClient` (which creates the ifnet and nexus) never matched it. It now calls `registerService(kIOServiceAsynchronous)` after registration, as `AppleBCMWLANSkywalkInterface::start` does, and teardown uses `terminate()` like `AppleBCMWLANCore::destroySkywalkInterface`.
- `llw0` uses `awdl0`'s MAC address, so `initBSDInterfaceParameters` now gives the ifnet its own 8-byte unique id (the object address), as `AppleBCMWLANLowLatencyInterface` does.
- `stop()` removes the queue event sources and lets the family's stop deregister the interface, instead of calling `deregisterEthernetInterface` separately.

## [2.7.67] — 2026-11-20

### Fixed

- `llw0` (`-itlllw`): `IO80211InfraInterface::registerInfraEthernetInterface` returns the `IOReturn` of `registerEthernetInterface` (`mov eax, r14d`), not a `bool`. It was declared `bool`, so success (`0`) was read as failure and 2.7.66 reported `LLWStatus = "datapath failed: registerInfraEthernetInterface returned false"` and tore down a registration that had worked. The result is now compared with `kIOReturnSuccess`.

## [2.7.66] — 2026-11-20

### Fixed

- `llw0` (`-itlllw`): `LLWStatus` on 2.7.65 reported `datapath failed: queue create failed`. The Skywalk queue `withPool` factories take `(pool, capacity, extra, owner, action, refcon, options)`; the queue id slot we assumed does not exist. `initWithPool` rejects a capacity below 8 (TX) / 2 (RX), and we passed `0` as the capacity. Now TX and RX queues are created with capacity 64.

## [2.7.65] — 2026-11-20

### Added

- `llw0` (`-itlllw`): the AWDL interface now publishes an `LLWStatus` property in the I/O Registry (`requested`, `attach failed`, `start failed`, `datapath failed: <step>`, `created`), because `IOLog` output from the kext does not reach `dmesg` here. 2.7.64 no longer hit the PM timeout (`start()` completed) but the interface was stopped again within 1 ms, which points at the datapath setup.

## [2.7.64] — 2026-11-20

### Fixed

- `llw0` (`-itlllw`) async worker races: synchronize `fLLWIf` / `fLLWBsdName` under `fLLWLock` to fix a use-after-free in `setCOMPANION_SKYWALK_IF_CREATE`; call `retain()` + `thread_call_enter` inside `fLLWLock` and synchronize teardown with `thread_call_cancel` / `IOLockSleep` / `thread_call_cancel_wait` instead of a 30 s polling timeout that could race `free()`; run `fLLWCall` at `THREAD_CALL_PRIORITY_KERNEL` instead of `HIGH`.

## [2.7.63] — 2026-11-20

### Fixed

- `llw0` (`-itlllw`): create and destroy now run on a `thread_call` instead of inside the `COMPANION_SKYWALK_IF_CREATE` ioctl. 2.7.62 showed `AirportItlwmLLWInterface::start` waiting 13 s for a power-management callout (`waitForPMDriverCall timeout`) and being torn down, most likely because the callout needs the controller workloop that the ioctl was holding. The ioctl now returns at once with `llw0`; requests are coalesced and teardown waits for the worker.

## [2.7.62] — 2026-11-20

### Changed

- `llw0` (`-itlllw`) diagnostics now use `IOLog` instead of `kprintf` (`XYLog`), so they appear in `dmesg`/the unified log; the `COMPANION_SKYWALK_IF_CREATE` handler logs on entry.

## [2.7.61] — 2026-11-20

### Fixed

- **AWDL / `llw0` companion Skywalk interface (`-itlllw`)**: fixed seven setup and teardown defects found against `AppleBCMWLANCoreMac` / `IO80211Family` disassembly:
  - Handle `enable == 0` at `+0x04` of `apple80211_companion_skywalk_if_create` to tear down `llw0` instead of treating `+0x00..+0x07` as an opaque header.
  - Set interface role `9` (instead of vendor init type `2`) and override `IOSkywalkNetworkInterface::getBSDNamePrefix()` to return `"llw"` (instead of inheriting `"en"`).
  - Call `setInitMacAddress`, `setParentInterface(this)`, and `IO80211InfraInterface::start(fController)` before datapath registration, and link `setCompanionLowLatencySkywalkInterface(llw)` on `awdl0`.
  - Declare `IOSkywalkPacketQueue` as an `IOEventSource` subclass and attach/detach `fTxQ` and `fRxQ` on the interface workloop (`addEventSource` / `removeEventSource`).
  - Populate `RegistrationInfo` fields (`+0x0c`, `+0x14`, `+0x24`, `+0x28`, `+0x30 = "llw"`, `+0x3c`, `+0x40`, `+0x108` MAC) and register via `IO80211InfraInterface::registerInfraEthernetInterface` instead of calling `registerEthernetInterface` directly.
  - Deregister the Skywalk interface (`deregisterEthernetInterface(0)`), remove queue event sources, stop, detach, and unlink companion pointers in `destroyLLWInterface()` (invoked on `enable == 0`, `destroyAWDLInterface()`, and `free()`).

## [2.7.60] — 2026-11-20

### Added

- **AWDL / Continuity Camera (experimental, off by default)**: stage-2 `llw0` low-latency Skywalk companion interface (`AirportItlwmLLWInterface`, an `IO80211InfraInterface`) created from `COMPANION_SKYWALK_IF_CREATE` only when the `-itlllw` boot-arg is set. It owns one `IOSkywalkPacketBufferPool`, one TX submission queue and one RX completion queue, registers them with `registerEthernetInterface`, then sets the companion interface id and low-latency mode. No AWDL data is routed through it yet (TX packets are returned to the pool, RX returns 0). Not yet tested on hardware. Adds `include/Airport/IOSkywalkPacketQueue.h`.

### Fixed

- `IOSkywalkEthernetInterface::registerEthernetInterface` is declared as returning `IOReturn` (it returned `bool`, which inverted success and failure); `IOSkywalkPacketBufferPool` methods are non-virtual exports, so calls no longer go through reserved vtable slots.

## [2.7.59] — 2026-11-19

### Fixed

- **AWDL / Continuity Camera**: Report card capability 142 (`capabilities[17] |= 0x40`) on macOS 26 so `IO80211Peer::getLastRxUnicastLinkActivity()` and `getLastQueuePacket()` actually query `AirportItlwmAWDLInterface` instead of skipping the vtable call; also implement `getLastQueuePacketTime` (`0xe18`) alongside `getLastRxUnicastLinkActivityTime` (`0xe20`) and check frame length and destination MAC before recording unicast RX activity.

## [2.7.58] — 2026-11-19

### Fixed
- AWDL: override `IO80211SkywalkInterface::getLastRxUnicastLinkActivityTime` (vtable 0xe20). The
  base returns 0, so IO80211Family's `monitorCachedPeerActivity` saw every peer as idle
  ("Cache REMOVAL of AirPlay Peer ... due to inactivity pkts:0 rxu:5044ms") and evicted the
  iPhone during Continuity Camera streaming despite thousands of RX frames. The driver now records
  a per-peer mach_absolute_time on each unicast AWDL data RX and returns it. Not yet verified on
  hardware.

## [2.7.57] — 2026-11-19

### Fixed
- AWDL: fix inverted status byte and payload sizes in `flushPeerCacheDone` (2.7.56). In
  `apple80211_actionFrame_status` (event 69, 7 bytes), byte `+6` is a status code where `0` means
  acked (`WLC_E_STATUS_SUCCESS`) and non-zero means `'DID NOT ack'` (causing 10 UMI retries and
  `EnsembleUMIStatus = 3` failure). Also restore the full `0x1d0`-byte buffer size for event 51
  (`apple80211_peer_cache_control`).

## [2.7.56] — 2026-11-19

### Fixed
- AWDL: report unicast action-frame TX status (event 69, peer MAC + acked flag) after a unicast
  OOB/MI frame is transmitted. IO80211Family's `sendUnicastMI` waits 3 s for it
  (`EnsembleUMIStatusDone`); without it Continuity Camera's RemoteCamera traffic registration
  failed with "Ensemble start Failure, waiting for UMIStatus umi: b000000". Reuses the 2.7.55
  deferred-event queue (now tagged with the event id). Not yet verified on hardware.

## [2.7.55] — 2026-11-19

### Fixed
- AWDL: complete the peer-cache handshake with IO80211Family. After `setPEER_CACHE_CONTROL`
  (add = op 0, delete = op 2) the family sleeps up to 3 s in `waitForPeerManagerEvent` for
  firmware to report completion (event 51, op+1), which the driver never sent. Every
  RemoteCamera traffic registration therefore stalled ~6 s, then failed with "peer cache is
  full", so wireless Continuity Camera hit `stream activation timeout`. The driver now echoes
  the completion from a timer (after the ioctl returns). New IORegistry property
  `AWDLPeerCacheDone`. Not yet verified on hardware.

## [2.7.54] — 2026-11-19

### Fixed

- README: moved the "Boot-args and kexts for all Continuity features" subsection after the
  macOS 26 feature-status list so Continuity Camera, iPhone Mirroring, and device-support
  bullets are no longer rendered inside the "Bluetooth kexts used" list. Documentation only.

## [2.7.53] — 2026-11-19

### Changed

- README: new "Boot-args and kexts for all Continuity features" section: the tested boot-arg
  set (Wi-Fi AWDL, Lilu, IntelBTPatcher, BlueToolFixup), what each is for, links to the
  BlueToolFixup (`-btlfxadvsets`) and IntelBluetoothFirmware forks, and how to give
  IntelBTPatcher the iPhone IRK for Instant Hotspot. The results were measured with BlueToolFixup
  2.7.5 (branch is at 2.7.7, untested here) and IntelBTPatcher 2.6.1. Documentation only.

## [2.7.52] — 2026-11-19

### Changed

- README: the install section's "this fork's Releases" link now points at
  minhnhan-tran/AirportItlwm instead of upstream laobamac/itlwm. Documentation only.

## [2.7.51] — 2026-11-19

### Changed

- README: the Downloads badge now follows this repository's latest release (prereleases
  included) and links to its releases page; it pointed at the upstream laobamac/itlwm tags
  (v2.4.0-alpha). Documentation only.

## [2.7.50] — 2026-11-18

### Changed

- README: the Instant Hotspot section now says both bands work (5 GHz with Maximize
  Compatibility off, 2.4 GHz with it on). Documentation only.

## [2.7.49] — 2026-11-18

### Changed

- README: documented iPhone Instant Hotspot and AirDrop over AWDL (what works, the
  sysctl switches and their defaults, measured speeds, and how to force AWDL for a test).
  Documentation only.

## [2.7.48] — 2026-11-18

### Changed

- `debug.itlwm.awdl_dpstats` now defaults to 1 (2.7.47 shipped it off). Hardware test, iPhone to
  Mac, 100 MB over AirDrop with the Mac on 5 GHz channel 36: off gave 3.7 Mbit/s average
  (4.7 peak); on gave about 8 Mbit/s average (12.3 peak), with the Family's data-mode monitor
  reporting `indatamode 1` and `airportd` applying the high-throughput channel sequence.
  106 calls, all returning 0, no panic. Set it to 0 to turn it off.

## [2.7.47] — 2026-11-18

### Added

- AirDrop speed, part 2: a switch to test, off by default. `debug.itlwm.awdl_dpstats=1`
  pushes the driver's AWDL data frame counts to IO80211Family about every 260 ms, through
  `IO80211SkywalkInterface::updateInterfaceDataStats`.
  - The Family's AWDL data-mode monitor (`IO80211AWDLPeerManager::monitorAWDLDataMode`,
    about 1 Hz) moves the Mac to the high-throughput channel sequence only when its
    peer-manager Tx/Rx counters move.
  - Two paths fill those counters: the Family's own peer-manager packet path, or this call
    (through `IO80211PeerManager::updateLocalCounters`). This driver used neither, so in the
    2.7.45 AirDrop test the monitor logged Tx/Rx 0 throughout and the Mac stayed on its
    low-power sequence for 481 of 491 s.
  - Only the Tx and Rx packet fields (+0x08, +0x18) are filled; every other field stays zero.
  - `AWDLPairStats` gains `dps=<calls>/<last return>`. The first two calls and any failures
    are logged as `AirportItlwm: AWDL dp stats`.

## [2.7.46] — 2026-11-18

### Changed

- `debug.itlwm.awdl_infra_nth` defaults to 1, so no off-channel AWDL listen is held while AWDL
  wants data. 2.7.45 test (home Wi-Fi on 5 GHz ch 36, AWDL on ch 149): with the old default
  of 2, the infra hold dropped about half of the Mac's own off-channel listens
  (AWDLListenInfraHold +256 vs AWDLListenStarted +267 in 94 s), and iPhone-to-Mac AirDrop
  stalled 2 of 2 times. With 1, it completed, though slowly (about 1.5 Mbit/s), because
  airportd keeps the Mac on its low-power channel sequence. Without AWDL data the hold is
  unchanged; set it to 2 or more for the old behaviour.

## [2.7.45] — 2026-11-18

### Added

- AWDL slot-pairing diagnostics, to explain the AirDrop speed swings (fast on some transfers,
  stalled on others) with the Mac on Wi-Fi on another channel (home 5 GHz ch 36). The
  `AWDLPairStats` property of AirportItlwmAWDLInterface (refreshed about once a second)
  classifies every AW slot by our channel and the peer's predicted channel:
  - `cOff`: both on the same channel, not the infra channel
  - `cHome`: both on the infra channel
  - `pHome`: the peer is on the infra channel while our slot is idle
  - `pElse`: the peer is on another channel
  - `own`: only we are on a channel
  - `idle`: neither of us is on a channel
  - `unk`: no peer timeline yet

  Each class reports slots / slots with any AWDL RX / data frames received / data frames sent.
  `qmax` is the deepest AWDL data TX queue since the last refresh. No log lines.

## [2.7.44] — 2026-11-18

### Fixed

- AirDrop from the Mac to an iPhone while the Mac is on Wi-Fi on another channel than AWDL's
  (2026-11-18 test: home 5 GHz ch 36, AWDL ch 149; iPhone to Mac worked, Mac to iPhone failed).
  The 2.7.33 infra hold drops every off-channel AWDL listen while associated unless airportd
  reports peer traffic, which Tahoe never does (AWDLListenInfraHold 5377 in one session), so
  the Mac was never on the iPhone's channel to transmit. New `debug.itlwm.awdl_infra_nth`
  (default 2): while AWDL wants data, 1 in every n off-channel listens goes out (0 = hold all
  as in 2.7.33, 1 = none held). Without AWDL data, the hold and the `itlawdlinfranth=<n>`
  boot-arg are unchanged.

## [2.7.43] — 2026-11-18

### Removed

- Instant Hotspot investigation leftovers, now that 2.7.42 fixed the click:
  - The 2.7.39 `setIE` path (`ic_assoc_appie`). airportd never calls `APPLE80211_IOC_IE` on
    Tahoe, and the Instant Hotspot IE is built in (`debug.itlwm.ihs_ie`). Skywalk `setIE` answers
    kIOReturnUnsupported again.
  - The 2.7.40 `AirportItlwm: WCL join` logs.
  - The 2.7.29/2.7.30 hex dumps of (Re)Association Requests/Responses and the BSS RSN IE.

### Changed

- One summary line per (Re)Association Request remains, now with `ihs=` (Instant Hotspot IE
  flags byte, -1 = not sent), plus one status line per (Re)Association Response.

## [2.7.42] — 2026-11-18

### Fixed

- Instant Hotspot click from Control Center now joins. `debug.itlwm.ihs_ie` defaults to 1, so
  every (Re)Association Request carries Apple's Instant Hotspot device IE (`dd 0b 00 17 f2 0b
  00 01 04 01 00 00 00`); -1 turns it off. Without it, the iPhone accepts the association but
  never starts the 4-way handshake, and airportd gives up with -3905 / INVALID_AKMS. Tested on
  2.7.41 (Tahoe, iPhone hotspot with Allow Others to Join and Maximize Compatibility off):
  - IE on: 2 of 2 clicks joined on the first association. The 4-way handshake completed, IP in
    5–10 s, 0% ping loss, HTTPS 200.
  - IE off (same hotspot session): 5 associations, no EAPOL, Join FAILED -3905.
  - IE on with a home WPA2-Personal router (5 GHz ch 36): joined, 0% loss, HTTPS 200.
  Real Macs send this IE only for Instant Hotspot joins. Standard APs ignore unknown vendor
  IEs, so it is sent on every join.

## [2.7.41] — 2026-11-18

### Added

- Instant Hotspot click, part 11: `debug.itlwm.ihs_ie` (default -1, off) adds Apple's Instant
  Hotspot device IE to (Re)Association Requests. It's the IE that real Macs build for an
  Instant Hotspot join (IO80211Family `IO80211_GetInstantHotSpotIE`, AppleBCMWLANCoreMac
  `AppleBCMWLANJoinAdapter::addInstantHSAppleDeviceIE`): vendor IE `dd 0b 00 17 f2 0b 00 01 04
  <flags> 00 00 00`. The sysctl value is the flags byte (IO80211 builds `bit0 | bit1 << 1` from
  two bools). AirportItlwm never sent it, a likely reason the iPhone accepts the association
  but never starts the 4-way handshake. Set it live (`sudo sysctl debug.itlwm.ihs_ie=1`)
  before a Control Center click; the `assoc_req` dump shows it as `221/11:0017f20b`.

## [2.7.40] — 2026-11-18

### Added

- Instant Hotspot click diagnostics (WCL): `AirportItlwm: WCL join` logs what airportd asks
  for in each join (lower/upper auth mask, classification, key length and cipher, channel,
  BSSID, RSN IE; no key or SSID bytes). `AirportItlwm: WCL join done` logs the status, reason
  and driver status reported back. A third log line appears when the BSS being joined
  (WPA2/WPA3 transition, PSK+SAE) was reported to airportd as WPA2-PSK only.

### Changed

- 2.7.39 note: airportd never calls `APPLE80211_IOC_IE` on Tahoe, at boot or during the
  Instant Hotspot click, so the 2.7.39 `setIE` path is unused and the click still fails. In
  every attempt the iPhone accepts the association but never sends EAPOL msg 1/4; neither
  side sends a deauth, and airportd records `INVALID_AKMS`.

## [2.7.39] — 2026-11-18

### Fixed

- Instant Hotspot click, part 10. On Sequoia/Tahoe (Skywalk) the driver answered every
  `APPLE80211_IOC_IE` (`setIE`) with kIOReturnUnsupported, so no IE airportd wanted in a
  (Re)Association Request was ever transmitted. That matches the click symptom: the iPhone
  accepts the association but never sends EAPOL msg 1/4, while auto-join and password joins to
  the same hotspot complete the 4-way handshake. ASSOC_REQ vendor IEs (except Microsoft
  00:50:f2, which net80211 already builds) are now kept, replaced by OUI and type, removed by
  add=0, and appended to every (Re)Association Request. Every call is logged as `AirportItlwm: setIE` (flags, add,
  signature length, IE ids/lengths and vendor OUI-type, no payload). The existing `assoc_req`
  dump shows them on the air.

## [2.7.38] — 2026-11-18

### Changed

- `debug.itlwm.awdl_home_listen` now defaults to 2 (listen on the shared infra channel whenever
  AWDL wants data). Mode 1 waits for AWDL peer traffic, which airportd never registered during
  real AirDrop transfers. In the 2026-11-18 test, `AWDLPeerTrafficActive` stayed 0 and
  `AWDLPeerTrafficRegs` did not change, so mode 1 behaved exactly like 2.7.36 (AirDrop failed
  both ways). With mode 2 on the iPhone hotspot (channel 149, shared with AWDL), AirDrop
  Mac→iPhone and iPhone→Mac both succeeded. During the transfers: `AWDLListenHome` +246, AWDL
  data RX +1,822 and TX +1,785 frames. Hotspot ping every 3 s: 0/79 lost, avg 15.7 ms, max
  159 ms. Idle with mode 2: 0% loss, avg 7.0 ms (5.5 ms with mode 1).

### Notes

- The 2.7.33 off-channel listen hold uses the same `AWDLPeerTrafficActive` signal. AirDrop
  while associated to an infra network on a different channel from AWDL is therefore likely
  still blocked (untested).

## [2.7.37] — 2026-11-18

### Fixed

- AirDrop between the Mac and the iPhone failed in both directions while the Mac was on the
  iPhone's hotspot. The hotspot (5 GHz channel 149) shares a channel with AWDL, and on a shared
  channel the driver never scheduled AWDL listens. AWDL data reached the Mac only through the
  station-MAC promiscuous filter that 2.7.36 turned off. With `awdl_bss_promisc=0`,
  Bonjour-over-AWDL discovery timed out and no AWDL data arrived. With `=1`, the iPhone was
  found but the hotspot dropped to 100% loss. AWDL now also listens on the infra channel, in
  its own time event: at most 64 TU per listen, then at least as long again on the station
  (three times as long outside peer traffic). `debug.itlwm.awdl_home_listen` selects when:
  0 never, 1 while AWDL peer traffic is registered (default), 2 whenever AWDL wants data
  (also covers discovery). The count shows as `AWDLListenHome`.

### Changed

- Correction to 2.7.36: the station-MAC promiscuous filter stalls station TX (114 frames
  submitted, 45 completions in the 2026-11-18 test). It does not cause CCMP decrypt errors
  (0 in that test).

## [2.7.36] — 2026-11-18

### Fixed

- iPhone Personal Hotspot traffic died (100% ping loss, no DHCP lease) while awdl0 was up.
  Root cause: with `-itlawdlpromisc`, the station MAC context got `IWX_MAC_FILTER_IN_PROMISC`
  whenever AWDL wanted data. With that filter set, the associated BSS's traffic stops getting
  through: CCMP decrypt errors climb and a TX queue backs up. The 2.7.35 switches bisected it on
  one boot. `awdl_home_tx=0` and `awdl_tx=0` changed nothing. `awdl_bss_promisc=0` plus a
  Wi-Fi off/on gave 0% loss (gateway about 3.5 ms, 1.1.1.1 60–170 ms, HTTPS 200) and a DHCP
  lease within 3 s. Turning it back on broke the link again. Each direction was reproduced
  three times. Station-MAC promisc is now off by default. The P2P MAC keeps its own
  promiscuous filter for AWDL. `debug.itlwm.awdl_bss_promisc=1` turns it back on for AWDL
  experiments; it still needs `-itlawdlpromisc`.
- The 2.7.33 listen hold and the 2.7.34 P2P PHY park targeted this loss but did not fix it.
  Both are still in place; boot-arg `itlawdlinfranth=1` turns them off.

### Removed

- The `debug.itlwm.awdl_tx` and `debug.itlwm.awdl_home_tx` diagnostic switches, which had no
  effect on the loss.

## [2.7.35] — 2026-11-17

### Added

- Runtime switches to isolate the hotspot loss seen with awdl0 up, without editing boot-args
  and rebooting between tests. 2.7.34 parked the P2P PHY on the infra channel (parked=1, listens
  held 769) and still saw 100% loss, +1,187 decrypt errors, TX status 0x91 and 165 frames stuck
  on a TX queue. All default to 1 (behaviour unchanged):
  - `debug.itlwm.awdl_tx=0`: iwx_awdl_tx sends nothing.
  - `debug.itlwm.awdl_home_tx=0`: AWDL frames no longer use the infra station's data queue
    (they were hand-built, unencrypted and sent without going through net80211).
  - `debug.itlwm.awdl_bss_promisc=0`: the station MAC never gets IN_PROMISC for AWDL. This takes
    effect at the next MAC context update (Wi-Fi off/on). The P2P MAC filter is unchanged.

## [2.7.34] — 2026-11-17

### Fixed

- iPhone hotspot loss with awdl0 up, part 2. On 2.7.33 the off-channel listens were held
  (`AWDLListenInfraHold` reached 769), yet traffic still saw 100% loss: about 2,900 CCMP errors
  per 10-ping run, TX status 0x91, and 182 frames stuck on TX queue 2. With awdl0 down the loss
  was 0%. After its last listen the P2P MAC stays bound to a PHY context on the AWDL channel
  (149), so the firmware keeps time-sharing with the station's channel (36). When it starts
  holding listens, the AWDL interface now issues one short listen (16 TU) on the infra channel.
  That moves the P2P PHY there, and it retries until the firmware accepts. The count shows as
  `AWDLListenParked`. The 2.7.33 entry below described a full fix; it was only part of one.

## [2.7.33] — 2026-11-17

### Fixed

- iPhone Personal Hotspot (and any infra network) lost most traffic while awdl0 was up. AWDL
  listens on its own channel (149 here) took the radio off the infra channel (36) for up to
  160 TU, back to back. The 2.7.32 counters showed 60–90% ping loss, 0.5–10 s RTT, about 2,600
  CCMP decrypt errors per 20 pings and TX status 0x91 (STA color mismatch). With
  `ifconfig awdl0 down` the same link had 0% loss, gateway 4 ms, and HTTPS 200 (reproduced
  twice). Off-channel AWDL listens (which only run while the station is associated) are now
  held when their channel differs from the infra channel, unless airportd reports active AWDL
  peer traffic (AirDrop). AWDL activity on the infra channel is unchanged. Boot-arg
  `itlawdlinfranth=<n>` lets one in every n held listens through for peer discovery. The held
  count shows as `AWDLListenInfraHold`.

## [2.7.32] — 2026-11-17

### Added

- Data-path counters for the iPhone hotspot loss, reset at each authentication. Write
  `sudo sysctl debug.itlwm.dpdump=1` to log them as `AirportItlwm: dp`:
  - TX: frames handed to the firmware, firmware TX status OK/fail with the last failure code,
    and aggregated per-frame status.
  - RX: frames dropped by the descriptor checks, data frames from the BSS as delivered (all and
    unicast to us), BA-reorder drops, and frames passed to net80211.
  - net80211 drop counters: dup, wepfail, decap, unauth, CCMP replays and decrypt errors,
    BA window below/above/jump/no-buf/gap timeout.
  - TX rings that still hold frames.

### Notes

- 2.7.31 test (21:07): with `debug.itlwm.ps=1` (PM flags 3), the 5 GHz hotspot auto-join lost
  100% to the gateway and 1.1.1.1. With `ps=0` (flags 1, after a Wi-Fi off/on) it lost 100% to
  the gateway and 75% to 1.1.1.1 with 0.7–3.9 s RTT. Power save is not the cause. ARP for the
  gateway resolved. A 10-ping run sent 13 packets and received 4.

## [2.7.31] — 2026-11-17

### Added

- `debug.itlwm.ps` sysctl (default 1). Set it to 0 to keep iwx out of 802.11 power save
  (POWER_MANAGEMENT_ENA) while associated as a client. It takes effect at the next power-table
  update: association, port authorization, or a Wi-Fi off/on. Test case: on 2.7.30 the iPhone
  hotspot joined fine on 5 GHz (auto-join, -41 dBm, 4-way handshake done), but pings lost 75-90%
  with 2-3.7 s RTT and 7 beacon misses. The 2.4 GHz manual join showed the same symptom (no DHCP
  reply). The switch shows whether power save causes this. The flags sent are visible in the
  `ItlwmPMFlags` property.

## [2.7.30] — 2026-11-17

### Changed

- The `assoc_req` and `assoc_resp` hex dumps now zero the payload of every vendor-specific IE
  (221) after its OUI and type, because Apple's hotspot IEs can carry SSIDs or credentials. IE
  ids, lengths and all standard IEs are kept, so dumps can still be compared byte by byte.
  `assoc_resp` dumps are now capped at 512 bytes, up from 384.

### Notes

- Instant Hotspot click test with Maximize Compatibility on: the BLE handoff works. rapportd
  pair-verifies and the iPhone answers `com.sharing.hotspot.request` with name, channel 36 and
  `hasPassword: yes`. airportd then joins the 5 GHz BSS on channel 36 and times out four times
  (-3905, about 10.3 s each) without receiving EAPOL msg 1/4. A manual password join to the
  2.4 GHz BSS (channel 6) completes the 4-way handshake in 23 ms. The failure follows the 5 GHz
  join, not the driver handshake.

## [2.7.29] — 2026-11-17

### Added

- Instant Hotspot join, part 9. The 2.7.28 test ruled out the private MAC address: the firmware
  MAC context and all 115 association responses used the same address (26:40:47:da:f3:eb).
  After association the hotspot still sent only encrypted multicast frames, never EAPOL msg 1/4.
  macOS's later fallback join failed too ("Failed to enable Personal Hotspot"). Each
  (Re)Association Request is now logged as `AirportItlwm: assoc_req`: a summary (capinfo,
  AKMs/ciphers, MFP, PMKID, HT/VHT/HE) plus the full IEs in hex, followed by the BSS RSN IE as
  `bss rsnie`. Each response is logged as `AirportItlwm: assoc_resp` (status, AID and IEs), so
  the hotspot join can be compared byte by byte with a home WPA2 join that works.

## [2.7.28] — 2026-11-17

### Added

- Instant Hotspot join, part 8. The 2.7.27 test kept power save off, but the hotspot still sent
  nothing addressed to the Mac after association: only the association response and encrypted
  broadcast frames, so EAPOL msg 1/4 never arrived. Joins to a normal WPA2 network work. To check
  for a mismatch with the randomized private MAC address, `AirportItlwm: pre-auth rx` now logs the
  receiver address and `ic_myaddr`, and the new `AirportItlwm: auth` line logs the address given to
  the firmware MAC context and the BSSID. The addresses are printed byte by byte because
  `ether_sprintf` returns one static buffer.

## [2.7.27] — 2026-11-17

### Fixed

- 2.7.26 ran the `ic_link_up` hook only when the link state changed. Builds with the Apple
  supplicant (Big Sur to Sonoma) bring the link up at RUN and mark the port valid later, when
  airportd installs the keys. So on WPA networks power save never came back on there. The hook
  now runs on every STA link-up request, including group rekeys (they only re-send the MAC and
  power commands).

## [2.7.26] — 2026-11-17

### Fixed

- Instant Hotspot join, part 7. On 2.7.25 the click finds the hidden hotspot and associates
  (channel 36, ASSOC -> RUN in 7 ms), but the 4-way handshake never starts.
  `ieee80211_eapol_key_input` never runs, and airportd deauthenticates after 10 s. iwx enabled
  802.11 power save (POWER_MANAGEMENT_ENA) on entering RUN, before the handshake, so the radio
  could doze while the AP sent EAPOL msg 1/4. Linux mac80211 allows power save only once the
  port is authorized, and iwx now does the same. Power save turns on when the link goes up,
  through a new net80211 `ic_link_up` hook that runs `iwx_mac_ctxt_task`, which now also
  re-sends the power table. Open networks are unchanged. Not yet confirmed as the cause.

### Added

- `AirportItlwm: pre-auth rx` logs up to 16 frames per association that the BSS sends before
  the port is authorized (beacons and probe responses excluded), with frame control, sequence,
  length and ethertype, to show whether EAPOL arrives at all. `AirportItlwm: power mode` logs
  each power-table update.

## [2.7.25] — 2026-11-17

### Fixed

- 2.7.24 also stopped directed WCL scans from probing passive 5 GHz channels during background
  scans, which 2.7.23 allowed. Clicking the hotspot while connected to another Wi-Fi network
  would then fail to match it again. The background-scan exclusion now applies only to
  net80211's own roaming scans.

## [2.7.24] — 2026-11-17

### Fixed

- Instant Hotspot join, part 6. 2.7.23 confirmed the cause: the driver map marks every 5 GHz
  channel passive (LAR, MCC `00`). The directed WCL scan of channel 36 now gets the hidden
  hotspot's probe response (`ssid[20]`), so airportd matches the hotspot and asks to associate.
  That join timed out (-3905, WCL join status 1009/1007, no auth attempted). The join restarts
  the firmware, which empties the node cache, and net80211's own join scan of channel 36
  (`ic_des_essid`) still did not probe. Only the hotspot's empty-SSID beacons came back, and they
  never match. Directed join scans now probe passive 5 GHz channels too, with LAR only and never
  during background scans (`AirportWCL::probePassiveChannel`).
- The driver map flags no channel DFS (`dfs=[]` in every 2.7.23 scan log), so 2.7.23's DFS
  exception did nothing and directed WCL scans could probe channels 52–144. That range now
  stays passive explicitly.

## [2.7.23] — 2026-11-17

### Fixed

- Instant Hotspot join, part 5. 2.7.22 logs show the click-time scan is a WCL scan with the SSID
  (found at 0x1324) on channel 36 only. The iPhone was there the whole time, sending beacons with
  a hidden SSID (`ssid[0]`, -50 dBm), but no probe response ever came back. The iwx scan
  (`iwx_umac_scan_fill_channels`) never sends probes on channels the NVM marks passive. The
  likely cause is that with LAR, that profile marks 5 GHz passive, while the firmware enforces
  no-IR itself (passive dwell until it hears a beacon), which Linux iwlwifi and OpenBSD iwx rely
  on. That would mean the directed probe that makes a hidden hotspot answer was never sent.
  Directed WCL scans now probe passive non-DFS channels when LAR is enabled. DFS channels and
  broadcast scans are unchanged. Not yet verified on hardware: the new `passive=[...]` log field
  will show whether channel 36 was actually marked passive.

### Changed

- The `AirportItlwm: WCL scan` line also lists which requested channels the driver marks
  passive or DFS (`passive=[...] dfs=[...]`).

## [2.7.22] — 2026-11-17

### Changed

- Scan diagnostics for the Instant Hotspot join: every WCL scan request is now logged with IOLog,
  including its channel list (`AirportItlwm: WCL scan ... channels=N [36,149,...]`). So are
  undecodable requests that are rejected. The legacy `SCAN_REQ` path (`beginLegacyScan`) used
  XYLog, which is kprintf and never reaches `log show`. It now uses IOLog
  (`AirportItlwm: legacy scan ...`) with the SSID and channel list.

### Notes

- Tested on hardware, 2.7.21 still failed to join. The click-time single-channel scans produced
  no `WCL scan` line, while CoreWLAN directed scans carry the SSID in the normal field at 0x20.
  The 2.7.20/2.7.21 guess that the SSID sits in a list after the channel table was never
  confirmed. That lookup only runs when the field at 0x20 is empty, so it is harmless.

## [2.7.21] — 2026-11-17

### Fixed

- Instant Hotspot join, part 4: tested on hardware, 2.7.20 still failed 14 times and logged nothing.
  The hotspot scan reaches the driver as scan type 3 (fast), which the driver also probes for,
  but the list SSID search and the new log line only covered type 1. Both now cover types 1 and 3
  (still limited to 1-4 channels). Not yet verified on hardware.

## [2.7.20] — 2026-11-17

### Fixed

- Instant Hotspot join, part 3: on Tahoe, IO80211ScanManager turns Apple80211Scan's `SCAN_REQ`
  into an ordinary WCL scan, so the 2.7.19 `setSCAN_REQ` path is never reached. With
  capability 73 the hotspot scans now run (active, channel 36), but they found only the other
  APs there. These requests carry the SSID only in the list after the channel table, while
  `decodeScan` read only the single SSID field. The probes were therefore wildcard, which a
  hidden hotspot ignores. For active scans of 1-4 channels where that field is empty (the hotspot
  join scans one), the SSID is now taken from the first list entry: a length of 2-32, then
  that many printable/UTF-8 bytes, then zero padding. Broadcast scans keep their wildcard probes.

### Changed

- Active or directed WCL scans are logged with IOLog (`AirportItlwm: WCL scan ...`: SSID and
  where it was found, channel count, the first 12 words after the channel table), so the
  layout can be checked in `log show`. Not yet verified on hardware.

## [2.7.19] — 2026-11-17

### Fixed

- Instant Hotspot join, part 2 (Tahoe): the card now reports capability 73 (byte 9, 0x02).
  Without it IO80211.framework's `Apple80211Scan`, which airportd's hotspot join uses, always
  sends `SCAN_REQ_MULTIPLE`. The WCL interface has no handler for that ioctl, so every such
  scan failed with 102 (seen on 2.7.18). With it, the framework sends `SCAN_REQ` in a
  0x1598-byte layout that does reach the driver.
- `SCAN_REQ` on the WCL path (Tahoe) now starts a real scan, directed at the request's SSID
  (its own field, or the first entry of its SSID list) on its channels, through the WCL scan
  engine. When it ends the driver posts the legacy `APPLE80211_M_SCAN_DONE`, which
  `Apple80211Scan` waits for before reading `SCAN_RESULT`. Fast scans, or requests that can't
  start a scan, are answered from the node cache after 100 ms. A request that arrives during
  a WCL scan is answered when that scan ends. Previously this ioctl ran an undirected
  background scan and could cut an in-progress WCL scan short. Each request is logged
  (`beginLegacyScan`). Not yet verified on hardware.

## [2.7.18] — 2026-11-17

### Fixed

- Reconnecting to an iPhone hotspot from the Wi-Fi menu (Instant Hotspot) after leaving it:
  the card now reports capability 22 (byte 2, 0x40). Without it IO80211.framework's
  `Apple80211Scan` refuses every scan that carries an SSID list with -3903 before the driver
  sees it. airportd's tether join scans that way, so the Mac only rejoined when auto-join
  happened to see the hotspot beaconing. The WCL path only ever reported WME|SGI40|SGI20
  in byte 2; the legacy path, like upstream, reports 0xFF, which includes this bit. Tested on
  hardware: not enough on its own. The scan now reaches the kernel as `SCAN_REQ_MULTIPLE`,
  which the WCL interface rejects with 102 (EOPNOTSUPP).

## [2.7.17] — 2026-11-16

### Changed

- iwx, HT-only links (2.4 GHz): the firmware station now gets the peer's advertised maximum
  A-MPDU length instead of a fixed 64K, and an MPDU start spacing of at least the peer's
  requirement (VHT/HE links already used the peer's length). Suspected cause of the iPhone
  2.4 GHz hotspot stall (associated, data frames unacknowledged, then LMAC assert 0x91 with
  the TID 0 aggregation queue never drained); verified on hardware (channel 6: 0% ping loss,
  ~10 MB/s, no firmware errors).
- iwx, HT-only links: a peer in static SM power save is treated as single-stream (no 2-stream
  HT rates, SISO station flag), as iwlwifi does.

### Added

- Runtime switches, applied from the next association: `sysctl debug.itlwm.txagg=0` (no TX
  A-MPDU sessions) and `sysctl debug.itlwm.ht_siso=1` (single stream on HT-only links).
- Association log line with the peer's HT capabilities, A-MPDU parameters, SM power save mode
  and the resulting station flags.

## [2.7.16] — 2026-11-16

### Added

- iwx diagnostics for the iPhone hotspot link (HE off: associated, but almost no traffic):
  - CCMP hardware-decrypt failures split into frames from the associated BSS (by failed
    check: SEC_ENC vs DEC_DONE|MIC_OK) and everything else (e.g. AWDL promiscuous
    neighbours). Rate-limited log with status, frame control, sequence, destination.
    Exported as the `NetRxDecDiag` property.
  - Firmware TX failures counted by kind (data/mgmt/AWDL) with a status histogram and
    the last status, RTS/frame retry counts and initial rate. Rate-limited log.
    Exported as the `NetTxFailStats` property.

## [2.7.15] — 2026-11-16

### Fixed

- iwx: `iwx_mac_ctxt_cmd_fill_sta` no longer sends a zero beacon interval or
  DTIM period to the firmware when the node was learned from a probe response
  (no TIM element). Defaults to bi=100, dtim_period=1. Suspected, not yet
  confirmed, cause of the `0x2000011A` UMAC assert on 2.4 GHz joins.

### Added

- iwx: log of bi/dtim_period/dtimcount/channel at each MAC context STA fill,
  to confirm or rule out the suspected cause.

## [2.7.14] — 2026-11-15

### Changed

- README: documented Continuity Camera. It works over USB (tested in Photo Booth);
  wireless is blocked because the Mac never sees the iPhone as nearby (cause
  undetermined, possibly the Bluetooth stack). Documentation only; no driver changes.

## [2.7.13] — 2026-11-15

### Changed

- README: documented that Handoff works on the tested setup (MacPro7,1 SMBIOS, Intel
  AX210, macOS Tahoe 26.7.3, iPhone 16 Pro). Documentation only; no driver changes.

## [2.7.12] — 2026-11-15

### Changed

- README: moved the "AirportItlwm on macOS 26" section to the top, and documented that
  Universal Clipboard now works (text and images, both directions) as of 2.7.11.
  Fixed the two "below" references in the Sequoia section that pointed the wrong way after
  the move. Documentation only; no driver changes.

## [2.7.11] — 2026-11-15

### Fixed

- awdl0 receive: split A-MSDU frames. The phone aggregates bulk data (a Universal
  Clipboard image, AirDrop) into 2-3 KB frames whose payload starts with A-MSDU
  subframes `DA(6) SA(6) len(2) | LLC/SNAP 00:17:f2 | AWDL header | packet`. The Intel
  path clears the QoS A-MSDU bit itself, so `inputAWDLData` saw a SNAP mismatch, dropped
  the whole frame and the phone kept retransmitting, which stalled phone->Mac pastes of
  images while text (single small frames) worked. A frame that fails the normal SNAP
  check is now parsed as a subframe list; only if every subframe is well formed (length
  fits, SNAP + type 0x0800 present, 4-byte padding, no trailing bytes) is it split and
  each subframe delivered as its own Ethernet frame. Anything else is still counted bad
  and dropped, and frames with a valid SNAP take the unchanged path.
- New counters: `AWDLDataRxSplitFrames`, `AWDLDataRxSplitSub`, and
  `AWDLDataRxSplitAddrDiff` (subframes whose DA/SA differ from the 802.11 addr1/addr2,
  which are what is used for the Ethernet header, as before).

## [2.7.10] — 2026-11-15

### Fixed

- awdl0 bad-frame diagnostics: `AWDLDataRxBadLastRaw0..3` (renamed `AWDLDataRxBadLastPay0..3`)
  captured the first 32 bytes from the 802.11 header, i.e. only 6 payload bytes. They now
  capture the first 32 bytes *after* the header, enough to see an A-MSDU subframe header
  (DA, SA, length) followed by the LLC/SNAP bytes, or to show the frame is something else.
- Corrected the 2.7.9 note below: it said the bad frames "are not A-MSDUs" because
  `AWDLDataRxAmsdu` stayed 0. That was wrong: `ItlIwx` clears the QoS A-MSDU bit itself for
  hardware A-MSDU subframes before `inputAWDLData` runs, so the bit and that counter cannot
  tell. Whether these frames are A-MSDU subframes is still unknown.

## [2.7.9] — 2026-11-15

### Added

- awdl0 receive diagnostics only (no behaviour change): the last bad frame now also
  records its QoS control (`AWDLDataRxBadLastQos`, the A-MSDU bit is always clear here, see
  2.7.10) and its first bytes (`AWDLDataRxBadLastRaw0..3`), and `AWDLDataRxBadOver2304`
  counts bad frames longer than the 2304-byte MSDU limit. In 2.7.8 the bad frames during a
  failed Universal Clipboard image paste were all 2.4-3 KB QoS data frames with a non-AWDL
  payload; this records what they really are before any receive handling is changed.

## [2.7.8] — 2026-11-15

### Added

- awdl0 receive diagnostics (no behaviour change) to find why phone→Mac bulk data
  such as a Universal Clipboard image stalls: `AWDLDataRxBad` is now split into
  `AWDLDataRxBadPullup`, `AWDLDataRxBadShort` and `AWDLDataRxBadSnap`, with the last
  bad frame's length, frame-control, assumed header length and first 8 payload bytes
  (`AWDLDataRxBadLast*`). Good frames are bucketed by size (`AWDLDataRxLenLt128`,
  `Lt512`, `Lt1024`, `Ge1024`) with a max (`AWDLDataRxMaxLen`).

### Changed

- Null / QoS-null data frames (no payload) are counted in the new `AWDLDataRxNull`
  instead of `AWDLDataRxBad`, so "bad" now means a frame that carried data we could
  not parse. They were and still are dropped.

## [2.7.7] — 2026-11-15

### Changed

- README: documented that iPhone Mirroring works only up to the passcode prompt on
  the iPhone and cannot be ported, because the Mac has no T2 / bridgeOS to issue the
  device certificate (DCRT) it requires. Documentation only; no driver changes.

## [2.7.6] — 2026-11-14

### Changed

- AWDL listen windows: the firmware request-to-start delay is now peak-held (fast
  attack, slow decay) and the host lead time adds 1.5 ms margin. Previously the
  lead followed the single last sample, so a jittery 4-10 ms start latency made
  later requests too late and the window missed the peer's AW slot, leaving the
  Mac barely hearing the phone's replies on channels 6/149 while home is 36.
  Both ROC paths (P2P session protection and the aux-ROC fallback) share one
  helper, and each sample is capped at 30 ms so a stale request time cannot latch.

## [2.7.5] — 2026-11-14

### Fixed

- `ItlIwx`: drop awdl0 unicast data frames that are link-layer retransmissions
  (retry bit set and same sequence number as the previous frame of that TID).
  The peer repeats frames whenever it misses our hardware ACK, and each copy used
  to reach TCP as a duplicate segment, collapsing the phone's congestion window
  and stalling AirDrop mid-transfer. Exposed as `AWDLFwUcDropDup`.
  The filter is per transmitter and per TID, tracks the A-MSDU subframe index (so
  the later subframes of a retried aggregate are kept), and ignores QoS-Null.

## [2.7.4] — 2026-11-14

### Fixed

- `AirportItlwmAWDLInterface` / `ItlIwx`: trigger off-channel AWDL `PSF`, `MIF`
  (Bonjour `_airdrop._tcp` / `_companion-link._tcp` TLVs), OOB, and `awdl0` data
  transmission from the firmware remain-on-channel `START` notification
  (`ic_awdl_roc_notify`) with adaptive ROC lead timing instead of a fixed 15/30 ms
  wall-clock timer that fired before Intel session protection started (causing
  74% `EAGAIN` `AWDLTxFwBusy` drops and zero unaligned `PSF`/`MIF` advertisements
  on social channels 6/149 when associated on another 5 GHz channel such as 36).
- `AirportItlwmAWDLInterface`: stop draining `awdl0` multicast (mDNS/NDP) and
  `pc == 0` extension-slot data/OOB frames onto a non-shared home channel (where
  `228/247` packets were previously lost when the peer only listened on 6/149),
  holding them for the active off-channel ROC dwell instead.
- `ItlIwx` / `ItlIwm`: ignore firmware missed-beacon notifications while an
  off-channel scan, background scan, or AWDL remain-on-channel window is active,
  preventing a background scan from arming `ic_mgt_timer` via `PROBE_REQ` and
  dropping the live Wi-Fi link mid-scan.
- `itl80211` / `WCL`: clear `IEEE80211_F_BGSCAN` whenever transitioning to
  `IEEE80211_S_INIT` / `IEEE80211_S_SCAN`, leaving a WCL network, aborting/timing
  out a WCL scan, or finishing `ieee80211_end_scan` outside `IEEE80211_S_RUN`,
  fixing a permanent `kIOReturnBusy` (`0xe00002d5`) scan/join wedge (`"Unable to
  join the network … The network could not be found."`).

## [2.7.3] — 2026-11-14

### Fixed

- `AirportItlwmAWDLEthernetInterface`: mark `awdl0` with IORegistry
  `HiddenInterface` and `HiddenConfiguration` properties so `configd` /
  `SCNetworkConfiguration` does not register `awdl0` as a user-visible `"Wi-Fi 2"`
  service (stealing `IOPrimaryInterface` from `en1`) or assign IPv4 Link-Local
  (`169.254.x.x`), which caused `mDNSResponder` / `wifip2pd` D2D self-conflict
  loops every 100 ms and stalled AirDrop Bonjour/Rapport resolution on `"Waiting..."`;
  also drop self-sourced AWDL data frames in `inputAWDLData`.

## [2.7.2] — 2026-11-14

### Fixed

- `ItlIwx`: defer BSS MAC filter updates from `iwx_awdl_sync_notify` onto a
  `systq` task (`sc_awdl_mac_task`) without holding `iwx_nic_lock` across
  `iwx_mac_ctxt_cmd`, preventing synchronous command sleeps inside RX packet
  processing or user-thread IOCTLs; also clear `IWX_TXDATA_FLAG_AWDL` in both
  `iwx_txd_done` and `iwx_reset_tx_ring`.

## [2.7.1] — 2026-11-14

### Fixed

- `ItlIwx`: reset `sc_bss_awdl_promisc`, `ic_awdl_listen_active`, and
  `ic_awdl_promisc` in `iwx_stop` so BSS promiscuous state is re-applied cleanly
  across NIC resets and sleep/wake; clear `IWX_TXDATA_FLAG_AWDL` in
  `iwx_txd_done` when reclaiming TX descriptors; only enable `IN_PROMISC` on
  the BSS MAC once associated (`assoc != 0`).
- `AirportItlwmAWDLInterface`: copy `setAWDL_OOB_REQUEST` destination and
  payload inside the workloop gate before calling `trySendPendingOOB` to prevent
  races with concurrent timer-driven TX.

## [2.7.0] — 2026-11-14

### Added

- AWDL / AirDrop (`AirportItlwmAWDLInterface`, `AirportItlwmSkywalkInterface`,
  `AirportItlwmV2`):
  - Transmit on-demand OOB / Unicast Master Indication frames from
    `setAWDL_OOB_REQUEST` (with unicast or broadcast destination MAC) instead of
    only recording diagnostic headers.
  - Support AWDL action and `awdl0` data transmission directly on the
    associated infrastructure home channel (`iwx_awdl_tx` via `first_data_qid`)
    as well as off-channel P2P listen dwells, with immediate queue flushing on
    active channels to eliminate multi-dwell handshake stalls.
  - Implement known-layout AWDL and Skywalk IOCTLs queried by macOS 26
    `IO80211AWDLPeerManager`: `getVHT_CAPABILITY`,
    `getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES`, `getAWDL_AF_TX_MODE`,
    `getAWDL_MASTER_CHANNEL`, `SECONDARY_MASTER_CHANNEL`, `ELECTION_ID`,
    `ELECTION_METRIC`, `ELECTION_RSSI_THRESHOLDS`, `MIN_RATE`,
    `EXTENSION_STATE_MACHINE_PARAMETERS`, `setAWDL_ENCRYPTION_KEYS`, and
    `setAWDL_PEER_TRAFFIC_REGISTRATION`.

### Fixed

- `ieee80211_input.c`: deliver multicast AWDL `DIR_NODS` data frames matching
  the AWDL BSSID (IPv6 NDP Neighbor Solicitations and mDNS `_airdrop._tcp`
  packets) to `ic_awdl_data_handler` instead of dropping them as station
  `wrongdir` input errors, and filter foreign promiscuous `TYPE_DATA` frames
  before station BA reordering.
- `AirportItlwmAWDLInterface`: remove the inverted `fAfTxMode == 3` gate in
  `scheduleTx` that muted all PSF, MI, and data transmissions whenever
  `IO80211AWDLPeerManager` entered active `Scan + Data` mode; apply the
  `(phy_tx - target_tx)` contention delay correction to AW boundary sync.
- `ItlIwx` & `WCL`: keep `IWX_MAC_FILTER_IN_BEACON` enabled when AWDL is active
  and report `LqmBeaconUpdate` every 2 beacons so `monitorInfraBeacon` no
  longer downgrades AWDL from `Scan + Data` to `Idle` (`RptDataPathThres`);
  enable `IWX_MAC_FILTER_IN_PROMISC` on the BSS MAC while AWDL data is active
  and on the P2P MAC as soon as `awdl0` has an address.
- `AirportItlwmV2` / `AirportSTAIOCTL`: set `APPLE80211_VERSION` and HT/VHT
  capability bits in `getCARD_CAPABILITIES` so `IO80211AWDLPeerManager` no
  longer rejects card capabilities.

## [2.6.2] — 2026-11-09

### Fixed

- README: AWDL / AirDrop was still listed as unsupported next to the new
  experimental AWDL entry; the receive-filter note now says it applies once
  awdl0 has an address.

## [2.6.1] — 2026-11-09

### Changed

- README: replaced the stale `itlwm_awdl=1` text (removed in 2.6.0) with the
  PR #53 boot-args, including `-itlawdllisten`, `-itlawdltx`, `-itlawdlaw`,
  `-itlawdldata`, `-itlawdlnopeerlisten` and `itlawdllistench=<n>`, which the
  2.6.0 entry did not list.

## [2.6.0] — 2026-11-09

### Added

- Merged upstream `laobamac/itlwm` PR #53: experimental AWDL (`awdl0`) for
  AirDrop on the Tahoe target. Off by default; enabled with the boot-args
  `-itlawdl`, `-itlawdlp2p`, `-itlawdlpromisc`.
  - New `AirportItlwmAWDLInterface`, macOS 26 `IO80211VirtualInterface` /
    `IO80211AWDLProtocol` headers, `setVIRTUAL_IF_CREATE` handling.
  - iwx: P2P listen path, open receive filter, and a per-station
    `iwx_tvqm_enable_txq(..., sta_id)`.
  - net80211: with `-itlawdl` set, management frames from other BSSes and
    non-matching control frames are dropped on input.

### Changed

- Replaced the 2.4.8 `itlwm_awdl=1` virtual-interface logging experiment with
  the PR #53 implementation.
- Merge fix: the PR #44 SCD v3 queue path now honours `sta_id`
  (`sta_mask`, remove-on-failure), and the main-station TID mask is only
  updated for `IWX_STATION_ID`.

Untested on hardware; the code comes from an upstream PR built by reverse
engineering.

## [2.5.1] — 2026-11-09

### Added

- iwx: match Killer Wi-Fi 6E AX1675s / AX1675i on PCI device `8086:51F1`
  (subsystems `1671` / `1672`), same config as the `51F0` entries (upstream
  issue #45).

### Reviewed, no change

Remaining open upstream issues were checked against this tree:

- #31 (WPA2-Enterprise): the link-up-before-RUN race it suspects is already
  handled (first beacon held until the port is authorized, EAPOL copied to
  user space, key install gated on RUN). The reporter's logs do not show a
  driver fault.
- #38, #39, #40, #42, #43, #46: wake / fast-join / scan-after-wake; covered by
  the PR #44 changes in 2.5.0, unverified on hardware.
- #47 (OpenCore vtable patch failure on Tahoe), #48 (iPhone hotspot), #50,
  #51, #23, #19, #21 and #15 / #27 / #49 (BE200): need hardware logs or new
  firmware / chip support; not addressed.

## [2.5.0] — 2026-11-09

### Added

- Merged upstream `laobamac/itlwm` PR #44 (AX201 / sleep-wake work):
  - iwx: firmware API 77 for AX201 (`iwlwifi-QuZ-a0-hr-b0-77.ucode`).
  - iwx: Wi-Fi power management while connected.
  - Fast join: a join request from macOS no longer waits for a full scan;
    the BSS channel is checked again before falling back to a full scan.
  - WCL: the driver is reported available when the firmware starts after wake;
    scan requests are held while the firmware restarts; a deauth is sent to the
    AP when leaving the network; link up is posted only after the port is
    authorized; `setWCL_ARP_MODE` / `setPM_MODE` are accepted as no-ops.

### Fixed

- iwx: wait 300 ms for ALIVE on the first firmware load; scan-abort
  completion handling, firmware restart during an aborted scan, and a failed
  SCD queue removal.
- net80211: ignore a disassoc received before the association is complete.

### Not included

- Upstream PR #37 (experimental Meteor Lake / AX211, API 83) conflicts with
  PR #44 in `ItlIwx.cpp` and is not merged.

## [2.4.9] — 2026-11-09

### Fixed

Ported from upstream `laobamac/itlwm` (commits after baseline `d695c97`, up to
`7204c69`):

- WPA2/WPA3 transition-mode association; an ordinary PSK (AKM 2) is required
  before the transition-mode conversion.
- WCL link authorization is handled before RUN (early-EAPOL race).
- VHT is no longer advertised in association requests on 2.4 GHz (Cisco
  Aironet 3800 compatibility).
- Invalid VHT 160 MHz channels fall back to a valid width.
- iwx: copy only received bytes from the VT-d bounce buffer; all-ones
  register reads are treated as "not ready" (polling runs to the timeout);
  passive dwells are no longer fragmented while associated.
- iwm: LMAC and UMAC scans use the `scanTiming()` suspend time.
- Rate-control locking (Sequoia/Tahoe) and TX aggregation queue validation for
  newer firmware.

## [2.4.8] — 2026-10-14

### Changed

- README: documented the experimental `itlwm_awdl=1` boot-arg and the
  `SYMROOT="$PWD/build"` option that puts Release kexts in `build/`.

## [2.4.7] — 2026-10-14

### Added

- Sequoia/Tahoe: `setVIRTUAL_IF_CREATE` now logs every virtual-interface
  request from `airportd` (role, MAC, BSD name) instead of silently returning
  unsupported, to find out what macOS asks for when it wants `awdl0`.
- Experimental boot-arg `itlwm_awdl=1`: for an AWDL request, try
  `attachVirtualInterface()` to create the interface. Off by default (still
  returns unsupported). This only creates the interface; there is no AWDL
  protocol, so AirDrop will not work. Untested on hardware.
  (`AirportItlwm/AirportItlwmSkywalkInterface.cpp/.hpp`)

## [2.4.6] — 2026-10-13

### Changed

- Bundle copyright strings of the remaining kexts changed from
  "Copyright © 2020 钟先耀. All rights reserved." to "Copyright © 2026
  minhnhan_tran. Based on AirportItlwm by OpenIntelWireless." (AirportItlwm
  Info.plists) or "... Based on itlwm by OpenIntelWireless." (itlwm,
  itl80211), matching the WCL plists. Xcode ORGANIZATIONNAME is now
  minhnhan_tran. License/copyright headers in source files are unchanged.

## [2.4.5] — 2026-10-13

### Changed

- Bundle copyright string is now "Copyright © 2026 minhnhan_tran. Based on
  AirportItlwm by OpenIntelWireless." (`AirportItlwm/WCL/Info.plist`,
  `AirportItlwm/WCL/Sequoia-Info.plist`). Source-file copyright headers are
  unchanged.

## [2.4.4] — 2026-10-13

### Fixed

- Noise floor and SNR reported to macOS. WCL scan results
  (`publishWCLBeacon`) went out with noise 0 dBm and SNR 0 for every
  network, and the link noise / legacy scan results used `-getBSSNoise()`,
  which negates a dBm value (0 dBm until the firmware sends statistics, a
  positive value after). All of them now report the firmware's measured
  noise floor when it is plausible (-120..-40 dBm), otherwise a typical
  -95 dBm, and WCL scan results carry SNR = RSSI - noise. Experiment: SNR 0
  may be why `locationd` treats every AP as unusable (`usableAps 0`).
  (`AirportItlwm/AirportItlwmNoise.hpp` (new), `AirportItlwm/WCL/WCL.cpp`,
  `AirportItlwm/AirportItlwmSkywalkInterface.cpp`,
  `AirportItlwm/AirportSTAIOCTL.cpp`)

### Changed

- Reverted the 2.4.3 passive-scan dwell override: the scan home/away time
  macOS requests applies to all scans again. It never triggered in testing,
  and when it does, macOS is probably limiting off-channel time for
  latency-sensitive traffic, where 120 ms gaps would hurt. The
  "scan home/away time set to N ms" log line is kept.
  (`AirportItlwm/WCL/WCL.hpp`, `itlwm/hal_iwx/ItlIwx.cpp`,
  `itlwm/hal_iwm/scan.cpp`)

## [2.4.3] — 2026-10-13

### Changed

- Passive scans while associated now always get a full, non-fragmented
  passive dwell (110 TU, max out 120 TU). Previously, when macOS set a scan
  home/away time below ~113 ms (WCL command 15),
  `AirportWCL::scanTiming()` capped the off-channel time below one beacon
  interval and enabled Intel fragmented scanning (44 TU dwell). Passive scans
  only discover an AP from its beacons (~102.4 TU apart), so weak and
  neighbouring APs were often not reported, and `locationd` (which only uses
  APs seen in the last 5 s) was left without usable Wi-Fi data. The requested
  home/away time is still honoured for active scans. Trade-off: while
  associated, passive scans now leave the home channel for up to ~120 ms at a
  time. (`AirportItlwm/WCL/WCL.hpp`, `itlwm/hal_iwx/ItlIwx.cpp`,
  `itlwm/hal_iwm/scan.cpp`)

### Added

- One-line `IOLog` ("scan home/away time set to N ms") when macOS sets the
  scan home/away time, to confirm whether the fragmented-scan path is hit on
  a given system. (`AirportItlwm/WCL/WCL.cpp`)

## [2.4.2] — 2026-10-13

### Fixed

- Regulatory locale was hardcoded to FCC in `getLOCALE()` (legacy STA ioctl
  path and Sonoma Skywalk interface). It is now derived from the active
  country code (`AirportItlwm/AirportItlwmLocale.hpp`).
- Sequoia/Tahoe (WCL) builds showed `Locale: Unknown`: that ABI never issues
  `APPLE80211_IOC_LOCALE`, so the `IO80211Locale` registry property is now
  published from `getCOUNTRY_CODE()`.

### Changed

- Hardened scan-result age (`asr_age`): saturating arithmetic, and
  `ni_age_ts` is initialised when a node is created. Station-mode nodes were
  already always timestamped, so this is defensive rather than a live fix.

## [2.4.1] — 2026-10-12

### Fixed

- Scan list collapsing to only a couple of networks while associated. The
  periodic cache maintenance in `ieee80211_begin_cache_bgscan()` dropped the
  entire node cache via `ieee80211_free_allnodes()` every 5 minutes, but the
  background scan that follows only dwells on a few channels. Every AP the
  bgscan did not happen to revisit was therefore discarded, leaving userland
  (System Information, the Wi-Fi menu) showing just the associated channel plus
  whichever channel the bgscan last sampled. Cached results are now aged out
  individually by `ieee80211_clean_inactive_nodes()`, keeping anything heard in
  the last `IEEE80211_CACHE_MAX_AGE` (300) seconds and never evicting the
  associated BSS. (`itl80211/openbsd/net80211/ieee80211.c`,
  `ieee80211_node.c`, `ieee80211_node.h`)
- WCL scan requests no longer fail with `kIOReturnBusy` when net80211 declines
  to start a background scan (bgscan already running, pending management
  timer, or RSN port not yet valid). Returning an error made userland keep its
  previous, stale list; the request now completes successfully so the caller
  re-reads the node cache. (`AirportItlwm/WCL/WCL.cpp`)

### Added

- `ieee80211_clean_inactive_nodes()` — age-based cached-node eviction, the
  gentle counterpart to `ieee80211_free_allnodes()` (AirportItlwm builds only).

## [2.4.0] — baseline

- Baseline of this fork as imported: Intel Wi-Fi kernel extension for macOS
  based on the OpenBSD `net80211` stack, with native Wi-Fi (Airport) support on
  macOS Sequoia 15.2+ and Tahoe 26.x via the Skywalk/WCL path.
