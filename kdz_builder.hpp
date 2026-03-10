#ifndef KDZ_BUILDER_UTILS
#define KDZ_BUILDER_UTILS

#include <vector>
#include <map>
#include <string>
#include <filesystem>
#include <cstdint>
#include "utils.hpp"
#include "shared_structure.hpp"

class KdzBuilder {
private:
    const json& meta;
    
    struct RecordInfo {
        uint64_t offset;
        uint64_t size;
    };

    std::vector<char> build_v1_header(const std::map<std::string, RecordInfo>& records_info);

    std::vector<char> build_v2_header(const std::map<std::string, RecordInfo>& records_info);
    
    // Safer helper to write POD types to a vector buffer
    template<typename T>
    void write_to_buffer(std::vector<char>& buffer, size_t& offset, const T& value);
    
    // Safer helper to write char arrays to a vector buffer
    void write_to_buffer(std::vector<char>& buffer, size_t& offset, const std::vector<char>& data, size_t max_len);

    std::vector<char> build_v3_header(const std::map<std::string, RecordInfo>& records_info, const std::map<std::string, RecordInfo>& additional_records);

public:
#pragma pack(push, 1)
    struct BaseHeader {
        uint32_t size;
        uint32_t magic;
    };
#pragma pack(pop)

    explicit KdzBuilder(const json& metadata) : meta(metadata["kdz"]) {}

    void build(const std::filesystem::path& output_path, const std::filesystem::path& input_dir, 
               const std::vector<char>& dz_data, const std::vector<char>& sec_part_data);
};

#endif
