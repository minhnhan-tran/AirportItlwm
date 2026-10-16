# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
