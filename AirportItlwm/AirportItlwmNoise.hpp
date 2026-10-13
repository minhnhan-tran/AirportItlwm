//
//  AirportItlwmNoise.hpp
//  Noise floor / SNR values reported to macOS (link status, legacy scan
//  results and WCL beacon metadata).
//

#ifndef AirportItlwmNoise_hpp
#define AirportItlwmNoise_hpp

#include <stdint.h>

namespace AirportNoise {

/*
 * Typical 20 MHz noise floor. Used until the firmware provides a
 * measurement: reporting 0 dBm makes macOS see SNR 0 for every network.
 */
static constexpr int16_t kDefaultFloorDbm = -95;

/*
 * getBSSNoise() returns the firmware's beacon-silence noise in dBm (negative),
 * 0 if no statistics notification has arrived yet, or -127 when no antenna
 * reported a value. Only physically plausible measurements are passed on.
 */
inline int16_t floorDbm(int driverNoise)
{
    return (driverNoise >= -120 && driverNoise <= -40) ?
        int16_t(driverNoise) : kDefaultFloorDbm;
}

inline int16_t snrDb(int rssiDbm, int16_t noiseDbm)
{
    int snr = rssiDbm - noiseDbm;
    return int16_t(snr < 0 ? 0 : snr);
}

}

#endif /* AirportItlwmNoise_hpp */
