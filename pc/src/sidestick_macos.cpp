/**
 * @file sidestick_macos.cpp
 * @brief macOS Sidestick backend using IOKit's IOHIDManager.
 *
 * GameController.framework only exposes MFi/Xbox/PlayStation-style gamepads,
 * so generic HID flight sticks like the TCA Sidestick are invisible to it.
 * IOHIDManager reads the HID joystick directly with no extra dependencies.
 */

#include "sidestick.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>

#include <cstdio>
#include <initializer_list>
#include <stdexcept>

namespace hermes {

namespace {

constexpr long THRUSTMASTER_VID = 0x044F;

struct HidState {
    IOHIDManagerRef manager = nullptr;
    IOHIDDeviceRef  device  = nullptr;
    IOHIDElementRef x       = nullptr;
    IOHIDElementRef y       = nullptr;
    IOHIDElementRef rz      = nullptr;
    IOHIDElementRef buttons[Sidestick::MAX_BUTTONS] = {};
    int             buttonCount = 0;
};

long intProperty(IOHIDDeviceRef dev, CFStringRef key) {
    long value = 0;
    auto ref = static_cast<CFNumberRef>(IOHIDDeviceGetProperty(dev, key));
    if (ref && CFGetTypeID(ref) == CFNumberGetTypeID()) {
        CFNumberGetValue(ref, kCFNumberLongType, &value);
    }
    return value;
}

CFDictionaryRef matchUsage(int page, int usage) {
    CFNumberRef p = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &page);
    CFNumberRef u = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage);
    const void* keys[] = { CFSTR(kIOHIDDeviceUsagePageKey), CFSTR(kIOHIDDeviceUsageKey) };
    const void* vals[] = { p, u };
    CFDictionaryRef dict = CFDictionaryCreate(kCFAllocatorDefault, keys, vals, 2,
                                              &kCFTypeDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks);
    CFRelease(p);
    CFRelease(u);
    return dict;
}

void cacheElements(HidState& s) {
    CFArrayRef elems = IOHIDDeviceCopyMatchingElements(s.device, nullptr, kIOHIDOptionsTypeNone);
    if (!elems) return;

    for (CFIndex i = 0; i < CFArrayGetCount(elems); ++i) {
        auto el = (IOHIDElementRef)CFArrayGetValueAtIndex(elems, i);
        const IOHIDElementType type = IOHIDElementGetType(el);
        if (type != kIOHIDElementTypeInput_Misc && type != kIOHIDElementTypeInput_Axis &&
            type != kIOHIDElementTypeInput_Button) {
            continue;
        }

        const uint32_t page  = IOHIDElementGetUsagePage(el);
        const uint32_t usage = IOHIDElementGetUsage(el);

        if (page == kHIDPage_GenericDesktop) {
            if      (usage == kHIDUsage_GD_X  && !s.x)  s.x  = (IOHIDElementRef)CFRetain(el);
            else if (usage == kHIDUsage_GD_Y  && !s.y)  s.y  = (IOHIDElementRef)CFRetain(el);
            else if (usage == kHIDUsage_GD_Rz && !s.rz) s.rz = (IOHIDElementRef)CFRetain(el);
        } else if (page == kHIDPage_Button && usage >= 1 &&
                   usage <= static_cast<uint32_t>(Sidestick::MAX_BUTTONS)) {
            const int idx = static_cast<int>(usage) - 1;
            if (!s.buttons[idx]) {
                s.buttons[idx] = (IOHIDElementRef)CFRetain(el);
                if (idx + 1 > s.buttonCount) s.buttonCount = idx + 1;
            }
        }
    }
    CFRelease(elems);
}

/** Read an axis element and normalise its logical range to [-1.0, 1.0]. */
float readAxis(IOHIDDeviceRef dev, IOHIDElementRef el) {
    if (!el) return 0.0f;
    IOHIDValueRef val = nullptr;
    if (IOHIDDeviceGetValue(dev, el, &val) != kIOReturnSuccess || !val) return 0.0f;

    const CFIndex lo  = IOHIDElementGetLogicalMin(el);
    const CFIndex hi  = IOHIDElementGetLogicalMax(el);
    const CFIndex raw = IOHIDValueGetIntegerValue(val);
    if (hi <= lo) return 0.0f;
    return 2.0f * static_cast<float>(raw - lo) / static_cast<float>(hi - lo) - 1.0f;
}

bool readButton(IOHIDDeviceRef dev, IOHIDElementRef el) {
    if (!el) return false;
    IOHIDValueRef val = nullptr;
    if (IOHIDDeviceGetValue(dev, el, &val) != kIOReturnSuccess || !val) return false;
    return IOHIDValueGetIntegerValue(val) != 0;
}

} // namespace

Sidestick::Sidestick() : handle_(nullptr), mode_(Mode::ACKERMANN), prevActions_(0u) {
    auto* s = new HidState;
    s->manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);

    CFDictionaryRef joystick = matchUsage(kHIDPage_GenericDesktop, kHIDUsage_GD_Joystick);
    IOHIDManagerSetDeviceMatching(s->manager, joystick);
    CFRelease(joystick);

    IOHIDManagerScheduleWithRunLoop(s->manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    if (IOHIDManagerOpen(s->manager, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
        CFRelease(s->manager);
        delete s;
        throw std::runtime_error("IOHIDManagerOpen failed");
    }

    // Device enumeration is async — spin the run loop up to 3 s so a freshly
    // plugged stick has time to register before we take a snapshot.
    CFSetRef devices = nullptr;
    for (int i = 0; i < 30; ++i) {
        devices = IOHIDManagerCopyDevices(s->manager);
        if (devices && CFSetGetCount(devices) > 0) break;
        if (devices) { CFRelease(devices); devices = nullptr; }
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
    }

    if (devices) {
        const CFIndex n = CFSetGetCount(devices);
        const void** list = new const void*[n];
        CFSetGetValues(devices, list);
        // Prefer a Thrustmaster device; otherwise take the first joystick
        for (CFIndex i = 0; i < n; ++i) {
            auto dev = (IOHIDDeviceRef)list[i];
            if (!s->device || intProperty(dev, CFSTR(kIOHIDVendorIDKey)) == THRUSTMASTER_VID) {
                s->device = dev;
            }
        }
        if (s->device) CFRetain(s->device);
        delete[] list;
        CFRelease(devices);
    }

    if (!s->device) {
        IOHIDManagerClose(s->manager, kIOHIDOptionsTypeNone);
        CFRelease(s->manager);
        delete s;
        throw std::runtime_error(
            "No joystick detected — make sure the TCA Sidestick is plugged in via USB");
    }

    cacheElements(*s);
    if (!s->x || !s->y || !s->rz) {
        std::fprintf(stderr, "[hermes-stick] warning: missing axis (X=%d Y=%d Rz=%d)\n",
                     s->x != nullptr, s->y != nullptr, s->rz != nullptr);
    }

    handle_ = s;
}

Sidestick::~Sidestick() {
    auto* s = static_cast<HidState*>(handle_);
    if (!s) return;
    for (IOHIDElementRef el : { s->x, s->y, s->rz }) if (el) CFRelease(el);
    for (IOHIDElementRef el : s->buttons) if (el) CFRelease(el);
    CFRelease(s->device);
    IOHIDManagerClose(s->manager, kIOHIDOptionsTypeNone);
    CFRelease(s->manager);
    delete s;
    handle_ = nullptr;
}

void Sidestick::read(Raw& raw) {
    // Let IOKit deliver pending HID reports so element values are current
    while (CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true) == kCFRunLoopRunHandledSource) {}

    auto* s = static_cast<HidState*>(handle_);
    raw.x  = readAxis(s->device, s->x);
    raw.y  = readAxis(s->device, s->y);
    raw.rz = readAxis(s->device, s->rz);
    raw.buttonCount = s->buttonCount;
    for (int i = 0; i < s->buttonCount; ++i) {
        raw.buttons[i] = readButton(s->device, s->buttons[i]);
    }
}

} // namespace hermes
