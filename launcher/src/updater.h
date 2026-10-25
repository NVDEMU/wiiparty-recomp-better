#pragma once

#include <filesystem>
#include <map>
#include <string>

struct Release {
    std::string version;
    std::string build;
    std::string url;
    std::string sha256;
    std::string summary;
    std::string page;
    std::map<std::string, std::string> localized;

    const std::string& summary_for(const char* language) const {
        auto found = localized.find(language);
        return found == localized.end() || found->second.empty() ? summary : found->second;
    }
};

bool newer_version(const std::string& candidate, const std::string& current);
bool update_available(const Release& candidate, const std::string& current_version);
std::filesystem::path update_download_path();
bool latest_release(Release& release, Release& current, const std::string& current_version, std::string& error);
bool download(const std::string& url, const std::filesystem::path& target, std::string& error);
bool replace_launcher(const std::filesystem::path& downloaded, std::string& error);
void remove_old_launcher();
std::filesystem::path launcher_path();
