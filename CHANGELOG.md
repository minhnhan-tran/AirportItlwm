# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.7.18] — 2026-11-17

### Fixed

- Reconnecting to an iPhone hotspot from the Wi-Fi menu (Instant Hotspot) after leaving it:
  the card now reports capability 22 (byte 2, 0x40). Without it IO80211.framework's
  `Apple80211Scan` refuses every scan that carries an SSID list with -3903 before the driver
  sees it. airportd's tether join scans that way, so the Mac only rejoined when auto-join
  happened to see the hotspot beaconing. The WCL path only ever reported WME|SGI40|SGI20
  in byte 2; the legacy path, like upstream, reports 0xFF, which includes this bit. Not yet
  verified on hardware.

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
