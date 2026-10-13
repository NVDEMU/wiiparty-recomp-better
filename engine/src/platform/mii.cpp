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
constexpr size_t kCreatorOffset = 0x36;
constexpr size_t kIdOffset = 0x18;
constexpr size_t kSystemIdOffset = 0x1C;
constexpr uint32_t kConsoleCreated = 0x80000000u;
constexpr uint32_t kIdMask = 0x1FFFFFFFu;
constexpr uint8_t kDefaultSystemId[4] = {0x05, 0x03, 0x04, 0x05};

const std::vector<Field> kFields = {
    {"gender", 0x00, 2, 14, 1, 1},         {"birth_month", 0x00, 2, 10, 4, 12},      {"birth_day", 0x00, 2, 5, 5, 31},
    {"favorite_color", 0x00, 2, 1, 4, 11}, {"favorite", 0x00, 2, 0, 1, 1},           {"height", 0x16, 1, 0, 7, 127},
    {"weight", 0x17, 1, 0, 7, 127},        {"face_shape", 0x20, 2, 13, 3, 7},        {"skin_color", 0x20, 2, 10, 3, 5},
    {"facial_feature", 0x20, 2, 6, 4, 11}, {"hair_type", 0x22, 2, 9, 7, 71},         {"hair_color", 0x22, 2, 6, 3, 7},
    {"hair_flip", 0x22, 2, 5, 1, 1},       {"eyebrow_type", 0x24, 4, 27, 5, 23},     {"eyebrow_rotation", 0x24, 4, 22, 4, 11},
    {"eyebrow_color", 0x24, 4, 13, 3, 7},  {"eyebrow_size", 0x24, 4, 9, 4, 8},       {"eyebrow_vertical", 0x24, 4, 4, 5, 18, 3},
    {"eyebrow_spacing", 0x24, 4, 0, 4, 12}, {"eye_type", 0x28, 4, 26, 6, 47},         {"eye_rotation", 0x28, 4, 21, 3, 7},
    {"eye_vertical", 0x28, 4, 16, 5, 18},  {"eye_color", 0x28, 4, 13, 3, 5},         {"eye_size", 0x28, 4, 9, 3, 7},
    {"eye_spacing", 0x28, 4, 5, 4, 12},    {"nose_type", 0x2C, 2, 12, 4, 11},        {"nose_size", 0x2C, 2, 8, 4, 8},
    {"nose_vertical", 0x2C, 2, 3, 5, 18},  {"mouth_type", 0x2E, 2, 11, 5, 23},       {"mouth_color", 0x2E, 2, 9, 2, 2},
    {"mouth_size", 0x2E, 2, 5, 4, 8},      {"mouth_vertical", 0x2E, 2, 0, 5, 18},    {"glasses_type", 0x30, 2, 12, 4, 8},
    {"glasses_color", 0x30, 2, 9, 3, 5},  {"glasses_size", 0x30, 2, 5, 3, 7},       {"glasses_vertical", 0x30, 2, 0, 5, 20},
    {"mustache_type", 0x32, 2, 14, 2, 3},  {"beard_type", 0x32, 2, 12, 2, 3},        {"facial_hair_color", 0x32, 2, 9, 3, 7},
    {"mustache_size", 0x32, 2, 5, 4, 8},   {"mustache_vertical", 0x32, 2, 0, 5, 16}, {"mole", 0x34, 2, 15, 1, 1},
    {"mole_size", 0x34, 2, 11, 4, 8},      {"mole_vertical", 0x34, 2, 6, 5, 30},     {"mole_horizontal", 0x34, 2, 1, 5, 16},
};

std::u16string to_utf16(const std::string& text) {
    std::u16string result;
    for (size_t i = 0; i < text.size();) {
        unsigned char lead = static_cast<unsigned char>(text[i]);
        int length = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : (lead >> 3) == 30 ? 4 : 1;
        uint32_t value = length == 1 ? lead : lead & (0x7Fu >> length);
        for (int k = 1; k < length && i + static_cast<size_t>(k) < text.size(); k++) {
            value = (value << 6) | (static_cast<unsigned char>(text[i + static_cast<size_t>(k)]) & 0x3Fu);
        }
        i += static_cast<size_t>(length);
        if (value >= 0x10000) {
            value -= 0x10000;
            result.push_back(static_cast<char16_t>(0xD800 + (value >> 10)));
            result.push_back(static_cast<char16_t>(0xDC00 + (value & 0x3FF)));
        } else {
            result.push_back(static_cast<char16_t>(value));
        }
    }
    return result;
}

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

std::string read_text(const Record& record, size_t offset) {
    std::u16string text;
    for (size_t i = 0; i < kNameLength; i++) {
        char16_t unit = static_cast<char16_t>((record[offset + i * 2] << 8) | record[offset + i * 2 + 1]);
        if (unit == 0) {
            break;
        }
        text.push_back(unit);
    }
    return to_utf8(text);
}

void write_text(Record& record, size_t offset, const std::string& utf8) {
    std::u16string text = to_utf16(utf8);
    if (text.size() > kNameLength) {
        text.resize(kNameLength);
    }
    for (size_t i = 0; i < kNameLength; i++) {
        char16_t unit = i < text.size() ? text[i] : 0;
        record[offset + i * 2] = static_cast<uint8_t>(unit >> 8);
        record[offset + i * 2 + 1] = static_cast<uint8_t>(unit);
    }
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

const std::vector<Field>& fields() {
    return kFields;
}

const Field* find_field(const std::string& key) {
    for (const Field& field : kFields) {
        if (key == field.key) {
            return &field;
        }
    }
    return nullptr;
}

uint32_t get(const Record& record, const Field& field) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < field.bytes; i++) {
        value = (value << 8) | record[field.offset + i];
    }
    return (value >> field.shift) & ((1u << field.bits) - 1u);
}

void set(Record& record, const Field& field, uint32_t input) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < field.bytes; i++) {
        value = (value << 8) | record[field.offset + i];
    }
    uint32_t mask = ((1u << field.bits) - 1u) << field.shift;
    uint32_t limit = std::min<uint32_t>(field.maximum, (1u << field.bits) - 1u);
    input = std::max<uint32_t>(std::min(input, limit), field.minimum);
    value = (value & ~mask) | ((input << field.shift) & mask);
    for (uint8_t i = 0; i < field.bytes; i++) {
        record[field.offset + i] = static_cast<uint8_t>(value >> (8 * (field.bytes - 1 - i)));
    }
}

std::string name(const Record& record) {
    return read_text(record, kNameOffset);
}

void set_name(Record& record, const std::string& utf8) {
    write_text(record, kNameOffset, utf8);
}

std::string creator(const Record& record) {
    return read_text(record, kCreatorOffset);
}

void set_creator(Record& record, const std::string& utf8) {
    write_text(record, kCreatorOffset, utf8);
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

Record blank_record() {
    return Record{};
}

Record default_record(const std::string& utf8_name) {
    Record record = blank_record();
    set_name(record, utf8_name);
    const struct {
        const char* key;
        uint32_t value;
    } defaults[] = {{"height", 63},           {"weight", 63},          {"hair_type", 33},          {"hair_color", 1},      {"eyebrow_type", 6},
                    {"eyebrow_rotation", 6},  {"eyebrow_color", 1},    {"eyebrow_size", 4},     {"eyebrow_vertical", 10},   {"eyebrow_spacing", 2},  {"eye_type", 2},
                    {"eye_rotation", 4},      {"eye_vertical", 12},    {"eye_size", 4},            {"eye_spacing", 2},      {"nose_type", 1},
                    {"nose_size", 4},         {"nose_vertical", 9},    {"mouth_type", 23},         {"mouth_size", 4},       {"mouth_vertical", 13},
                    {"glasses_size", 4},      {"glasses_vertical", 10}, {"mustache_size", 4},      {"mustache_vertical", 10}, {"mole_size", 4},
                    {"mole_vertical", 20},    {"mole_horizontal", 2}};
    for (const auto& entry : defaults) {
        set(record, *find_field(entry.key), entry.value);
    }
    return record;
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
        for (size_t i = 0; i < kSlots; i++) {
            take(kHeaderSize + i * kRecordSize);
        }
        return records;
    }
    constexpr size_t kStoreSize = kRecordSize + 2;
    if (data.size() == kRecordSize) {
        take(0);
    } else if (data.size() == kStoreSize) {
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
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
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
        records_[slot] = blank_record();
    }
}

}
