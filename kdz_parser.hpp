#ifndef KDZ_PARSER_HPP
#define KDZ_PARSER_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include "shared_structure.hpp"

class KdzHeader {
public:
    struct Record {
        std::string name;
        uint64_t size;
        uint64_t offset;
    };

    struct AdditionalRecord {
        uint64_t offset;
        uint32_t size;
    };

    uint32_t version;
    uint32_t magic;
    uint32_t size;
    std::vector<Record> records;

    // V3 specific fields
    std::string tag;
    std::string ftm_model_name;
    uint64_t additional_records_size;
    AdditionalRecord extended_mem_id;
    AdditionalRecord suffix_map;
    AdditionalRecord sku_map;
    AdditionalRecord extended_sku_map;

    explicit KdzHeader(std::ifstream& file);
    void print_info(std::ifstream& file) const;

private:
    void parse_v1_header(const std::vector<char>& data);
    void parse_v2_header(const std::vector<char>& data);
    void parse_v3_header(const std::vector<char>& data);
};

#endif // KDZ_PARSER_HPP