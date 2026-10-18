#include "SystemOutputMute.h"

#include "LogManager.h"

#ifdef Q_OS_MACOS
#include <AudioToolbox/AudioServices.h>
#include <CoreAudio/CoreAudio.h>
#endif

namespace AetherSDR {

#ifdef Q_OS_MACOS
namespace {

AudioObjectPropertyAddress outputAddress(AudioObjectPropertySelector selector)
{
    return {selector, kAudioDevicePropertyScopeOutput,
            kAudioObjectPropertyElementMain};
}

AudioObjectID defaultOutputDevice()
{
    AudioObjectID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    const AudioObjectPropertyAddress address{
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0,
                                   nullptr, &size, &device) != noErr) {
        return kAudioObjectUnknown;
    }
    return device;
}

bool isSettable(AudioObjectID device, const AudioObjectPropertyAddress& address)
{
    Boolean settable = false;
    return AudioObjectHasProperty(device, &address)
        && AudioObjectIsPropertySettable(device, &address, &settable) == noErr
        && settable;
}

template <typename T>
bool readProperty(AudioObjectID device, const AudioObjectPropertyAddress& address,
                  T& value)
{
    UInt32 size = sizeof(T);
    return AudioObjectGetPropertyData(device, &address, 0, nullptr, &size,
                                      &value) == noErr;
}

template <typename T>
bool writeProperty(AudioObjectID device, const AudioObjectPropertyAddress& address,
                   T value)
{
    return AudioObjectSetPropertyData(device, &address, 0, nullptr,
                                      sizeof(T), &value) == noErr;
}

}  // namespace
#endif

SystemOutputMute::SystemOutputMute(QObject* parent)
    : QObject(parent)
{
}

SystemOutputMute::~SystemOutputMute()
{
    release();
}

bool SystemOutputMute::isSupported()
{
#ifdef Q_OS_MACOS
    return true;
#else
    return false;
#endif
}

void SystemOutputMute::setEnabled(bool on)
{
    if (m_enabled == on) {
        return;
    }
    m_enabled = on;
    if (m_enabled && m_transmitting) {
        engage();
    } else if (!m_enabled) {
        release();
    }
}

void SystemOutputMute::setTransmitting(bool tx)
{
    if (m_transmitting == tx) {
        return;
    }
    m_transmitting = tx;
    if (tx && m_enabled) {
        engage();
    } else if (!tx) {
        release();
    }
}

void SystemOutputMute::engage()
{
    if (m_engaged) {
        return;
    }
#ifdef Q_OS_MACOS
    const AudioObjectID device = defaultOutputDevice();
    if (device == kAudioObjectUnknown) {
        qCWarning(lcAudio) << "TX Mute: no default output device";
        return;
    }

    const auto muteAddr = outputAddress(kAudioDevicePropertyMute);
    if (isSettable(device, muteAddr)) {
        UInt32 alreadyMuted = 0;
        readProperty(device, muteAddr, alreadyMuted);
        if (alreadyMuted) {
            return;   // the operator muted it; leave it to them
        }
        if (writeProperty(device, muteAddr, UInt32{1})) {
            m_method = Method::Mute;
            m_deviceId = device;
            m_engaged = true;
            emit engagedChanged(true);
        }
        return;
    }

    // No mute control (common on USB and HDMI outputs): turn the device's
    // overall volume down to zero and put it back afterwards.
    const auto volAddr =
        outputAddress(kAudioHardwareServiceDeviceProperty_VirtualMainVolume);
    Float32 volume = 0.0f;
    if (isSettable(device, volAddr) && readProperty(device, volAddr, volume)) {
        if (volume <= 0.0f) {
            return;
        }
        if (writeProperty(device, volAddr, Float32{0.0f})) {
            m_method = Method::Volume;
            m_deviceId = device;
            m_savedVolume = volume;
            m_engaged = true;
            emit engagedChanged(true);
        }
        return;
    }
    qCWarning(lcAudio) << "TX Mute: the default output device has no mute or volume control";
#endif
}

void SystemOutputMute::release()
{
    if (!m_engaged) {
        return;
    }
#ifdef Q_OS_MACOS
    // Undo on the device that was silenced, even if the default output has
    // changed since key-down.
    if (m_method == Method::Mute) {
        writeProperty(m_deviceId, outputAddress(kAudioDevicePropertyMute), UInt32{0});
    } else if (m_method == Method::Volume) {
        writeProperty(m_deviceId,
                      outputAddress(kAudioHardwareServiceDeviceProperty_VirtualMainVolume),
                      Float32{m_savedVolume});
    }
#endif
    m_method = Method::None;
    m_engaged = false;
    emit engagedChanged(false);
}

} // namespace AetherSDR
