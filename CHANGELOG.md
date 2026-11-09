# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
