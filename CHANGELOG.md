# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
