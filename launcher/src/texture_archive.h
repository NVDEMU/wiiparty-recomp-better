#pragma once
#include <cstddef>
#include <filesystem>
#include <string>

bool extract_texture_archive(const std::filesystem::path& archive, const std::filesystem::path& destination,
                            size_t& png_count, std::string& error);
