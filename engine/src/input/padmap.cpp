#include "wp/padmap.h"

#include "wp/input.h"
#include "wp/settings.h"

namespace wp::padmap {

namespace {

struct Action {
    const char* name;
    uint32_t button;
};

constexpr Action kActions[kActionCount] = {
    {"a", input::kButtonA},         {"b", input::kButtonB},         {"one", input::kButtonOne}, {"two", input::kButtonTwo},
    {"plus", input::kButtonPlus}, {"minus", input::kButtonMinus}, {"home", input::kButtonHome},
    {"shake", input::kMotionShake}, {"swing_up", input::kMotionSwingUp}, {"swing_down", input::kMotionSwingDown},
    {"tilt_left", input::kMotionTiltLeft}, {"tilt_right", input::kMotionTiltRight}, {"tilt_up", input::kMotionTiltUp}, {"tilt_down", input::kMotionTiltDown},
};

constexpr const char* kPhysicalNames[kPhysicalCount] = {
    "south", "east", "west", "north", "back", "guide", "start", "left_stick", "right_stick",
    "left_shoulder", "right_shoulder", "dpad_up", "dpad_down", "dpad_left", "dpad_right", "left_trigger", "right_trigger",
    "right_stick_up", "right_stick_down", "right_stick_left", "right_stick_right",
};

uint32_t automatic(const Pressed& pressed, bool sideways) {
    auto held = [&](Physical physical) { return pressed.held[physical]; };
    uint32_t b = 0;
    if (held(kRightShoulder)) {
        b |= input::kButtonA;
    }
    if (held(kRightTrigger)) {
        b |= input::kButtonB;
    }
    if (sideways) {
        if (held(kSouth)) {
            b |= input::kButtonTwo;
        }
        if (held(kWest) || held(kEast)) {
            b |= input::kButtonOne;
        }
        if (held(kNorth)) {
            b |= input::kButtonA;
        }
    } else {
        if (held(pressed.nintendo_layout ? kEast : kSouth)) {
            b |= input::kButtonA;
        }
        if (held(pressed.nintendo_layout ? kSouth : kEast)) {
            b |= input::kButtonB;
        }
        if (held(kWest)) {
            b |= input::kButtonOne;
        }
        if (held(kNorth)) {
            b |= input::kButtonTwo;
        }
    }
    if (held(kBack)) {
        b |= input::kButtonMinus;
    }
    if (held(kStart)) {
        b |= input::kButtonPlus;
    }
    if (held(kGuide)) {
        b |= input::kButtonHome;
    }
    return b;
}

uint32_t automatic_motion(const Pressed& pressed) {
    auto held = [&](Physical physical) { return pressed.held[physical]; };
    uint32_t b = 0;
    if (held(kLeftTrigger)) {
        b |= input::kMotionShake;
    }
    if (held(kLeftShoulder)) {
        if (held(kDpadUp)) {
            b |= input::kMotionSwingUp;
        }
        if (held(kDpadDown)) {
            b |= input::kMotionSwingDown;
        }
        if (held(kRightStickLeft)) {
            b |= input::kMotionTiltLeft;
        }
        if (held(kRightStickRight)) {
            b |= input::kMotionTiltRight;
        }
        if (held(kRightStickUp)) {
            b |= input::kMotionTiltUp;
        }
        if (held(kRightStickDown)) {
            b |= input::kMotionTiltDown;
        }
    }
    return b;
}

}

const char* action_name(size_t action) {
    return action < kActionCount ? kActions[action].name : "";
}

uint32_t action_button(size_t action) {
    return action < kActionCount ? kActions[action].button : 0;
}

std::string setting_key(size_t action) {
    return std::string("pad.") + action_name(action);
}

const char* physical_name(int physical) {
    return physical >= 0 && physical < kPhysicalCount ? kPhysicalNames[physical] : "auto";
}

int physical_from_name(const std::string& name) {
    for (int i = 0; i < kPhysicalCount; i++) {
        if (name == kPhysicalNames[i]) {
            return i;
        }
    }
    return kAutomatic;
}

Mapping current_mapping() {
    Mapping mapping{};
    for (size_t i = 0; i < kActionCount; i++) {
        mapping[i] = physical_from_name(settings::text(setting_key(i).c_str(), nullptr));
    }
    return mapping;
}

uint32_t buttons(const Pressed& pressed, bool sideways, const Mapping& mapping) {
    bool custom = false;
    for (size_t i = 0; i < kButtonActionCount; i++) {
        custom = custom || mapping[i] != kAutomatic;
    }
    uint32_t b = 0;
    Pressed for_buttons = pressed;
    for (size_t i = kButtonActionCount; i < kActionCount; i++) {
        if (mapping[i] != kAutomatic) {
            for_buttons.held[mapping[i]] = false;
        }
    }
    if (!custom || sideways) {
        b = automatic(for_buttons, sideways);
    } else {
        Pressed unmapped = for_buttons;
        for (int physical : mapping) {
            if (physical != kAutomatic) {
                unmapped.held[physical] = false;
            }
        }
        uint32_t defaults = automatic(unmapped, false);
        for (size_t i = 0; i < kButtonActionCount; i++) {
            if (mapping[i] == kAutomatic) {
                b |= defaults & kActions[i].button;
            } else if (pressed.held[mapping[i]]) {
                b |= kActions[i].button;
            }
        }
    }
    Pressed free = pressed;
    for (int physical : mapping) {
        if (physical != kAutomatic) {
            free.held[physical] = false;
        }
    }
    uint32_t motion = automatic_motion(free);
    for (size_t i = kButtonActionCount; i < kActionCount; i++) {
        if (mapping[i] == kAutomatic) {
            b |= motion & kActions[i].button;
        } else if (pressed.held[mapping[i]]) {
            b |= kActions[i].button;
        }
    }
    return b;
}

std::string device_key(const std::string& name, int ordinal) {
    return name + "#" + std::to_string(ordinal + 1);
}

bool listed(const std::string& list, const std::string& key) {
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(';', start);
        std::string item = list.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (item == key) {
            return true;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

std::string toggled(const std::string& list, const std::string& key) {
    std::string result;
    bool found = false;
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(';', start);
        std::string item = list.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (item == key) {
            found = true;
        } else if (!item.empty()) {
            result += (result.empty() ? "" : ";") + item;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    if (!found) {
        result += (result.empty() ? "" : ";") + key;
    }
    return result;
}

}
