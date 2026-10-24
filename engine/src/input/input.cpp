#include "wp/input.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "wp/game.h"
#include "wp/gamepad.h"
#include "wp/gx.h"
#include "wp/keymap.h"
#include "wp/log.h"
#include "wp/modules.h"
#include "wp/settings.h"
#include "wp/video.h"
#include "wp/runtime.h"

namespace wp::input {

namespace {

constexpr auto kWheelSwing = std::chrono::milliseconds(120);
constexpr int64_t kNoticePulsePeriod = 500;
constexpr int64_t kNoticePulseLength = 100;
std::atomic<int64_t> g_wheel_up{0};
std::atomic<int64_t> g_wheel_down{0};
std::atomic<bool> g_blocked{false};
std::atomic<bool> g_release_wait[4] = {};

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool sideways_minigame() {
    static const settings::LiveFlag automatic("input.auto_grip", "WP_AUTO_ORIENTATION");
    if (!automatic()) {
        return false;
    }
    static int64_t next = 0;
    static bool cached = false;
    int64_t now = now_ms();
    if (now >= next) {
        next = now + 100;
        cached = false;
        const game::Description& game = game::description();
        for (size_t i = 0; i < game.sideways_module_count; i++) {
            if (module_loaded(game.sideways_modules[i])) {
                cached = true;
                break;
            }
        }
    }
    return cached;
}

bool pressed(int key) {
    if (key == keymap::kWheelUp || key == keymap::kWheelDown) {
        int64_t last = (key == keymap::kWheelUp ? g_wheel_up : g_wheel_down).load();
        return now_ms() - last < kWheelSwing.count();
    }
    return video::key_down(key);
}

bool any_pressed(const std::vector<int>& keys) {
    for (int key : keys) {
        if (pressed(key)) {
            return true;
        }
    }
    return false;
}

struct ScriptEntry {
    int from = 0;
    int to = 0;
    uint32_t buttons = 0;
    bool pointer = false;
    float x = 0.0f;
    float y = 0.0f;
    uint32_t channel = 0;
};

std::vector<ScriptEntry> parse_script(const char* text) {
    std::vector<ScriptEntry> entries;
    std::stringstream lines(text);
    std::string line;
    while (std::getline(lines, line, ';')) {
        std::stringstream fields(line);
        std::string field;
        std::vector<std::string> parts;
        while (std::getline(fields, field, ',')) {
            parts.push_back(field);
        }
        if (parts.size() < 3) {
            continue;
        }
        ScriptEntry entry;
        entry.from = std::atoi(parts[0].c_str());
        entry.to = std::atoi(parts[1].c_str());
        entry.buttons = static_cast<uint32_t>(std::strtoul(parts[2].c_str(), nullptr, 16));
        if (parts.size() >= 7) {
            entry.pointer = parts[3] == "1";
            entry.x = static_cast<float>(std::atof(parts[4].c_str()));
            entry.y = static_cast<float>(std::atof(parts[5].c_str()));
            entry.channel = static_cast<uint32_t>(std::strtoul(parts[6].c_str(), nullptr, 10));
        } else if (parts.size() >= 5) {
            entry.pointer = true;
            entry.x = static_cast<float>(std::atof(parts[3].c_str()));
            entry.y = static_cast<float>(std::atof(parts[4].c_str()));
            if (parts.size() >= 6) entry.channel = static_cast<uint32_t>(std::strtoul(parts[5].c_str(), nullptr, 10));
        }
        entries.push_back(entry);
    }
    return entries;
}

Sample scripted(const std::vector<ScriptEntry>& script, int milliseconds, uint32_t channel) {
    Sample result;
    for (const ScriptEntry& entry : script) {
        if (entry.channel == channel && milliseconds >= entry.from && milliseconds < entry.to) {
            result.buttons |= entry.buttons;
            if (entry.pointer) {
                result.pointer_valid = true;
                result.pointer_x = entry.x;
                result.pointer_y = entry.y;
            }
        }
    }
    return result;
}

std::mutex g_replay_mutex;
bool g_replay_recording = false;
bool g_replay_playing = false;
std::chrono::steady_clock::time_point g_record_origin;
std::chrono::steady_clock::time_point g_play_origin;
std::vector<ScriptEntry> g_record_segments;
std::array<ScriptEntry, 4> g_record_current{};
std::array<bool, 4> g_record_started{};
std::vector<ScriptEntry> g_replay_segments;
int g_replay_duration = 0;

std::filesystem::path replay_path() {
    return std::filesystem::path(game::description().data_directory) / "replays" / "last.wpr";
}

int elapsed_ms(std::chrono::steady_clock::time_point origin) {
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - origin).count());
}

bool same_state(const ScriptEntry& a, const Sample& b) {
    return a.buttons == b.buttons && a.pointer == b.pointer_valid &&
           (!a.pointer || (a.x == b.pointer_x && a.y == b.pointer_y));
}

void record_sample(uint32_t channel, const Sample& sample) {
    if (channel >= g_record_current.size()) return;
    std::lock_guard<std::mutex> lock(g_replay_mutex);
    if (!g_replay_recording) return;
    int now = std::max(0, elapsed_ms(g_record_origin));
    if (!g_record_started[channel]) {
        ScriptEntry entry;
        entry.from = now; entry.to = now + 1; entry.buttons = sample.buttons;
        entry.pointer = sample.pointer_valid; entry.x = sample.pointer_x; entry.y = sample.pointer_y; entry.channel = channel;
        g_record_current[channel] = entry;
        g_record_started[channel] = true;
        return;
    }
    ScriptEntry& current = g_record_current[channel];
    if (same_state(current, sample)) {
        current.to = std::max(current.to, now + 1);
        return;
    }
    current.to = std::max(now, current.from + 1);
    g_record_segments.push_back(current);
    current.from = now; current.to = now + 1; current.buttons = sample.buttons;
    current.pointer = sample.pointer_valid; current.x = sample.pointer_x; current.y = sample.pointer_y; current.channel = channel;
}

bool playback_sample(uint32_t channel, Sample& result) {
    std::lock_guard<std::mutex> lock(g_replay_mutex);
    if (!g_replay_playing) return false;
    int now = elapsed_ms(g_play_origin);
    if (now > g_replay_duration) {
        g_replay_playing = false;
        return false;
    }
    result = scripted(g_replay_segments, now, channel);
    return true;
}

void save_replay_recording() {
    std::vector<ScriptEntry> segments;
    {
        std::lock_guard<std::mutex> lock(g_replay_mutex);
        int end = std::max(1, elapsed_ms(g_record_origin) + 1);
        for (size_t i = 0; i < g_record_current.size(); i++) {
            if (g_record_started[i]) {
                g_record_current[i].to = std::max(end, g_record_current[i].from + 1);
                g_record_segments.push_back(g_record_current[i]);
                g_record_started[i] = false;
            }
        }
        segments.swap(g_record_segments);
    }
    std::filesystem::path path = replay_path();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    for (const ScriptEntry& entry : segments) {
        file << entry.from << ',' << entry.to << ',' << std::hex << entry.buttons << std::dec << ','
             << (entry.pointer ? 1 : 0) << ',' << entry.x << ',' << entry.y << ',' << entry.channel << ';';
    }
    file.flush();
    if (file) {
        std::fprintf(stderr, "replay saved: %s (%zu input segments)\n", path.string().c_str(), segments.size());
        log::write("replay", "saved %s (%zu input segments)", path.string().c_str(), segments.size());
    } else {
        std::fprintf(stderr, "replay could not be saved: %s\n", path.string().c_str());
        log::write("replay", "could not save %s", path.string().c_str());
    }
}

}

const keymap::Bindings& bindings() {
    static std::mutex mutex;
    static std::atomic<const keymap::Bindings*> current{nullptr};
    static std::atomic<uint32_t> built{0};
    uint32_t revision = settings::revision();
    if (built.load() != revision || !current.load()) {
        std::lock_guard<std::mutex> lock(mutex);
        if (built.load() != revision || !current.load()) {
            std::vector<std::string> invalid;
            keymap::Bindings* result = new keymap::Bindings(keymap::load([](const std::string& key) { return settings::text(key.c_str(), nullptr); }, invalid));
            for (const std::string& entry : invalid) {
                std::fprintf(stderr, "settings: ignored key name %s", entry.c_str());
                std::fputc(10, stderr);
                log::write("input", "ignored key name %s", entry.c_str());
            }
            current.store(result);
            built.store(revision);
        }
    }
    return *current.load();
}

void note_wheel(int delta) {
    if (delta > 0) {
        g_wheel_up = now_ms();
    } else if (delta < 0) {
        g_wheel_down = now_ms();
    }
}

bool replay_recording() {
    std::lock_guard<std::mutex> lock(g_replay_mutex);
    return g_replay_recording;
}

bool replay_playing() {
    std::lock_guard<std::mutex> lock(g_replay_mutex);
    return g_replay_playing;
}

void toggle_replay_recording() {
    bool save = false;
    {
        std::lock_guard<std::mutex> lock(g_replay_mutex);
        if (g_replay_recording) {
            g_replay_recording = false;
            save = true;
        } else {
            g_replay_playing = false;
            g_replay_segments.clear();
            g_replay_duration = 0;
            g_record_segments.clear();
            g_record_current = {};
            g_record_started = {};
            g_record_origin = std::chrono::steady_clock::now();
            g_replay_recording = true;
        }
    }
    if (save) save_replay_recording();
    else {
        std::fprintf(stderr, "replay recording started (F6 to stop)\n");
        log::write("replay", "recording started");
    }
}

bool start_replay_playback() {
    std::filesystem::path path = replay_path();
    std::ifstream file(path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (content.empty()) {
        std::fprintf(stderr, "no replay found: %s\n", path.string().c_str());
        log::write("replay", "no replay found: %s", path.string().c_str());
        return false;
    }
    std::vector<ScriptEntry> segments = parse_script(content.c_str());
    int duration = 0;
    for (const ScriptEntry& entry : segments) duration = std::max(duration, entry.to);
    if (segments.empty() || duration <= 0) {
        std::fprintf(stderr, "replay is empty or invalid: %s\n", path.string().c_str());
        log::write("replay", "invalid replay: %s", path.string().c_str());
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_replay_mutex);
        g_replay_recording = false;
        g_replay_segments = std::move(segments);
        g_replay_duration = duration;
        g_play_origin = std::chrono::steady_clock::now();
        g_replay_playing = true;
    }
    std::fprintf(stderr, "replay playback started: %s\n", path.string().c_str());
    log::write("replay", "playback started: %s", path.string().c_str());
    return true;
}

namespace {

bool sideways_grip(bool read_toggle) {
    static bool flipped = false;
    static bool toggle_held = false;
    if (read_toggle) {
        bool toggle = any_pressed(bindings().of(keymap::Action::Grip));
        if (toggle && !toggle_held) {
            flipped = !flipped;
        }
        toggle_held = toggle;
    }
    return sideways_minigame() != flipped;
}

Sample gamepad_sample(uint32_t channel, bool sideways) {
    Sample result;
    if (sideways) {
        result.buttons |= kMotionSideways;
    }
    gamepad::State pad = gamepad::poll(channel, sideways);
    result.buttons |= pad.buttons;
    result.pointer_valid = pad.pointer_valid;
    result.pointer_x = pad.pointer_x;
    result.pointer_y = pad.pointer_y;
    result.motion_valid = pad.motion_valid;
    for (int i = 0; i < 3; i++) {
        result.accel[i] = pad.accel[i];
    }
    return result;
}

}

uint32_t mouse_motion(MouseMotionState& state, bool active, long x, long y, int64_t now) {
    constexpr double kSlow = 0.15;
    constexpr double kFast = 2.0;
    constexpr int64_t kStep = 8;
    constexpr int64_t kHold = 120;
    if (!active) {
        state = MouseMotionState{};
        return 0;
    }
    if (!state.tracking) {
        state.tracking = true;
        state.x = x;
        state.y = y;
        state.time = now;
        return 0;
    }
    int64_t elapsed = now - state.time;
    if (elapsed >= kStep) {
        double vx = static_cast<double>(x - state.x) / static_cast<double>(elapsed);
        double vy = static_cast<double>(y - state.y) / static_cast<double>(elapsed);
        state.x = x;
        state.y = y;
        state.time = now;
        double ax = vx < 0.0 ? -vx : vx;
        double ay = vy < 0.0 ? -vy : vy;
        double speed = ax > ay ? ax : ay;
        uint32_t bits = 0;
        if (speed >= kFast) {
            bits = ay > ax ? (vy < 0.0 ? kMotionSwingUp : kMotionSwingDown) : kMotionShake;
        } else if (speed >= kSlow) {
            bits = ax >= ay ? (vx < 0.0 ? kMotionTiltLeft : kMotionTiltRight) : (vy < 0.0 ? kMotionTiltUp : kMotionTiltDown);
        }
        if (bits != 0) {
            state.held = bits;
            state.until = now + kHold;
        }
    }
    return now < state.until ? state.held : 0;
}

bool wakes_remote(uint32_t channel) {
    Sample current = sample(channel);
    if ((current.buttons & ~kMotionSideways) != 0) {
        return true;
    }
    static const settings::LiveFlag wake_on_mouse("input.wake_on_mouse", "WP_RECONNECT_ON_POINTER");
    bool pointer_wakes = wake_on_mouse();
    static bool had_pointer = false;
    static float last_x = 0.0f;
    static float last_y = 0.0f;
    bool moved = pointer_wakes && channel == 0 && current.pointer_valid && had_pointer && (current.pointer_x != last_x || current.pointer_y != last_y);
    if (channel == 0) {
        had_pointer = current.pointer_valid;
        last_x = current.pointer_x;
        last_y = current.pointer_y;
    }
    return moved;
}

bool connected(uint32_t channel) {
    return channel == 0 || gamepad::connected(channel);
}

Sample device_sample(uint32_t channel) {
    Sample replay;
    if (playback_sample(channel, replay)) return replay;
    static const char* script_text = std::getenv("WP_INPUT_SCRIPT");
    if (script_text) {
        static const std::vector<ScriptEntry> script = [] {
            std::string text = script_text;
            if (!text.empty() && text[0] == '@') {
                std::ifstream file(text.substr(1), std::ios::binary);
                std::stringstream contents;
                contents << file.rdbuf();
                text = contents.str();
            }
            return parse_script(text.c_str());
        }();
        static const auto origin = std::chrono::steady_clock::now();
        static const bool by_frames = std::getenv("WP_SCRIPT_CLOCK") && std::strcmp(std::getenv("WP_SCRIPT_CLOCK"), "frames") == 0;
        int elapsed = by_frames ? static_cast<int>(gx::frames_drawn() * 1000 / 60)
                                : static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - origin).count());
        if (channel != 0) {
            bool explicit_channel = false;
            for (const ScriptEntry& entry : script) if (entry.channel == channel) { explicit_channel = true; break; }
            if (!explicit_channel) return gamepad_sample(channel, sideways_grip(false));
        }
        return scripted(script, elapsed, channel);
    }
    Sample result;
    if (const char* forced = std::getenv("WP_INPUT_BUTTONS")) {
        static int frames = 0;
        if (frames++ > 150) {
            result.buttons = static_cast<uint32_t>(std::strtoul(forced, nullptr, 16));
        }
        return result;
    }
    void* window = video::window_handle();
    static const bool log_input = std::getenv("WP_LOG_INPUT") != nullptr;
    static bool logged_focus = false;
    bool focused = video::window_focused();
    if (log_input && channel == 0 && focused != logged_focus) {
        logged_focus = focused;
        std::fprintf(stderr, "input window=%p focused=%d", window, focused ? 1 : 0);
        std::fputc(10, stderr);
    }
    if (window != nullptr && !focused) {
        return result;
    }
    if (channel != 0) {
        return gamepad_sample(channel, sideways_grip(false));
    }
    if (window == nullptr) {
        return result;
    }
    static int64_t last_reload = 0;
    int64_t clock = now_ms();
    if (clock - last_reload >= 500) {
        last_reload = clock;
        settings::reload_if_changed();
    }
    const keymap::Bindings& keys = bindings();
    for (size_t i = 0; i < keymap::kActionCount; i++) {
        if (keymap::action(i).buttons != 0 && any_pressed(keys.codes[i])) {
            result.buttons |= keymap::action(i).buttons;
        }
    }
    bool sideways = sideways_grip(true);
    if (sideways) {
        uint32_t keys_held = result.buttons;
        result.buttons &= ~(kButtonUp | kButtonDown | kButtonLeft | kButtonRight);
        result.buttons |= ((keys_held & kButtonLeft) ? kButtonUp : 0) | ((keys_held & kButtonRight) ? kButtonDown : 0) | ((keys_held & kButtonUp) ? kButtonRight : 0) |
                          ((keys_held & kButtonDown) ? kButtonLeft : 0);
        result.buttons |= kMotionSideways;
    }
    gamepad::State pad = gamepad::poll(channel, sideways);
    result.buttons |= pad.buttons;
    if (pad.motion_valid) {
        result.motion_valid = true;
        for (int i = 0; i < 3; i++) {
            result.accel[i] = pad.accel[i];
        }
    }
    int64_t now = now_ms();
    static uint32_t logged_buttons = 0;
    if (log_input && result.buttons != logged_buttons) {
        logged_buttons = result.buttons;
        std::fprintf(stderr, "input buttons=%04x", result.buttons);
        std::fputc(10, stderr);
    }
    static long last_x = 0;
    static long last_y = 0;
    static int64_t last_mouse_move = -1000000;
    long cursor_x = 0;
    long cursor_y = 0;
    if (video::cursor_position(cursor_x, cursor_y)) {
        if (cursor_x != last_x || cursor_y != last_y) {
            last_x = cursor_x;
            last_y = cursor_y;
            last_mouse_move = now;
        }
    }
    static MouseMotionState mouse_state;
    result.buttons |= mouse_motion(mouse_state, any_pressed(keys.of(keymap::Action::MouseMotion)), cursor_x, cursor_y, now);
    bool mouse_recent = now - last_mouse_move < 1000;
    if ((mouse_recent || !pad.pointer_valid) && video::cursor_on_image(result.pointer_x, result.pointer_y)) {
        result.pointer_valid = true;
    } else if (pad.pointer_valid) {
        result.pointer_valid = true;
        result.pointer_x = pad.pointer_x;
        result.pointer_y = pad.pointer_y;
    }
    return result;
}

bool skipping_notice() {
    static const settings::LiveFlag setting("system.skip_notices", "WP_SKIP_NOTICES");
    static const bool scripted_input = std::getenv("WP_INPUT_SCRIPT") || std::getenv("WP_INPUT_BUTTONS");
    const char* module = game::description().notice_module;
    if (scripted_input || !setting() || !module || !module_loaded(module)) {
        return false;
    }
    return now_ms() % kNoticePulsePeriod < kNoticePulseLength;
}

void set_blocked(bool blocked) {
    if (!blocked) {
        for (std::atomic<bool>& wait : g_release_wait) {
            wait = true;
        }
    }
    g_blocked = blocked;
}

Sample sample(uint32_t channel) {
    Sample result = device_sample(channel);
    uint32_t held = result.buttons & ~kMotionSideways;
    bool waiting = channel < 4 && g_release_wait[channel].load();
    if (g_blocked.load() || (waiting && held != 0)) {
        result.buttons &= kMotionSideways;
        return result;
    }
    if (waiting) {
        g_release_wait[channel] = false;
    }
    if (channel == 0 && skipping_notice()) {
        result.buttons |= kButtonA;
    }
    record_sample(channel, result);
    return result;
}

}
