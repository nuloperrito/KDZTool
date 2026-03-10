#ifndef SECURE_PARTITION_PARSER_HPP
#define SECURE_PARTITION_PARSER_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <optional>
#include <utility>
#include "shared_structure.hpp"

class SecurePartition {
public:
    struct Part {
        std::string name;
        uint8_t hw_part;
        uint8_t logical_part;
        uint32_t start_sect;
        uint32_t end_sect;
        uint32_t data_sect_cnt;
        uint32_t reserved;
        std::vector<uint8_t> hash;
    };

    uint32_t magic;
    uint32_t flags;
    uint32_t part_count;
    std::vector<uint8_t> signature;
    // The structure is: vector<pair<hw_id, vector<pair<partition_name, vector<Part_info>>>>>
    std::vector<std::pair<uint8_t, std::vector<std::pair<std::string, std::vector<Part>>>>> parts;

    static std::optional<SecurePartition> parse(std::ifstream& file);
    void print_info() const;

private:
    SecurePartition() = default;
};

#endif // SECURE_PARTITION_PARSER_HPP