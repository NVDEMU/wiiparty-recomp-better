#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wp::padmap {

enum Physical {
    kSouth,
    kEast,
    kWest,
    kNorth,
    kBack,
    kGuide,
    kStart,
    kLeftStick,
    kRightStick,
    kLeftShoulder,
    kRightShoulder,
    kDpadUp,
    kDpadDown,
    kDpadLeft,
    kDpadRight,
    kLeftTrigger,
    kRightTrigger,
    kRightStickUp,
    kRightStickDown,
    kRightStickLeft,
    kRightStickRight,
    kPhysicalCount
};

constexpr int kAutomatic = -1;
constexpr size_t kButtonActionCount = 7;
constexpr size_t kActionCount = 14;

struct Pressed {
    std::array<bool, kPhysicalCount> held{};
    bool nintendo_layout = false;
};

using Mapping = std::array<int, kActionCount>;

const char* action_name(size_t action);
uint32_t action_button(size_t action);
std::string setting_key(size_t action);
const char* physical_name(int physical);
int physical_from_name(const std::string& name);
Mapping current_mapping();
uint32_t buttons(const Pressed& pressed, bool sideways, const Mapping& mapping);

std::string device_key(const std::string& name, int ordinal);
bool listed(const std::string& list, const std::string& key);
std::string toggled(const std::string& list, const std::string& key);
int assigned(const std::string& list, const std::string& key);
std::string assign(const std::string& list, const std::string& key, int player);
std::vector<int> plan_slots(const std::vector<std::string>& keys, const std::vector<int>& current, const std::string& ignored, const std::string& assignments,
                            bool player1_keyboard_only);

}
