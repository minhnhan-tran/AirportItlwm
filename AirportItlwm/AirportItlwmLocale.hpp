//
//  AirportItlwmLocale.hpp
//  Regulatory-locale and scan-age helpers shared by the legacy STA ioctl
//  path, the Skywalk interface and the V2 (Sonoma+/WCL) controller.
//

#ifndef AirportItlwmLocale_hpp
#define AirportItlwmLocale_hpp

#include <libkern/c++/OSString.h>
#include <IOKit/IORegistryEntry.h>

#define kAirportLocaleProperty "IO80211Locale"

/* `list` is "XX YY ZZ": 2-letter codes on a 3-char stride. No libc calls, so
 * the kext gains no new kernel symbol dependencies. */
static inline bool
airportCountryIn(const char *list, const char *code)
{
    for (const char *p = list; p[0] && p[1]; p += 3) {
        if (p[0] == code[0] && p[1] == code[1])
            return true;
        if (p[2] == '\0')
            break;
    }
    return false;
}

/*
 * Best-effort ISO 3166 alpha-2 -> Apple regulatory locale mapping. Apple's
 * own table is internal; this only has to be plausible, since the locale is
 * informational (System Information) and does not drive channel selection.
 */
static inline enum apple80211_locale
airportLocaleForCountry(const char *cc)
{
    static const char fcc[]  = "US CA PR GU VI AS MP UM";
    static const char etsi[] = "AT BE BG HR CY CZ DK EE FI FR DE GR HU IE IT LV "
                               "LT LU MT NL PL PT RO SK SI ES SE IS LI NO GB CH";
    static const char apac[] = "CN HK MO TW SG MY TH VN PH IN AU NZ BD LK PK KH "
                               "LA MM BN NP MN";
    char code[3];

    if (!cc || !cc[0] || !cc[1])
        return APPLE80211_LOCALE_UNKNOWN;
    code[0] = (cc[0] >= 'a' && cc[0] <= 'z') ? cc[0] - 'a' + 'A' : cc[0];
    code[1] = (cc[1] >= 'a' && cc[1] <= 'z') ? cc[1] - 'a' + 'A' : cc[1];
    code[2] = '\0';
    if (code[0] == 'Z' && code[1] == 'Z')   /* firmware "world" domain */
        return APPLE80211_LOCALE_ROW;
    if (code[0] == 'J' && code[1] == 'P')
        return APPLE80211_LOCALE_JAPAN;
    if (code[0] == 'K' && code[1] == 'R')
        return APPLE80211_LOCALE_KOREA;
    if (code[0] == 'I' && code[1] == 'D')
        return APPLE80211_LOCALE_INDONESIA;
    if (airportCountryIn(fcc, code))
        return APPLE80211_LOCALE_FCC;
    if (airportCountryIn(etsi, code))
        return APPLE80211_LOCALE_ETSI;
    if (airportCountryIn(apac, code))
        return APPLE80211_LOCALE_APAC;
    return APPLE80211_LOCALE_ROW;
}

static inline const char *
airportLocaleName(enum apple80211_locale locale)
{
    switch (locale) {
        case APPLE80211_LOCALE_FCC:       return "FCC";
        case APPLE80211_LOCALE_ETSI:      return "ETSI";
        case APPLE80211_LOCALE_JAPAN:     return "Japan";
        case APPLE80211_LOCALE_KOREA:     return "Korea";
        case APPLE80211_LOCALE_APAC:      return "APAC";
        case APPLE80211_LOCALE_ROW:       return "ROW";
        case APPLE80211_LOCALE_INDONESIA: return "Indonesia";
        default:                          return "Unknown";
    }
}

/*
 * The Sequoia/Tahoe (WCL) ABI never issues APPLE80211_IOC_LOCALE, so the
 * family leaves IO80211Locale at "Unknown". Publish it from the country code.
 */
static inline void
airportPublishLocale(OSObject *object, const char *cc)
{
    IORegistryEntry *entry = OSDynamicCast(IORegistryEntry, object);
    const char *name;
    OSString *current;

    if (!entry)
        return;
    name = airportLocaleName(airportLocaleForCountry(cc));
    current = OSDynamicCast(OSString, entry->getProperty(kAirportLocaleProperty));
    if (current && current->isEqualTo(name))
        return;
    entry->setProperty(kAirportLocaleProperty, name);
}

/* Milliseconds since a node was last heard, saturating instead of wrapping. */
static inline uint32_t
airportNodeAgeMs(uint64_t lastSeenMs)
{
    uint64_t now = airport_up_time();

    if (lastSeenMs == 0)                    /* never heard: report as stale */
        return UINT32_MAX;
    if (lastSeenMs >= now)
        return 0;
    return (now - lastSeenMs) > UINT32_MAX ? UINT32_MAX : (uint32_t)(now - lastSeenMs);
}

#endif /* AirportItlwmLocale_hpp */