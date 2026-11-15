[![Version](https://img.shields.io/github/v/tag/laobamac/itlwm?style=flat&label=Downloads)](https://github.com/laobamac/itlwm/releases)
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
- **Continuity Camera works over USB only.** With the iPhone connected by cable it
  appears as a camera and works (tested in Photo Booth). Wirelessly it does not
  appear in FaceTime or Photo Booth: the Mac's Continuity Camera agent reads the
  iPhone as `nearby:0` (the same iPhone reads `nearby:1` on an Apple Silicon Mac),
  and the agent's logic only offers a wireless camera once that flag is set.
  `nearbyd` on this setup logs "Ranging is not supported on this platform" and
  interrupts its sessions with "BluetoothNotAvailable", which may implicate the
  Bluetooth stack rather than the Wi-Fi driver; this is unconfirmed. There is no
  preference or boot-arg to bypass the check (`CMCaptureDebug` only controls debug
  logging). The wireless video path over AWDL is therefore untested, and the cause
  is undetermined.
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

Download Sequoia or Tahoe **Release** and **Debug** builds from [this fork's Releases](https://github.com/laobamac/itlwm/releases).
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
