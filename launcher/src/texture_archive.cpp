#include "texture_archive.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>
#include <zlib.h>

namespace {
using Bytes = std::vector<unsigned char>;
uint16_t u16(const Bytes& b, size_t p) {
    return static_cast<uint16_t>(b[p] | (static_cast<uint16_t>(b[p + 1]) << 8));
}
uint32_t u32(const Bytes& b, size_t p) {
    return static_cast<uint32_t>(b[p]) | (static_cast<uint32_t>(b[p + 1]) << 8) |
           (static_cast<uint32_t>(b[p + 2]) << 16) | (static_cast<uint32_t>(b[p + 3]) << 24);
}
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
bool supported(const std::string& path) {
    std::string p = lower(path);
    return p.size() >= 4 && (p.compare(p.size()-4,4,".png")==0 || p.compare(p.size()-4,4,".ini")==0 ||
           p.compare(p.size()-4,4,".txt")==0 || p.compare(p.size()-4,4,".md")==0);
}
bool safe_path(std::string& name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    if (name.empty() || name.front() == '/' || name.find(':') != std::string::npos) return false;
    std::filesystem::path path(name);
    if (path.is_absolute() || path.has_root_name()) return false;
    for (const auto& part : path) if (part == ".." || part == "." || part.empty()) return false;
    return true;
}
bool inflate_raw(const unsigned char* input, size_t input_size, unsigned char* output, size_t output_size) {
    if (input_size > std::numeric_limits<uInt>::max() || output_size > std::numeric_limits<uInt>::max()) return false;
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input));
    stream.avail_in = static_cast<uInt>(input_size);
    stream.next_out = reinterpret_cast<Bytef*>(output);
    stream.avail_out = static_cast<uInt>(output_size);
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return false;
    int result = inflate(&stream, Z_FINISH);
    bool ok = result == Z_STREAM_END && stream.total_out == output_size;
    inflateEnd(&stream);
    return ok;
}
}

bool extract_texture_archive(const std::filesystem::path& archive, const std::filesystem::path& destination,
                             size_t& png_count, std::string& error) {
    namespace fs = std::filesystem;
    png_count = 0;
    error.clear();
    std::ifstream file(archive, std::ios::binary);
    if (!file) { error = "The ZIP file could not be opened."; return false; }
    Bytes bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() < 22 || bytes.size() > 1024ull * 1024ull * 1024ull) {
        error = "The ZIP file is empty or too large.";
        return false;
    }
    const size_t search_start = bytes.size() > 65557 ? bytes.size() - 65557 : 0;
    size_t eocd = std::string::npos;
    for (size_t p = bytes.size() - 22;; --p) {
        if (u32(bytes, p) == 0x06054b50u) { eocd = p; break; }
        if (p == search_start) break;
    }
    if (eocd == std::string::npos || eocd + 22 > bytes.size()) { error = "This file is not a valid ZIP archive."; return false; }
    uint16_t entries = u16(bytes, eocd + 10);
    uint32_t central_size = u32(bytes, eocd + 12);
    uint32_t central_offset = u32(bytes, eocd + 16);
    uint16_t comment = u16(bytes, eocd + 20);
    if (eocd + 22u + comment > bytes.size() || entries == 0 || entries > 50000 ||
        static_cast<uint64_t>(central_offset) + central_size > bytes.size()) {
        error = "The ZIP directory is invalid or contains too many files.";
        return false;
    }
    std::error_code ec;
    fs::create_directories(destination, ec);
    if (ec) { error = "Cannot create temporary import folder: " + ec.message(); return false; }
    size_t cursor = central_offset;
    uint64_t total_written = 0;
    size_t files_seen = 0;
    for (uint16_t i = 0; i < entries; ++i) {
        if (cursor + 46 > bytes.size() || u32(bytes, cursor) != 0x02014b50u) {
            error = "The ZIP central directory is malformed.";
            return false;
        }
        uint16_t flags = u16(bytes, cursor + 8);
        uint16_t method = u16(bytes, cursor + 10);
        uint32_t compressed = u32(bytes, cursor + 20);
        uint32_t expanded = u32(bytes, cursor + 24);
        uint16_t name_len = u16(bytes, cursor + 28);
        uint16_t extra_len = u16(bytes, cursor + 30);
        uint16_t comment_len = u16(bytes, cursor + 32);
        uint32_t local_offset = u32(bytes, cursor + 42);
        uint64_t next = static_cast<uint64_t>(cursor) + 46 + name_len + extra_len + comment_len;
        if (next > bytes.size()) { error = "The ZIP directory entry is truncated."; return false; }
        std::string name(reinterpret_cast<const char*>(bytes.data() + cursor + 46), name_len);
        cursor = static_cast<size_t>(next);
        if (name.empty() || name.back() == '/' || name.back() == '\\') continue;
        if (!safe_path(name)) { error = "The ZIP contains an unsafe path."; return false; }
        if (!supported(name)) continue;
        if ((flags & 1u) != 0 || (method != 0 && method != 8)) {
            error = "Encrypted ZIPs or unsupported compression methods are not supported.";
            return false;
        }
        if (expanded > 128u * 1024u * 1024u || compressed > bytes.size() ||
            total_written + expanded > 512ull * 1024ull * 1024ull) {
            error = "The ZIP expands beyond the allowed texture-pack size.";
            return false;
        }
        if (local_offset + 30ull > bytes.size() || u32(bytes, local_offset) != 0x04034b50u) {
            error = "A ZIP file entry has an invalid local header.";
            return false;
        }
        uint16_t local_name_len = u16(bytes, local_offset + 26);
        uint16_t local_extra_len = u16(bytes, local_offset + 28);
        uint64_t data_offset64 = static_cast<uint64_t>(local_offset) + 30 + local_name_len + local_extra_len;
        if (data_offset64 + compressed > bytes.size()) { error = "A ZIP file entry is truncated."; return false; }
        size_t data_offset = static_cast<size_t>(data_offset64);
        Bytes output(expanded);
        if (method == 0) {
            if (compressed != expanded) { error = "A stored ZIP entry has inconsistent sizes."; return false; }
            std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(data_offset), expanded, output.begin());
        } else if (!inflate_raw(bytes.data() + data_offset, compressed, output.data(), expanded)) {
            error = "A compressed ZIP entry could not be decompressed.";
            return false;
        }
        fs::path relative(name);
        fs::path target = destination / relative;
        fs::create_directories(target.parent_path(), ec);
        if (ec) { error = "Cannot create a texture folder: " + ec.message(); return false; }
        std::ofstream output_file(target, std::ios::binary | std::ios::trunc);
        if (!output_file) { error = "Cannot write extracted file: " + target.filename().string(); return false; }
        if (!output.empty()) output_file.write(reinterpret_cast<const char*>(output.data()), static_cast<std::streamsize>(output.size()));
        if (!output_file) { error = "Writing extracted file failed."; return false; }
        std::string ext = lower(target.extension().string());
        if (ext == ".png") png_count++;
        files_seen++;
        total_written += expanded;
    }
    if (png_count == 0 || files_seen == 0) { error = "The ZIP does not contain PNG textures or supported pack metadata."; return false; }
    return true;
}
