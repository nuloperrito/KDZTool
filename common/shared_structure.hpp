#ifndef SHARED_STRUCTURE_HPP
#define SHARED_STRUCTURE_HPP

#include <cstdint>

// Disable padding for all structures for binary compatibility
#pragma pack(push, 1)

struct SecurePartitionHeader {
    uint32_t magic;
    uint32_t flags;
    uint32_t part_count;
    uint32_t sig_size;
    uint8_t signature[512];
};

struct SecurePartitionRecord {
    char name[30];
    uint8_t hw_part;
    uint8_t logical_part;
    uint32_t start_sect;
    uint32_t end_sect;
    uint32_t data_sect_cnt;
    uint32_t reserved;
    uint8_t hash[32];
};

struct KDZ_V1RECORD_FMT {
    char name[256];
    uint32_t size;
    uint32_t offset;
};

struct KDZ_V2RECORD_FMT {
    char name[256];
    uint64_t size;
    uint64_t offset;
};

struct DzMainHeader {
    uint32_t magic;
    uint32_t major;
    uint32_t minor;
    uint32_t reserved;
    char model_name[32];
    char sw_version[128];
    uint16_t build_date[8]; // year, month, weekday, day, hour, min, sec, msec
    uint32_t part_count;
    uint8_t chunk_hdrs_hash[16];
    uint8_t secure_image_type;
    char compression[9];
    uint8_t data_hash[16];
    char swfv[50];
    char build_type[16];
    uint32_t unknown_0;
    uint32_t header_crc;
    char android_ver[10];
    char memory_size[11];
    char signed_security[4];
    uint32_t is_ufs;
    uint32_t anti_rollback_ver;
    char supported_mem[64];
    char target_product[24];
    uint8_t multi_panel_mask;
    uint8_t product_fuse_id;
    uint32_t unknown_1;
    uint8_t is_factory_image;
    char operator_code[24];
    uint32_t unknown_2;
    char padding[44];
};

struct DzChunkHeaderV0 {
    uint32_t magic;
    char part_name[32];
    char chunk_name[64];
    uint32_t decompressed_size;
    uint32_t compressed_size;
    uint8_t hash[16];
};

struct DzChunkHeaderV1 {
    uint32_t magic;
    char part_name[32];
    char chunk_name[64];
    uint32_t decompressed_size;
    uint32_t compressed_size;
    uint8_t hash[16];
    uint32_t start_sector;
    uint32_t sector_count;
    uint32_t hw_partition;
    uint32_t crc;
    uint32_t unique_part_id;
    uint32_t is_sparse;
    uint32_t is_ubi_image;
    uint32_t part_start_sector;
    char padding[356];
};

// Restore default packing alignment
#pragma pack(pop)

constexpr uint32_t SP_OFFSET = 1320;
constexpr size_t SP_SIZE = 82448;
constexpr uint32_t SP_MAGIC = 0x53430799;
constexpr uint32_t KDZV1_HDR_SIZE = 1304;
constexpr uint32_t KDZV1_MAGIC = 0x50447932;
constexpr uint32_t KDZV2_HDR_SIZE = 1320;
constexpr uint32_t KDZV2_MAGIC = 0x80253134;
constexpr uint32_t KDZV3_HDR_SIZE = 1320;
constexpr uint32_t KDZV3_MAGIC = 0x25223824;
constexpr uint64_t EXTENDED_MEM_ID_OFFSET = 0x14738;
constexpr uint32_t DZ_MAGIC = 0x74189632;
constexpr uint32_t DZ_PART_MAGIC = 0x78951230;
#endif