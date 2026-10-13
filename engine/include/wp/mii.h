#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace wp::mii {

constexpr size_t kSlots = 100;
constexpr size_t kRecordSize = 0x4A;
constexpr size_t kHeaderSize = 4;
constexpr size_t kChecksumOffset = 0x1F1DE;
constexpr size_t kDatabaseSize = 0x1F1E0;
constexpr size_t kFileSize = 0xBE6C0;
constexpr size_t kNameLength = 10;

using Record = std::array<uint8_t, kRecordSize>;

struct Field {
    const char* key;
    uint8_t offset;
    uint8_t bytes;
    uint8_t shift;
    uint8_t bits;
    uint8_t maximum;
};

const std::vector<Field>& fields();
const Field* find_field(const std::string& key);
uint32_t get(const Record& record, const Field& field);
void set(Record& record, const Field& field, uint32_t value);

std::string name(const Record& record);
void set_name(Record& record, const std::string& utf8);
std::string creator(const Record& record);
void set_creator(Record& record, const std::string& utf8);
bool occupied(const Record& record);

Record blank_record();
Record default_record(const std::string& utf8_name);

uint16_t crc16(const uint8_t* data, size_t size);

std::vector<Record> import_records(const std::vector<uint8_t>& data);

class Database {
public:
    Database();

    bool load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path);
    bool valid() const { return valid_; }
    bool existed() const { return existed_; }

    size_t count() const;
    std::vector<size_t> used() const;
    const Record& at(size_t slot) const { return records_[slot]; }
    Record& at(size_t slot) { return records_[slot]; }
    bool full() const;
    size_t add(Record record);
    void remove(size_t slot);
    std::vector<uint8_t> bytes() const;
    bool parse(const std::vector<uint8_t>& data);

private:
    std::array<Record, kSlots> records_{};
    std::vector<uint8_t> image_;
    bool valid_ = true;
    bool existed_ = false;
};

}
