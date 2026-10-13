#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

#include "layout.h"
#include "subprocess.h"
#include "texts.h"
#include "wp/mii.h"
#include "wp/ui_text.h"

namespace {

namespace fs = std::filesystem;

struct Group {
    const char* title;
    std::vector<const char*> keys;
};

const std::vector<Group>& groups() {
    static const std::vector<Group> value = {
        {"mii.group.general", {"gender", "birth_month", "birth_day", "favorite_color", "favorite", "height", "weight"}},
        {"mii.group.face", {"face_shape", "skin_color", "facial_feature"}},
        {"mii.group.hair", {"hair_type", "hair_color", "hair_flip"}},
        {"mii.group.eyebrows", {"eyebrow_type", "eyebrow_rotation", "eyebrow_color", "eyebrow_size", "eyebrow_vertical", "eyebrow_spacing"}},
        {"mii.group.eyes", {"eye_type", "eye_rotation", "eye_color", "eye_size", "eye_vertical", "eye_spacing"}},
        {"mii.group.nose", {"nose_type", "nose_size", "nose_vertical"}},
        {"mii.group.mouth", {"mouth_type", "mouth_color", "mouth_size", "mouth_vertical"}},
        {"mii.group.glasses", {"glasses_type", "glasses_color", "glasses_size", "glasses_vertical"}},
        {"mii.group.facial_hair", {"mustache_type", "beard_type", "facial_hair_color", "mustache_size", "mustache_vertical"}},
        {"mii.group.mole", {"mole", "mole_size", "mole_vertical", "mole_horizontal"}},
    };
    return value;
}

bool is_toggle(const std::string& key) {
    return key == "favorite" || key == "hair_flip" || key == "mole";
}

std::vector<std::string> choices(const std::string& key, uint32_t maximum, uint32_t minimum) {
    std::vector<std::string> labels;
    if (key == "gender") {
        return {wp::ui::label("mii.male"), wp::ui::label("mii.female")};
    }
    if (is_toggle(key)) {
        return {wp::ui::label("mii.no"), wp::ui::label("mii.yes")};
    }
    bool optional = key == "birth_month" || key == "birth_day";
    for (uint32_t i = 0; i <= maximum; i++) {
        if (i < minimum) {
            labels.push_back("");
            continue;
        }
        labels.push_back(optional && i == 0 ? wp::ui::label("mii.unset") : std::to_string(optional ? i : i + (key == "height" || key == "weight" ? 0 : 1)));
    }
    return labels;
}

ImU32 rgb(int r, int g, int b) {
    return IM_COL32(r, g, b, 255);
}

ImU32 pick(const std::vector<ImU32>& palette, uint32_t index) {
    return palette[std::min<size_t>(index, palette.size() - 1)];
}

void draw_preview(const wp::mii::Record& record, ImVec2 origin, float size) {
    static const std::vector<ImU32> skin = {rgb(255, 215, 180), rgb(255, 190, 140), rgb(240, 165, 110), rgb(205, 120, 70), rgb(160, 95, 55), rgb(100, 60, 35)};
    static const std::vector<ImU32> hair = {rgb(30, 26, 25),    rgb(75, 45, 30),   rgb(125, 55, 30), rgb(165, 100, 50),
                                            rgb(130, 130, 130), rgb(225, 185, 80), rgb(70, 70, 70),  rgb(205, 205, 205)};
    static const std::vector<ImU32> eyes = {rgb(25, 25, 25), rgb(110, 110, 120), rgb(110, 65, 30), rgb(60, 120, 60), rgb(60, 100, 170), rgb(120, 170, 200)};
    static const std::vector<ImU32> frames = {rgb(25, 25, 25), rgb(110, 65, 30), rgb(200, 50, 50), rgb(60, 100, 170), rgb(230, 190, 40), rgb(130, 130, 130)};
    static const std::vector<ImU32> lips = {rgb(205, 110, 100), rgb(200, 60, 60), rgb(230, 120, 150)};
    static const std::vector<ImU32> shirts = {rgb(220, 50, 50),  rgb(240, 140, 40),  rgb(240, 220, 60),  rgb(150, 210, 70), rgb(40, 140, 60),   rgb(40, 70, 180),
                                              rgb(80, 170, 230), rgb(240, 140, 180), rgb(130, 70, 170),  rgb(120, 75, 40),  rgb(235, 235, 235), rgb(40, 40, 40)};
    static const float widths[] = {50, 54, 48, 52, 56, 46, 53, 49};
    static const float heights[] = {60, 56, 62, 58, 54, 64, 57, 61};
    auto value = [&](const char* key) { return wp::mii::get(record, *wp::mii::find_field(key)); };
    auto number = [&](const char* key) { return static_cast<float>(value(key)); };
    ImDrawList* list = ImGui::GetWindowDrawList();
    float scale = size / 200.0f;
    auto at = [&](float x, float y) { return ImVec2(origin.x + x * scale, origin.y + y * scale); };
    ImU32 skin_color = pick(skin, value("skin_color"));
    ImU32 hair_color = pick(hair, value("hair_color"));
    uint32_t shape = value("face_shape");
    float rx = widths[shape & 7];
    float ry = heights[shape & 7];
    float head_y = 92.0f;
    list->AddRectFilled(at(52, 150), at(148, 205), pick(shirts, value("favorite_color")), 22.0f * scale);
    uint32_t hair_type = value("hair_type");
    float length = static_cast<float>(hair_type % 6) / 5.0f;
    if (length > 0.35f) {
        list->AddRectFilled(at(100 - rx - 7, head_y - ry * 0.5f), at(100 + rx + 7, head_y + ry * (0.35f + length * 0.9f)), hair_color, 16.0f * scale);
    }
    list->AddEllipseFilled(at(100, head_y), ImVec2(rx * scale, ry * scale), skin_color, 0.0f, 48);
    float fringe = 0.30f + static_cast<float>(hair_type % 4) * 0.07f;
    list->AddEllipseFilled(at(100, head_y - ry * 0.42f), ImVec2((rx + 4) * scale, ry * (0.62f + fringe * 0.2f) * scale), hair_color, 0.0f, 48);
    list->AddEllipseFilled(at(100, head_y + ry * 0.12f), ImVec2(rx * scale, ry * 0.88f * scale), skin_color, 0.0f, 48);
    float eye_y = 52.0f + number("eye_vertical") * 3.0f;
    float eye_gap = 11.0f + number("eye_spacing") * 1.5f;
    float eye_w = 4.5f + number("eye_size") * 0.9f;
    float eye_tilt = (number("eye_rotation") - 4.0f) * 1.2f;
    for (int side = -1; side <= 1; side += 2) {
        float direction = static_cast<float>(side);
        float x = 100.0f + direction * eye_gap;
        ImVec2 center = at(x, eye_y + direction * eye_tilt * 0.3f);
        list->AddEllipseFilled(center, ImVec2(eye_w * scale, eye_w * 1.15f * scale), rgb(250, 250, 250), 0.0f, 24);
        list->AddEllipseFilled(center, ImVec2(eye_w * 0.62f * scale, eye_w * 0.9f * scale), pick(eyes, value("eye_color")), 0.0f, 24);
        float brow_x = 100.0f + direction * (10.0f + number("eyebrow_spacing") * 1.5f);
        float brow_y = eye_y - 13.0f + (number("eyebrow_vertical") - 10.0f) * 1.2f;
        float half = 6.0f + number("eyebrow_size") * 1.2f;
        float tilt = (number("eyebrow_rotation") - 6.0f) * 1.4f;
        list->AddLine(at(brow_x - half, brow_y + direction * tilt * 0.5f), at(brow_x + half, brow_y - direction * tilt * 0.5f), pick(hair, value("eyebrow_color")),
                      3.2f * scale);
    }
    uint32_t glasses = value("glasses_type");
    if (glasses > 0) {
        float radius = eye_w + 4.0f + number("glasses_size");
        float glass_y = eye_y + (number("glasses_vertical") - 10.0f) * 0.6f;
        ImU32 color = pick(frames, value("glasses_color"));
        for (int side = -1; side <= 1; side += 2) {
            ImVec2 center = at(100.0f + static_cast<float>(side) * eye_gap, glass_y);
            if (glasses >= 7) {
                list->AddCircleFilled(center, radius * scale, IM_COL32(20, 20, 20, 235), 24);
            } else {
                list->AddCircle(center, radius * scale, color, 24, 2.4f * scale);
            }
        }
        list->AddLine(at(100.0f - eye_gap + radius, glass_y), at(100.0f + eye_gap - radius, glass_y), color, 2.4f * scale);
    }
    float nose_y = 70.0f + number("nose_vertical") * 3.4f;
    float nose_w = 3.5f + number("nose_size") * 0.8f;
    list->AddEllipseFilled(at(100, nose_y), ImVec2(nose_w * scale, nose_w * 0.75f * scale), IM_COL32(160, 100, 70, 150), 0.0f, 20);
    float mouth_y = 90.0f + number("mouth_vertical") * 1.6f;
    float mouth_w = 7.0f + number("mouth_size") * 2.2f;
    ImU32 lip = pick(lips, value("mouth_color"));
    ImU32 facial = pick(hair, value("facial_hair_color"));
    if (value("beard_type") > 0) {
        list->AddEllipseFilled(at(100, mouth_y + 12.0f), ImVec2(rx * 0.62f * scale, 13.0f * scale), facial, 0.0f, 24);
    }
    list->AddEllipseFilled(at(100, mouth_y), ImVec2(mouth_w * scale, 3.8f * scale), lip, 0.0f, 24);
    if (value("mustache_type") > 0) {
        list->AddEllipseFilled(at(100, mouth_y - 7.0f), ImVec2((6.0f + number("mustache_size") * 1.6f) * scale, 3.6f * scale), facial, 0.0f, 24);
    }
    if (value("mole") != 0) {
        list->AddCircleFilled(at(100.0f + (number("mole_horizontal") - 8.0f) * 3.0f, 60.0f + number("mole_vertical") * 2.4f), (1.5f + number("mole_size") * 0.4f) * scale,
                              IM_COL32(60, 40, 30, 220), 12);
    }
}

std::string display_name(const wp::mii::Record& record, size_t slot) {
    std::string name = wp::mii::name(record);
    return (name.empty() ? std::string("?") : name) + "  #" + std::to_string(slot + 1);
}

}

void App::choose_mii_file() {
    static std::string name;
    name = wp::ui::label("mii.import_filter");
    static const char* pattern = "mii;dat";
    SDL_DialogFileFilter filters[] = {{name.c_str(), pattern}};
    std::string start = utf8_of(project_.game_folder());
    SDL_ShowOpenFileDialog(
        [](void* self, const char* const* files, int) {
            if (files && files[0]) {
                App* app = static_cast<App*>(self);
                std::lock_guard<std::mutex> lock(app->picked_mutex_);
                app->picked_mii_ = files[0];
            }
            wake_main_loop();
        },
        this, window_, filters, 1, start.c_str(), false);
}

void App::save_miis() {
    fs::path file = project_.game_folder() / "nand" / "shared2" / "menu" / "FaceLib" / "RFL_DB.dat";
    miis_save_failed_ = !mii_database_.save(file);
}

void App::miis_page(float width) {
    const ui::Palette& p = ui::palette();
    fs::path file = project_.game_folder() / "nand" / "shared2" / "menu" / "FaceLib" / "RFL_DB.dat";
    heading(wp::ui::label("mii.title"), wp::ui::label("mii.intro"), width);
    if (miis_dirty_) {
        miis_dirty_ = false;
        mii_database_.load(file);
        std::vector<size_t> used = mii_database_.used();
        miis_selected_ = used.empty() ? wp::mii::kSlots : used.front();
        miis_name_slot_ = static_cast<size_t>(-1);
        miis_save_failed_ = false;
    }
    if (!mii_database_.valid()) {
        ui::text(wp::ui::label("mii.damaged"), Font::Regular, ui::size::kDetail, p.danger, std::min(width, px(620)));
        ui::gap(14);
        if (ui::button(wp::ui::label("mii.create_database"), Kind::Primary, 240.0f)) {
            std::error_code error;
            fs::copy_file(file, fs::path(file).concat(".bak"), fs::copy_options::overwrite_existing, error);
            mii_database_ = wp::mii::Database();
            save_miis();
            miis_selected_ = wp::mii::kSlots;
        }
        return;
    }
    std::string picked;
    {
        std::lock_guard<std::mutex> lock(picked_mutex_);
        picked.swap(picked_mii_);
    }
    if (!picked.empty()) {
        std::ifstream input(path_from(picked), std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        std::vector<wp::mii::Record> records = wp::mii::import_records(data);
        size_t added = 0;
        for (const wp::mii::Record& record : records) {
            if (mii_database_.full()) {
                break;
            }
            miis_selected_ = mii_database_.add(record);
            added++;
        }
        miis_name_slot_ = static_cast<size_t>(-1);
        miis_message_failed_ = added == 0;
        miis_message_ = added == 0 ? wp::ui::label("mii.import_failed") : format(wp::ui::label("mii.import_done"), std::to_string(added));
        if (added > 0) {
            save_miis();
        }
    }
    std::vector<size_t> used = mii_database_.used();
    if (used.empty() || std::find(used.begin(), used.end(), miis_selected_) == used.end()) {
        miis_selected_ = used.empty() ? wp::mii::kSlots : used.front();
    }
    std::string count = format(wp::ui::label("mii.count"), std::to_string(used.size()));
    ui::text(count.c_str(), Font::Regular, ui::size::kDetail, p.secondary);
    ui::gap(10);
    if (ui::button(wp::ui::label("mii.new"), Kind::Primary, 130.0f, !mii_database_.full())) {
        std::string name = "Mii " + std::to_string(used.size() + 1);
        miis_selected_ = mii_database_.add(wp::mii::default_record(name));
        miis_name_slot_ = static_cast<size_t>(-1);
        save_miis();
    }
    ImGui::SameLine(0.0f, px(10));
    bool selected = miis_selected_ < wp::mii::kSlots;
    if (ui::button(wp::ui::label("mii.duplicate"), Kind::Secondary, 120.0f, selected && !mii_database_.full())) {
        wp::mii::Record copy = mii_database_.at(miis_selected_);
        miis_selected_ = mii_database_.add(copy);
        miis_name_slot_ = static_cast<size_t>(-1);
        save_miis();
    }
    ImGui::SameLine(0.0f, px(10));
    if (ui::button(wp::ui::label("mii.import"), Kind::Secondary, 130.0f, !mii_database_.full())) {
        choose_mii_file();
    }
    ImGui::SameLine(0.0f, px(10));
    if (ui::button(wp::ui::label("mii.delete"), Kind::Secondary, 110.0f, selected)) {
        mii_database_.remove(miis_selected_);
        miis_selected_ = wp::mii::kSlots;
        miis_name_slot_ = static_cast<size_t>(-1);
        save_miis();
    }
    ui::gap(18);
    if (!miis_message_.empty()) {
        ui::text(miis_message_.c_str(), Font::Regular, ui::size::kDetail, miis_message_failed_ ? p.danger : p.secondary, std::min(width, px(620)));
        ui::gap(14);
    }
    if (miis_save_failed_) {
        ui::text(wp::ui::label("mii.save_failed"), Font::Regular, ui::size::kDetail, p.danger, width);
        ui::gap(14);
    }
    if (used.empty() || !(miis_selected_ < wp::mii::kSlots)) {
        ui::text(wp::ui::label("mii.none"), Font::Regular, ui::size::kBody, p.secondary, std::min(width, px(620)));
        return;
    }
    if (mii_database_.full()) {
        ui::text(wp::ui::label("mii.full"), Font::Regular, ui::size::kDetail, p.secondary, width);
        ui::gap(10);
    }
    std::vector<std::string> names;
    int current = 0;
    for (size_t i = 0; i < used.size(); i++) {
        names.push_back(display_name(mii_database_.at(used[i]), used[i]));
        if (used[i] == miis_selected_) {
            current = static_cast<int>(i);
        }
    }
    if (miis_name_slot_ != miis_selected_) {
        miis_name_slot_ = miis_selected_;
        std::string name = wp::mii::name(mii_database_.at(miis_selected_));
        std::snprintf(miis_name_, sizeof miis_name_, "%s", name.c_str());
    }
    {
        Card card(width);
        ImVec2 top = ImGui::GetCursorScreenPos();
        float preview = px(220);
        draw_preview(mii_database_.at(miis_selected_), ImVec2(top.x + std::round((width - preview) * 0.5f), top.y + px(14)), preview);
        const char* note = wp::ui::label("mii.preview_note");
        float note_width = ui::text_size(note, Font::Regular, ui::size::kDetail).x;
        ImGui::SetCursorScreenPos(ImVec2(top.x + std::round((width - note_width) * 0.5f), top.y + preview + px(22)));
        ui::text(note, Font::Regular, ui::size::kDetail, p.tertiary);
        ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + preview + px(54)));
        ImGui::Dummy(ImVec2(width, 0));
        inset_separator(width);
        row(width, "Mii", "", 240.0f, 36.0f, [&] {
            if (ui::dropdown("mii-list", names, current, 240.0f)) {
                miis_selected_ = used[static_cast<size_t>(current)];
            }
        });
        inset_separator(width);
        row(width, wp::ui::label("mii.name"), "", 240.0f, 36.0f, [&] {
            ui::push_font(Font::Regular, ui::size::kBody);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, p.control);
            ImGui::PushStyleColor(ImGuiCol_Text, p.text);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(14), px(9)));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(10));
            ImGui::SetNextItemWidth(px(240));
            if (ImGui::InputText("##mii-name", miis_name_, sizeof miis_name_)) {
                wp::mii::set_name(mii_database_.at(miis_selected_), miis_name_);
                save_miis();
            }
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
            ui::pop_font();
        });
        card.end();
    }
    ui::gap(24);
    for (const Group& group : groups()) {
        ui::text(wp::ui::label(group.title), Font::Semibold, ui::size::kDetail, p.secondary);
        ui::gap(10);
        Card card(width);
        bool first = true;
        for (const char* key : group.keys) {
            const wp::mii::Field* field = wp::mii::find_field(key);
            if (!field) {
                continue;
            }
            if (!first) {
                inset_separator(width);
            }
            first = false;
            uint32_t maximum = std::min<uint32_t>(field->maximum, (1u << field->bits) - 1u);
            std::vector<std::string> labels = choices(key, maximum, field->minimum);
            int index = static_cast<int>(wp::mii::get(mii_database_.at(miis_selected_), *field));
            index = std::min(index, static_cast<int>(labels.size()) - 1);
            std::string label_key = std::string("mii.") + key;
            ImGui::PushID(key);
            row(width, wp::ui::label(label_key.c_str()), "", 200.0f, 36.0f, [&] {
                if (ui::dropdown("value", labels, index, 200.0f)) {
                    wp::mii::set(mii_database_.at(miis_selected_), *field, static_cast<uint32_t>(index));
                    save_miis();
                }
            });
            ImGui::PopID();
        }
        card.end();
        ui::gap(24);
    }
}
