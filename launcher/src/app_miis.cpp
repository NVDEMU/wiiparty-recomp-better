#include "app.h"

#include <algorithm>
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
constexpr const char* kOnlineEditor = "https://mii.nxw.pw/";

fs::path database_file(const Project& project) {
    return project.game_folder() / "nand" / "shared2" / "menu" / "FaceLib" / "RFL_DB.dat";
}
}

void App::save_miis() {
    miis_save_failed_ = !mii_database_.save(database_file(project_));
}

void App::choose_mii_file() {
    static std::string name;
    static std::string everything;
    name = wp::ui::label("mii.import_filter");
    everything = wp::ui::label("mii.import_all");
    static const char* pattern = "mii;miigx;mae;rcd;rsd;bin;dat";
    SDL_DialogFileFilter filters[] = {{name.c_str(), pattern}, {everything.c_str(), "*"}};
    std::string start = utf8_of(project_.game_folder());
    SDL_ShowOpenFileDialog(
        [](void* self, const char* const* files, int) {
            if (files && files[0]) {
                App* app = static_cast<App*>(self);
                std::vector<std::string> picked;
                for (size_t i = 0; files[i] != nullptr; ++i) picked.emplace_back(files[i]);
                std::lock_guard<std::mutex> lock(app->picked_mutex_);
                app->picked_miis_ = std::move(picked);
            }
            wake_main_loop();
        },
        this, window_, filters, 2, start.c_str(), true);
}

void App::choose_mii_export() {
    static std::string name;
    name = wp::ui::label("mii.import_filter");
    SDL_DialogFileFilter filter[] = {{name.c_str(), "mii;bin"}};
    std::string start = utf8_of(project_.game_folder() / "miis-export.mii");
    SDL_ShowSaveFileDialog(
        [](void* self, const char* const* files, int) {
            if (files && files[0]) {
                App* app = static_cast<App*>(self);
                std::lock_guard<std::mutex> lock(app->picked_mutex_);
                app->picked_mii_export_ = files[0];
            }
            wake_main_loop();
        },
        this, window_, filter, 1, start.c_str());
}

void App::miis_page(float width) {
    const ui::Palette& p = ui::palette();
    fs::path file = database_file(project_);
    heading(wp::ui::label("mii.title"), wp::ui::label("mii.intro"), width);
    if (miis_dirty_) {
        miis_dirty_ = false;
        mii_database_.load(file);
        miis_save_failed_ = false;
    }
    {
        Card card(width);
        row(width, wp::ui::label("mii.online"), wp::ui::label("mii.online_detail"), 200.0f, 36.0f, [&] {
            if (ui::button(wp::ui::label("mii.open_online"), Kind::Primary, 200.0f)) SDL_OpenURL(kOnlineEditor);
        });
        inset_separator(width);
        row(width, wp::ui::label("mii.import"), wp::ui::label("mii.import_detail"), 200.0f, 36.0f, [&] {
            if (ui::button(wp::ui::label("mii.choose_file"), Kind::Secondary, 200.0f, mii_database_.valid() && !mii_database_.full())) choose_mii_file();
        });
        inset_separator(width);
        row(width, "Export all Miis", "Save the current Mii collection as a portable record file.", 200.0f, 36.0f, [&] {
            if (ui::button("Export .mii", Kind::Secondary, 200.0f, mii_database_.valid() && mii_database_.count() > 0)) choose_mii_export();
        });
        card.end();
    }
    ui::gap(18);
    if (!mii_database_.valid()) {
        ui::text(wp::ui::label("mii.damaged"), Font::Regular, ui::size::kDetail, p.danger, std::min(width, px(620)));
        ui::gap(14);
        if (ui::button(wp::ui::label("mii.create_database"), Kind::Primary, 240.0f)) {
            std::error_code error;
            if (fs::is_regular_file(file, error)) {
                fs::path backup = file;
                backup += ".damaged.bak";
                fs::copy_file(file, backup, fs::copy_options::none, error);
            }
            mii_database_ = wp::mii::Database();
            save_miis();
        }
        return;
    }

    std::vector<std::string> picked;
    std::string export_path;
    {
        std::lock_guard<std::mutex> lock(picked_mutex_);
        picked.swap(picked_miis_);
        export_path.swap(picked_mii_export_);
    }
    if (!picked.empty()) {
        size_t added = 0, skipped = 0;
        for (const std::string& path : picked) {
            std::ifstream input(path_from(path), std::ios::binary);
            if (!input) { skipped++; continue; }
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            std::vector<wp::mii::Record> records = wp::mii::import_records(data);
            if (records.empty()) { skipped++; continue; }
            for (const wp::mii::Record& record : records) {
                if (mii_database_.full()) { skipped++; continue; }
                bool duplicate = false;
                for (size_t slot : mii_database_.used()) {
                    if (mii_database_.at(slot) == record) { duplicate = true; break; }
                }
                if (duplicate) { skipped++; continue; }
                mii_database_.add(record);
                added++;
            }
        }
        miis_message_failed_ = added == 0;
        miis_message_ = added == 0
            ? "No new Miis imported. Selected files were invalid, duplicated, or the database was full."
            : "Imported " + std::to_string(added) + " Mii(s); skipped " + std::to_string(skipped) + " duplicate, invalid, or excess record(s).";
        if (added > 0) save_miis();
    }
    if (!export_path.empty()) {
        fs::path target = path_from(export_path);
        if (target.extension().empty()) target += ".mii";
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        bool ok = static_cast<bool>(output);
        if (ok) {
            for (size_t slot : mii_database_.used()) {
                const wp::mii::Record& record = mii_database_.at(slot);
                output.write(reinterpret_cast<const char*>(record.data()), static_cast<std::streamsize>(record.size()));
            }
            ok = static_cast<bool>(output);
        }
        miis_message_failed_ = !ok;
        miis_message_ = ok ? "Exported " + std::to_string(mii_database_.count()) + " Mii(s) to " + utf8_of(target.filename())
                           : "Could not export the selected Mii collection.";
    }
    if (!miis_message_.empty()) {
        ui::text(miis_message_.c_str(), Font::Regular, ui::size::kDetail, miis_message_failed_ ? p.danger : p.secondary, std::min(width, px(620)));
        ui::gap(14);
    }
    if (miis_save_failed_) {
        ui::text(wp::ui::label("mii.save_failed"), Font::Regular, ui::size::kDetail, p.danger, width);
        ui::gap(14);
    }
    std::vector<size_t> used = mii_database_.used();
    std::string count = format(wp::ui::label("mii.count"), std::to_string(used.size()));
    ui::text(count.c_str(), Font::Semibold, ui::size::kDetail, p.secondary);
    ui::gap(10);
    if (used.empty()) {
        ui::text(wp::ui::label("mii.none"), Font::Regular, ui::size::kBody, p.secondary, std::min(width, px(620)));
        return;
    }
    Card card(width);
    bool first = true;
    for (size_t slot : used) {
        if (!first) inset_separator(width);
        first = false;
        std::string name = wp::mii::name(mii_database_.at(slot));
        ImGui::PushID(static_cast<int>(slot));
        row(width, name.empty() ? "?" : name.c_str(), "", 120.0f, 36.0f, [&] {
            if (ui::button(wp::ui::label("mii.delete"), Kind::Secondary, 120.0f)) {
                mii_database_.remove(slot);
                save_miis();
            }
        });
        ImGui::PopID();
    }
    card.end();
}
