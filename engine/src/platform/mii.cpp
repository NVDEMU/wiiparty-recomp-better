#include "wp/mii.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <random>
#include <system_error>

namespace wp::mii {

namespace {

constexpr uint8_t kTail[] = {0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x52, 0x4E, 0x48, 0x44, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr size_t kTailOffset = 0x1CEC;
constexpr size_t kNameOffset = 0x02;
constexpr size_t kIdOffset = 0x18;
constexpr size_t kSystemIdOffset = 0x1C;
constexpr uint32_t kConsoleCreated = 0x80000000u;
constexpr uint32_t kIdMask = 0x1FFFFFFFu;
constexpr uint8_t kDefaultSystemId[4] = {0x05, 0x03, 0x04, 0x05};

std::string to_utf8(const std::u16string& text) {
    std::string result;
    for (size_t i = 0; i < text.size(); i++) {
        uint32_t value = text[i];
        if (value >= 0xD800 && value < 0xDC00 && i + 1 < text.size()) {
            value = 0x10000 + ((value - 0xD800) << 10) + (text[++i] - 0xDC00u);
        }
        if (value < 0x80) {
            result.push_back(static_cast<char>(value));
        } else if (value < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (value >> 6)));
            result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
        } else if (value < 0x10000) {
            result.push_back(static_cast<char>(0xE0 | (value >> 12)));
            result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xF0 | (value >> 18)));
            result.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
        }
    }
    return result;
}

std::vector<uint8_t> blank_image() {
    std::vector<uint8_t> image(kFileSize, 0);
    image[0] = 'R';
    image[1] = 'N';
    image[2] = 'O';
    image[3] = 'D';
    std::copy(std::begin(kTail), std::end(kTail), image.begin() + kTailOffset);
    return image;
}

uint32_t identifier_of(const Record& record) {
    return (static_cast<uint32_t>(record[kIdOffset]) << 24) | (static_cast<uint32_t>(record[kIdOffset + 1]) << 16) |
           (static_cast<uint32_t>(record[kIdOffset + 2]) << 8) | record[kIdOffset + 3];
}

}

std::string name(const Record& record) {
    std::u16string text;
    for (size_t i = 0; i < kNameLength; i++) {
        char16_t unit = static_cast<char16_t>((record[kNameOffset + i * 2] << 8) | record[kNameOffset + i * 2 + 1]);
        if (unit == 0) {
            break;
        }
        text.push_back(unit);
    }
    return to_utf8(text);
}

bool occupied(const Record& record) {
    for (size_t i = kIdOffset; i < kIdOffset + 4; i++) {
        if (record[i] != 0) {
            return true;
        }
    }
    for (size_t i = kNameOffset; i < kNameOffset + kNameLength * 2; i++) {
        if (record[i] != 0) {
            return true;
        }
    }
    return false;
}

uint16_t crc16(const uint8_t* data, size_t size) {
    uint16_t crc = 0;
    for (size_t i = 0; i < size; i++) {
        crc ^= static_cast<uint16_t>(data[i] << 8);
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

std::vector<Record> import_records(const std::vector<uint8_t>& data) {
    std::vector<Record> records;
    auto take = [&](size_t offset) {
        Record record;
        std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(offset), kRecordSize, record.begin());
        if (occupied(record)) {
            records.push_back(record);
        }
    };
    if (data.size() >= kDatabaseSize && data[0] == 'R' && data[1] == 'N' && data[2] == 'O' && data[3] == 'D') {
        Database database;
        if (!database.parse(data)) return records;
        for (size_t slot : database.used()) records.push_back(database.at(slot));
        return records;
    }
    constexpr size_t kStoreSize = kRecordSize + 2;
    if (data.size() == kRecordSize || data.size() == kStoreSize) {
        take(0);
    } else if (!data.empty() && data.size() % kStoreSize == 0 && data.size() / kStoreSize <= kSlots) {
        for (size_t offset = 0; offset < data.size(); offset += kStoreSize) {
            take(offset);
        }
    } else if (!data.empty() && data.size() % kRecordSize == 0 && data.size() / kRecordSize <= kSlots) {
        for (size_t offset = 0; offset < data.size(); offset += kRecordSize) {
            take(offset);
        }
    }
    return records;
}

Database::Database() : image_(blank_image()) {}

bool Database::parse(const std::vector<uint8_t>& data) {
    if (data.size() < kDatabaseSize || data[0] != 'R' || data[1] != 'N' || data[2] != 'O' || data[3] != 'D') {
        return false;
    }
    uint16_t stored = static_cast<uint16_t>((data[kChecksumOffset] << 8) | data[kChecksumOffset + 1]);
    if (stored != crc16(data.data(), kChecksumOffset)) {
        return false;
    }
    image_ = data;
    if (image_.size() < kFileSize) {
        image_.resize(kFileSize, 0);
    }
    for (size_t i = 0; i < kSlots; i++) {
        std::copy_n(image_.begin() + static_cast<std::ptrdiff_t>(kHeaderSize + i * kRecordSize), kRecordSize, records_[i].begin());
    }
    return true;
}

bool Database::load(const std::filesystem::path& path) {
    records_ = {};
    image_ = blank_image();
    valid_ = true;
    existed_ = false;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return true;
    }
    existed_ = true;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    valid_ = parse(data);
    if (!valid_) {
        image_ = blank_image();
        records_ = {};
    }
    return valid_;
}

std::vector<uint8_t> Database::bytes() const {
    std::vector<uint8_t> data = image_;
    for (size_t i = 0; i < kSlots; i++) {
        std::copy(records_[i].begin(), records_[i].end(), data.begin() + static_cast<std::ptrdiff_t>(kHeaderSize + i * kRecordSize));
    }
    uint16_t crc = crc16(data.data(), kChecksumOffset);
    data[kChecksumOffset] = static_cast<uint8_t>(crc >> 8);
    data[kChecksumOffset + 1] = static_cast<uint8_t>(crc);
    return data;
}

bool Database::save(const std::filesystem::path& path) {
    std::vector<uint8_t> data = bytes();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!file) {
            return false;
        }
    }
    std::filesystem::path backup = path;
    backup += ".bak";
    bool had_original = std::filesystem::exists(path, error) && !error;
    if (had_original) {
        error.clear();
        std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    error.clear();
    std::filesystem::rename(temporary, path, error);
    if (error && had_original) {
        error.clear();
        std::filesystem::remove(path, error);
        if (!error) {
            error.clear();
            std::filesystem::rename(temporary, path, error);
        }
    }
    if (error) {
        std::error_code restore_error;
        if (had_original && !std::filesystem::exists(path, restore_error)) {
            restore_error.clear();
            std::filesystem::copy_file(backup, path, std::filesystem::copy_options::overwrite_existing, restore_error);
        }
        restore_error.clear();
        std::filesystem::remove(temporary, restore_error);
        return false;
    }
    valid_ = true;
    existed_ = true;
    return true;
}

size_t Database::count() const {
    return used().size();
}

std::vector<size_t> Database::used() const {
    std::vector<size_t> result;
    for (size_t i = 0; i < kSlots; i++) {
        if (occupied(records_[i])) {
            result.push_back(i);
        }
    }
    return result;
}

bool Database::full() const {
    return count() >= kSlots;
}

size_t Database::add(Record record) {
    std::vector<size_t> list = used();
    uint8_t system[4] = {kDefaultSystemId[0], kDefaultSystemId[1], kDefaultSystemId[2], kDefaultSystemId[3]};
    if (!list.empty()) {
        std::copy_n(records_[list.front()].begin() + kSystemIdOffset, 4, system);
    }
    std::copy(system, system + 4, record.begin() + kSystemIdOffset);
    std::random_device device;
    std::mt19937 generator(device());
    uint32_t identifier = 0;
    bool unique = false;
    while (!unique) {
        identifier = kConsoleCreated | (generator() & kIdMask);
        unique = true;
        for (size_t slot : list) {
            if (identifier_of(records_[slot]) == identifier) {
                unique = false;
            }
        }
    }
    for (size_t i = 0; i < 4; i++) {
        record[kIdOffset + i] = static_cast<uint8_t>(identifier >> (24 - 8 * i));
    }
    for (size_t i = 0; i < kSlots; i++) {
        if (!occupied(records_[i])) {
            records_[i] = record;
            return i;
        }
    }
    return kSlots;
}

void Database::remove(size_t slot) {
    if (slot < kSlots) {
        records_[slot] = Record{};
    }
}

}
