[![Version](https://img.shields.io/github/v/release/minhnhan-tran/AirportItlwm?include_prereleases&style=flat&label=Downloads)](https://github.com/minhnhan-tran/AirportItlwm/releases)
# itlwm

**An Intel Wi-Fi Adapter Kernel Extension for macOS, based on the OpenBSD Project.**

## AirportItlwm on macOS 26


This fork supports macOS Tahoe 26.x (x86_64) through the native Wi-Fi interface,
including scanning, connecting, network switching, and private Wi-Fi addresses.

- **Wi-Fi 6 (802.11ax / HE) is disabled by default.**
  Add `itlwm_he=1` to enable it on supported adapters.
- WPA2-Personal and WPA2/WPA3 transition networks with optional PMF are supported.
  WPA3-only, mandatory PMF, and MLO are not supported.
- Experimental (Tahoe target only): AWDL (`awdl0`, used by AirDrop and Universal
  Clipboard), off by default and only lightly tested on hardware. Boot-args, cumulative: `-itlawdl` (base), `-itlawdlp2p`
  and `-itlawdlpromisc` (iwx P2P listen / receive filter, AX2xx), `-itlawdllisten`,
  `-itlawdltx`, `-itlawdlaw`, `-itlawdldata` (listen, transmit, availability
  windows, data), `-itlawdlnopeerlisten` and `itlawdllistench=<n>` (diagnostics).
  Once awdl0 has an address, the shared receive path drops frames from other BSSes.
- **Universal Clipboard works** (tested on a MacPro7,1 SMBIOS with an Intel AX210,
  macOS Tahoe 26.7.3 and an iPhone 16 Pro, AirportItlwm 2.7.11 with the AWDL
  boot-args above). Text and images copy in both directions. Phone-to-Mac images
  need 2.7.11 or later: the phone sends bulk data as A-MSDU frames that older
  builds dropped, so the paste stalled on "Connecting...". Mac-to-phone worked
  earlier. Results may vary with the adapter and peer.
- **Handoff works** (same test setup as Universal Clipboard). Handoff discovery uses
  Bluetooth, so it also depends on a working Intel Bluetooth stack
  (BlueToolFixup with `-btlfxadvsets` on macOS 26.7.3). Both devices need the same
  Apple ID, Wi-Fi and Bluetooth on, and Handoff enabled on each.
- **iPhone Instant Hotspot works** (clicking the iPhone in the Wi-Fi menu or Control
  Center; tested on a MacPro7,1 SMBIOS with an Intel AX210, macOS Tahoe 26.7.3, an
  iPhone 16 Pro, AirportItlwm 2.7.42 or later). The Mac joins on the first attempt,
  gets an address in 5 to 10 s, and pings and HTTPS run with no loss. Notes:
  - Real Macs add Apple's Instant Hotspot device IE to the (Re)Association Request;
    without it the iPhone accepts the association but never sends EAPOL message 1.
    The driver adds it by default (`sysctl debug.itlwm.ihs_ie`: `1` on, `-1` off, or
    a flags byte `0` to `255`).
  - Waking the hotspot needs a Bluetooth LE connection from the Mac to the iPhone,
    so it depends on a working Intel Bluetooth stack. The iPhone uses a private
    address, which bluetoothd cannot resolve with an Intel controller; the test
    setup used IntelBTPatcher 2.6.1 from
    [minhnhan-tran/IntelBluetoothFirmware](https://github.com/minhnhan-tran/IntelBluetoothFirmware),
    which loads the iPhone's IRK into the controller's resolving list (see below).
  - Both bands work. With "Maximize Compatibility" off the hotspot is on 5 GHz; with
    it on, it is on 2.4 GHz (tested on channel 6: 802.11n, DHCP lease `172.20.10.2`,
    0% loss to the gateway and to 1.1.1.1, HTTPS 200; IPv6 and long transfers on
    2.4 GHz were not tested). The hotspot channel can differ from the AWDL channel;
    since 2.7.36 that no longer causes packet loss with `awdl0` up (the receive
    filter stays off the station MAC; see `debug.itlwm.awdl_bss_promisc`).
  - A saved network with `AutoJoinDisabled` does not auto-join after a reboot or a
    Wi-Fi off/on; join it from the Wi-Fi menu.
- **AirDrop works over AWDL, with limited speed** (same test setup and AWDL boot-args
  as above, AirportItlwm 2.7.48). Discovery and transfers work in both directions,
  also while the Mac is joined to home Wi-Fi or to the iPhone hotspot on any channel.
  Notes:
  - When the Mac and the iPhone are on the same network, AirDrop goes over that
    network (Wi-Fi or Ethernet) and is fast, not over `awdl0`. To exercise AWDL,
    disconnect the iPhone from Wi-Fi (leave Wi-Fi and Bluetooth on).
  - Over `awdl0` a 100 MB transfer took about 2 to 8 minutes (about 3 to 12 Mbit/s,
    iPhone to Mac usually faster than Mac to iPhone). One cause is the channel
    schedule: during a transfer the iPhone moves to a high-throughput schedule
    (11 of 16 slots on channel 149), and the Mac follows only if macOS sees AWDL
    traffic. The driver reports its AWDL data counts to IO80211Family through
    `updateInterfaceDataStats` (`sysctl debug.itlwm.awdl_dpstats`, default `1`);
    with it off the Mac stays on its low-power schedule, and iPhone to Mac measured
    3.7 Mbit/s instead of 8 Mbit/s, Mac to iPhone 1.7 instead of 2.8 Mbit/s.
  - Other switches (defaults in brackets): `debug.itlwm.awdl_home_listen` [2] makes
    AWDL listen on the infrastructure channel whenever it wants data (needed for
    AirDrop when the hotspot or router is on AWDL's channel),
    `debug.itlwm.awdl_infra_nth` [1] limits how often off-channel listens are
    held back while joined to an infrastructure network, `debug.itlwm.awdl_bss_promisc`
    [0] turns the station-MAC promiscuous filter back on (it stalls TX on a shared
    channel), and `debug.itlwm.ps` [1] turns client power save on or off.
  - The `AWDLPairStats` property on the AWDL interface in `ioreg` shows how the
    Mac's and the peer's channel slots overlap, and `dps=<calls>/<result>` for the
    data-count reports.
- **Continuity Camera works wirelessly (experimental, needs the `-itlllw` boot-arg).**
  macOS carries the camera's video and audio over a low-latency Skywalk interface,
  `llw0`, tied to AWDL. With `-itlllw` the driver creates `llw0` and bridges it to
  AWDL in both directions, and the iPhone appears as a camera and streams live video
  (tested in Photo Booth with an iPhone 16 Pro, while joined to a home 5 GHz network
  on channel 36, the channel AWDL used). Over USB it works without the boot-arg.
  With `debug.itlwm.awdl_share_phy` [1], AWDL listens on the home network's channel
  join the station's firmware binding instead of time-sharing a second PHY: about
  33–40 fps at about 1.9 Mbit/s, against 5–9 fps at under 1 Mbit/s with 0. Before
  the P2P MAC changes binding or PHY, its TX queue is flushed
  (`debug.itlwm.awdl_p2p_flush` [1]: flush when at least this many frames wait, 0
  never). Up to 2.7.88 the firmware asserted (`NMI_INTERRUPT_LMAC_FATAL`) after such
  moves while frames were still queued, dropping home Wi-Fi for about 20 s. In the
  2.7.90 test (4.5 min at 36–38 fps) the firmware did not crash and the gateway ping
  stayed at a 0.5 ms median with no loss (2.7.88: 450 ms median, 17% loss).
  `debug.itlwm.awdl_home_qmax` [8] caps unicast AWDL frames on the station's own TX
  queue (0 = no cap). While `llw0` carries traffic, those listens use `debug.itlwm.awdl_rt_cool` [1] (pause after
  each listen, as a multiple of its length) and `debug.itlwm.awdl_rt_dwell` [64]
  (maximum listen, TU). Keep `awdl_rt_cool` above 0: back-to-back listens dropped
  the home network. `debug.itlwm.llw_tx` [1] set to 0 stops sending `llw0` traffic
  to the iPhone. `ioreg` counters: `LLW*` on `llw0`; `AWDLDataRx*`,
  `AWDLListenHomeRT`, `AWDLListenShared`, `AWDLListenShareFail`, `AWDLTxFwHomeDefer`,
  `AWDLP2PFlushes` and `AWDLP2PFlushSkipped` on the AWDL interface.
- **iPhone Mirroring cannot be ported.** On a MacPro7,1 SMBIOS with Intel Bluetooth
  (BlueToolFixup), the Mac discovers the iPhone, opens the BLE link and the phone
  shows its passcode prompt, then the Mac aborts with "Unable to Connect to iPhone".
  macOS requests a device certificate (DCRT) from `mobileactivationd`, which needs
  an Apple coprocessor (T2 / bridgeOS). A Hackintosh has none: the log shows
  `IODeviceTree:/efi/platform` without `apple-coprocessor-version` and
  `bridgeOS available: false`, so the request fails locally (`MobileActivation`
  Code=-3) before any Apple server is contacted. This is not a Wi-Fi or Bluetooth
  driver fault, and no workaround is known.
- Device support follows the upstream hardware table; Tahoe compatibility still
  requires validation on each adapter.

### Boot-args and kexts for all Continuity features

AirDrop, Handoff, Universal Clipboard and iPhone Instant Hotspot need the Wi-Fi
boot-args and a patched Bluetooth stack. Tested set (MacPro7,1 SMBIOS, Intel AX210,
macOS Tahoe 26.7.3, iPhone 16 Pro), in OpenCore `NVRAM > Add > 7C436110-AB2A-4BBB-A880-FE41995C9F82 > boot-args`:

```
-lilubetaall -itlawdl -itlawdlp2p -itlawdlpromisc -itlawdllisten -itlawdltx -itlawdlaw -itlawdldata -ibtcompatbeta -btlfxadvsets
```

| Boot-arg | Kext | Needed for |
|---|---|---|
| `-itlawdl` `-itlawdlp2p` `-itlawdlpromisc` `-itlawdllisten` `-itlawdltx` `-itlawdlaw` `-itlawdldata` | AirportItlwm | `awdl0`: AirDrop, Universal Clipboard, Handoff data (boot-args are cumulative: start from `-itlawdl`) |
| `-lilubetaall` | Lilu | lets Lilu plugins load on macOS 26 |
| `-ibtcompatbeta` | IntelBTPatcher | lets the patcher load on macOS 26 |
| `-btlfxadvsets` | BlueToolFixup | BLE advertising and connection patches in `bluetoothd` (Handoff, Instant Hotspot) |
| `itlwm_cc=<CC>` (optional) | AirportItlwm | country code, e.g. `itlwm_cc=VN` |
| `itlwm_he=1` (optional) | AirportItlwm | enable Wi-Fi 6 (HE) |
| `-ibtcompatdbg`, `-itlawdlnopeerlisten`, `itlawdllistench=<n>` (optional) | IntelBTPatcher / AirportItlwm | diagnostics only |

Bluetooth kexts used (load order: Lilu first):

- **BlueToolFixup** with the `-btlfxadvsets` patch set for macOS 26.7.3:
  [minhnhan-tran/BlueToolFixup-FixA2DP-BLE](https://github.com/minhnhan-tran/BlueToolFixup-FixA2DP-BLE/tree/pr/btlfx-advsets)
  (branch `pr/btlfx-advsets`). Upstream BlueToolFixup does not have `-btlfxadvsets`,
  and the patches are matched to the macOS 26.7.3 (25G342) `bluetoothd`. All results in
  this README were measured with BlueToolFixup **2.7.5** from that branch. The branch is
  now at 2.7.7 (adds a Filter Accept List RPA substitution for BLE connects to the iPhone);
  2.7.6 and 2.7.7 have not been tested together with this driver.
- **IntelBluetoothFirmware / IntelBTPatcher / IntelBluetoothInjector**:
  [minhnhan-tran/IntelBluetoothFirmware](https://github.com/minhnhan-tran/IntelBluetoothFirmware)
  (IntelBTPatcher 2.6.1 or later).
- **Instant Hotspot also needs the iPhone's IRK** in the controller's resolving list,
  or the Mac's Bluetooth LE connect to the iPhone times out. Set it once with
  `sudo scripts/ibt-irk.sh set "Public <iPhone BLE address>"` (from that repo; reads the
  System keychain, never prints the key), then persist it across reboots with
  `sudo scripts/ibt-irk.sh oc <EFI>/EFI/OC/config.plist`. The key can also be given
  as boot-arg `ibtirk=` or NVRAM `ibt-irk`. Without a key IntelBTPatcher changes
  nothing, and Instant Hotspot from the Wi-Fi menu does not wake the hotspot.
- NVRAM `bluetoothExternalDongleFailed` and `bluetoothInternalControllerInfo` must be set
  through OpenCore (see the BlueToolFixup README).

Download Sequoia or Tahoe **Release** and **Debug** builds from [this fork's Releases](https://github.com/minhnhan-tran/AirportItlwm/releases).
CI builds both systems and updates the alpha release after all builds pass on `main`.

To build locally, install Xcode and Python 3, and check out MacKernelSDK commit
`3f750085caa17ec3a7880f11c11bf4f48cd6a164` in `MacKernelSDK/`:

```sh
xcodebuild -project itlwm.xcodeproj -scheme AirportItlwm-Tahoe \
  -configuration Release ARCHS=x86_64 CODE_SIGNING_ALLOWED=NO \
  SYMROOT="$PWD/build" build
```

With `SYMROOT="$PWD/build"` the unsigned kext is written to
`build/Release/Tahoe/AirportItlwm.kext` (`build/Release/Sequoia/...` for the
Sequoia scheme); `build/` is git-ignored. Without it, Xcode places products
under `~/Library/Developer/Xcode/DerivedData/`.

## Documentation

We highly recommend exploring our documentation before using this Kernel Extension:

- [Intro](https://OpenIntelWireless.github.io/itlwm)
- [Compatibility](https://openintelwireless.github.io/itlwm/Compat)
- [FAQ](https://openintelwireless.github.io/itlwm/FAQ)

## AirportItlwm on macOS Sequoia

The `AirportItlwm-Sequoia` target supports macOS Sequoia 15.2 and later on x86_64
through the native Wi-Fi interface. It has been tested on macOS 15.8 (24H23).
Sequoia and Tahoe use separate ABI headers and build products.

With the same MacKernelSDK checkout described above:

```sh
xcodebuild -project itlwm.xcodeproj -scheme AirportItlwm-Sequoia \
  -configuration Release ARCHS=x86_64 CODE_SIGNING_ALLOWED=NO build
```

Use this kext only on Darwin 24.2.0–24.99.99. Keep the Tahoe kext restricted to
Darwin 25.x when both systems share an OpenCore configuration. HE remains opt-in
with `itlwm_he=1`, and the Tahoe feature limitations above also apply to Sequoia.

## Questions and Issues

Check out our [FAQ Page](https://openintelwireless.github.io/itlwm/FAQ) for more info.

If you have other questions or feedback, feel free to [![Join the chat at https://gitter.im/OpenIntelWireless/itlwm](https://badges.gitter.im/OpenIntelWireless/itlwm.svg)](https://gitter.im/OpenIntelWireless/itlwm?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge).

We only accept bug reports in GitHub Issues, before opening an issue, you're recommended to reconfirm it with us on [Gitter](https://gitter.im/OpenIntelWireless/itlwm); once it's confirmed, please use the provided issue template.

## Credits

- [Acidanthera](https://github.com/acidanthera) for [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK)
- [Apple](https://www.apple.com) for [macOS](https://www.apple.com/macos)
- [AppleIntelWiFi](https://github.com/AppleIntelWiFi) for [Black80211-Catalina](https://github.com/AppleIntelWiFi/Black80211-Catalina)
- [ErrorErrorError](https://github.com/ErrorErrorError) for UserClient bug fixes
- [Intel](https://www.intel.com) for [Wireless Adapter Firmwares](https://www.intel.com/content/www/us/en/support/articles/000005511/network-and-io/wireless.html) and [iwlwifi](https://wireless.wiki.kernel.org/en/users/drivers/iwlwifi)
- [Linux](https://www.kernel.org) for [iwlwifi](https://wireless.wiki.kernel.org/en/users/drivers/iwlwifi)
- [mercurysquad](https://github.com/mercurysquad) for [Voodoo80211](https://github.com/mercurysquad/Voodoo80211)
- [OpenBSD](https://openbsd.org) for [net80211, iwn, iwm, and iwx](https://github.com/openbsd/src)
- [pigworlds](https://github.com/OpenIntelWireless/itlwm/commits?author=pigworlds) for DVM devices support, MIRA bug fixes, and Tx aggregation for MVM Gen 1 devices
- [rpeshkov](https://github.com/rpeshkov) for [black80211](https://github.com/rpeshkov/black80211)
- [usr-sse2](https://github.com/usr-sse2) for implementing the usage of Apple RSN Supplicant and bug fixes
- [zxystd](https://github.com/zxystd) for developing [itlwm](https://github.com/OpenIntelWireless/itlwm)

## Acknowledgements

- [@penghubingzhou](https://github.com/startpenghubingzhou)
- [@Bat.bat](https://github.com/williambj1)
- [@iStarForever](https://github.com/XStar-Dev)
- [@stevezhengshiqi](https://github.com/stevezhengshiqi)
- [@DogAndPot](https://github.com/DogAndPot) for providing resources and help for system configuration
- [@Daliansky](https://github.com/Daliansky) for providing Wi-Fi cards
