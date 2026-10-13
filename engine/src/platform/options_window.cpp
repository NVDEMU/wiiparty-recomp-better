#include "wp/options_window.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "wp/game.h"
#include "wp/gamepad.h"
#include "wp/input.h"
#include "wp/log.h"
#include "wp/nand.h"
#include "wp/runtime.h"
#include "wp/settings.h"
#include "wp/ui_text.h"
#include "wp/video.h"

namespace wp::options {

namespace {

constexpr uint32_t kRepeating = gamepad::kMenuUp | gamepad::kMenuDown | gamepad::kMenuLeft | gamepad::kMenuRight;
constexpr int64_t kRepeatDelay = 400;
constexpr int64_t kRepeatInterval = 120;

Menu g_menu;
bool g_shown = false;

bool menu_enabled() {
    static const settings::LiveFlag value("system.options_menu", "WP_OPTIONS_MENU");
    return value();
}

constexpr unsigned kKeyBack = 0x08;
constexpr unsigned kKeyReturn = 0x0D;
constexpr unsigned kKeyEscape = 0x1B;
constexpr unsigned kKeySpace = 0x20;
constexpr unsigned kKeyLeft = 0x25;
constexpr unsigned kKeyUp = 0x26;
constexpr unsigned kKeyRight = 0x27;
constexpr unsigned kKeyDown = 0x28;

int client_height() {
    int width = 0;
    int height = 0;
    return video::client_size(width, height) ? height : 0;
}

bool hint_enabled() {
    static const settings::LiveFlag value("input.controls_hint", "WP_CONTROLS_HINT");
    return value();
}

InstructionWatch g_instruction;
bool g_hint_shown = false;
int g_hint_width = 0;
int g_hint_height = 0;

bool instruction_screen() {
    static std::chrono::steady_clock::time_point next;
    static bool cached = false;
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now >= next) {
        next = now + std::chrono::milliseconds(100);
        const game::Description& game = game::description();
        cached = g_instruction.update(loaded_module_names(), game.instruction_module, game.minigame_prefix);
    }
    return cached;
}

ui::Language hint_language() {
    ui::Language chosen = ui::language_of(settings::text("system.interface_language", "WP_INTERFACE_LANGUAGE"));
    return chosen == ui::Language::English && nand::console_is_spanish() ? ui::Language::Spanish : chosen;
}

void update_hint() {
    int width = 0;
    int height = 0;
    bool wanted = hint_enabled() && !g_menu.open() && video::client_size(width, height) && instruction_screen();
    if (wanted) {
        if (!g_hint_shown || width != g_hint_width || height != g_hint_height) {
            if (!g_hint_shown) {
                log::write("options", "controls window shown");
            }
            g_hint_shown = true;
            g_hint_width = width;
            g_hint_height = height;
            ui::Language language = hint_language();
            show_overlay(render_hint(hint_rows(input::bindings(), language), ui::label("hint.title", language), width, height));
        }
    } else if (g_hint_shown) {
        g_hint_shown = false;
        hide_overlay();
        log::write("options", "controls window hidden");
    }
}

void sync(void*) {
    if (g_menu.open()) {
        g_hint_shown = false;
        show_overlay(render(g_menu, client_height()));
        if (!g_shown) {
            g_shown = true;
            input::set_blocked(true);
        }
    } else if (g_shown) {
        g_shown = false;
        hide_overlay();
        input::set_blocked(false);
    }
}

std::string act(void* window, Action action) {
    std::string changed = g_menu.handle(action);
    sync(window);
    return changed;
}

}

bool menu_open() {
    return g_menu.open();
}

void toggle_menu(void* window) {
    if (!g_menu.open() && !menu_enabled()) {
        return;
    }
    g_menu.set_open(!g_menu.open());
    sync(window);
}

std::string menu_key(void* window, unsigned key, bool repeat) {
    if (!g_menu.open()) {
        return "";
    }
    switch (key) {
    case kKeyUp:
        return act(window, Action::Up);
    case kKeyDown:
        return act(window, Action::Down);
    case kKeyLeft:
        return act(window, Action::Previous);
    case kKeyRight:
        return act(window, Action::Next);
    case kKeyReturn:
    case kKeySpace:
        return repeat ? "" : act(window, Action::Next);
    case kKeyEscape:
    case kKeyBack:
        return repeat ? "" : act(window, Action::Close);
    default:
        return "";
    }
}

std::string menu_wheel(void* window, int delta) {
    if (!g_menu.open() || delta == 0) {
        return "";
    }
    return act(window, delta > 0 ? Action::Up : Action::Down);
}

std::string menu_click(void* handle, int x, int y, bool right) {
    if (!g_menu.open()) {
        return "";
    }
    if (right) {
        return act(handle, Action::Close);
    }
    int width = 0;
    int height = 0;
    if (!video::client_size(width, height)) {
        return "";
    }
    Layout frame = layout(height, settings::keys().size());
    Hover hit = hit_test(frame, place(width, height, frame.width, frame.height), x, y);
    if (hit.part == Part::Close) {
        return act(handle, Action::Close);
    }
    if (hit.row < 0) {
        return "";
    }
    g_menu.select(static_cast<size_t>(hit.row));
    if (hit.part == Part::Label) {
        sync(handle);
        return "";
    }
    return act(handle, hit.part == Part::Previous ? Action::Previous : Action::Next);
}

void menu_hover(void* handle, int x, int y) {
    if (!g_menu.open()) {
        return;
    }
    int width = 0;
    int height = 0;
    Hover hit;
    if (video::client_size(width, height)) {
        Layout frame = layout(height, settings::keys().size());
        hit = hit_test(frame, place(width, height, frame.width, frame.height), x, y);
    }
    if (g_menu.set_hover(hit)) {
        sync(handle);
    }
}

std::string poll_gamepads(void* window) {
    static Repeat repeat(kRepeating, kRepeatDelay, kRepeatInterval);
    update_hint();
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    bool focused = video::window_focused();
    uint32_t fired = repeat.update(focused ? gamepad::menu_buttons() : 0, now);
    if (fired & gamepad::kMenuToggle) {
        toggle_menu(window);
        return "";
    }
    if (!g_menu.open() || fired == 0) {
        return "";
    }
    if (fired & gamepad::kMenuCancel) {
        return act(window, Action::Close);
    }
    std::string changed;
    if (fired & gamepad::kMenuUp) {
        act(window, Action::Up);
    }
    if (fired & gamepad::kMenuDown) {
        act(window, Action::Down);
    }
    if (fired & gamepad::kMenuLeft) {
        changed = act(window, Action::Previous);
    }
    if (fired & (gamepad::kMenuRight | gamepad::kMenuAccept)) {
        changed = act(window, Action::Next);
    }
    return changed;
}

void refresh_menu(void* window) {
    if (g_menu.open()) {
        sync(window);
    }
}

}
