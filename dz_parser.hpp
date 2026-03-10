#ifndef DZ_PARSER_HPP
#define DZ_PARSER_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <chrono>
#include <optional>
#include "kdz_parser.hpp"
#include "shared_structure.hpp"

class DzHeader {
public:
    struct Chunk {
        std::string name;
        uint32_t data_size;
        uint64_t file_offset;
        uint32_t file_size;
        std::vector<uint8_t> hash;
        uint32_t crc;
        uint32_t start_sector;
        uint32_t sector_count;
        uint32_t part_start_sector;
        uint32_t unique_part_id;
        bool is_sparse;
        bool is_ubi_image;
    };
    
    // Header fields
    uint32_t magic;
    uint32_t major, minor;
    std::string model_name;
    std::string sw_version;
    std::optional<std::chrono::system_clock::time_point> build_date;
    uint32_t part_count;
    std::vector<uint8_t> chunk_hdrs_hash;
    uint8_t secure_image_type;
    std::string compression;
    std::vector<uint8_t> data_hash;
    std::string swfv;
    std::string build_type;
    uint32_t header_crc;
    std::string android_ver;
    std::string memory_size;
    std::string signed_security;
    bool is_ufs;
    uint32_t anti_rollback_ver;
    std::string supported_mem;
    std::string target_product;
    uint8_t multi_panel_mask;
    uint8_t product_fuse_id;
    bool is_factory_image;
    std::vector<std::string> operator_code;

    std::vector<std::pair<uint32_t, std::vector<std::pair<std::string, std::vector<Chunk>>>>> parts;

    explicit DzHeader(std::ifstream& file, const KdzHeader::Record& dz_record, bool skip_verification);
    void print_info() const;

private:
    void parse_part_headers(std::ifstream& file, bool verify_data_hash);
};

#endif // DZ_PARSER_HPP