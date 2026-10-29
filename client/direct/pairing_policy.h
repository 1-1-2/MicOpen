#pragma once
#include <winrt/Windows.Devices.Enumeration.h>

// A generic result may disagree with the freshly queried AEP state. Only a
// positive paired record can recover that one result; refusals/cancellation
// must never fall through to opening a session.
inline bool mic_direct_pairing_allows_session(
    winrt::Windows::Devices::Enumeration::DevicePairingResultStatus result,
    bool freshlyPaired) {
    using Status = winrt::Windows::Devices::Enumeration::DevicePairingResultStatus;
    return result == Status::Paired || result == Status::AlreadyPaired
        || (result == Status::Failed && freshlyPaired);
}
