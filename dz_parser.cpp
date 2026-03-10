#include "dz_parser.hpp"
#include "utils.hpp"
#include "md5.hpp"
#include <iostream>
#include <stdexcept>
#include <zlib.h>
#include <vector>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>

#if defined(_WIN32) || defined(_WIN64)
#define timegm _mkgmtime
#endif

DzHeader::DzHeader(std::ifstream& file, const KdzHeader::Record& dz_record, bool skip_verification) {
    file.seekg(dz_record.offset);
    
    // Read the entire header into a raw byte buffer first.
    std::vector<char> hdr_bytes(sizeof(DzMainHeader));
    file.read(hdr_bytes.data(), hdr_bytes.size());
    if (!file) {
        throw std::runtime_error("Failed to read DZ header from file.");
    }

    DzMainHeader hdr;
    std::memcpy(&hdr, hdr_bytes.data(), sizeof(DzMainHeader));

    // Verify header CRC32 if present.
    if (hdr.header_crc != 0) {
        uint32_t original_crc = hdr.header_crc;
        
        // Create a copy of the header struct for CRC calculation.
        DzMainHeader hdr_for_crc;
        std::memcpy(&hdr_for_crc, hdr_bytes.data(), sizeof(DzMainHeader));

        // Zero out the CRC field and the data_hash field with 16 null bytes.
        hdr_for_crc.header_crc = 0;
        std::memset(hdr_for_crc.data_hash, 0, 16);

        // Calculate CRC32 on the modified struct.
        // binascii.crc32 is compatible with zlib's crc32.
        uint32_t calculated_crc = crc32(0L, Z_NULL, 0);
        calculated_crc = crc32(calculated_crc, reinterpret_cast<const Bytef*>(&hdr_for_crc), sizeof(DzMainHeader));
        
        // The check is now fully implemented with a detailed error message.
        if (original_crc != calculated_crc) {
            std::ostringstream oss;
            oss << "Header CRC mismatch: expected 0x" << std::hex << original_crc
                << ", got 0x" << calculated_crc;
            throw std::runtime_error(oss.str());
        }
    }

    // Determine if data hash verification is needed.
    bool verify_data_hash = false;
    if (!skip_verification) {
        for(int i=0; i<16; ++i) {
            if (hdr.data_hash[i] != 0xff) {
                verify_data_hash = true;
                break;
            }
        }
    }
    
    // Assertions for header integrity, now fully implemented.
    if (hdr.magic != DZ_MAGIC) throw std::runtime_error("Invalid DZ header magic");
    if (hdr.major > 2 || hdr.minor > 1) {
        throw std::runtime_error("Unexpected DZ version " + std::to_string(hdr.major) + "." + std::to_string(hdr.minor));
    }
    if (hdr.reserved != 0) throw std::runtime_error("Unexpected value for reserved field");
    if (hdr.part_count == 0) throw std::runtime_error("Expected positive part count, got " + std::to_string(hdr.part_count));
    if (hdr.unknown_0 != 0) throw std::runtime_error("Expected 0 in unknown field, got " + std::to_string(hdr.unknown_0));
    if (hdr.unknown_1 != 0 && hdr.unknown_1 != 0xffffffff) {
        std::ostringstream oss;
        oss << "Unexpected value in unknown field: 0x" << std::hex << hdr.unknown_1;
        throw std::runtime_error(oss.str());
    }
    if (hdr.unknown_2 != 0 && hdr.unknown_2 != 1) {
        throw std::runtime_error("Expected 0 or 1 in unknown field, got " + std::to_string(hdr.unknown_2));
    }
    if (!std::all_of(hdr.padding, hdr.padding + 44, [](char c){ return c == 0; })) {
        throw std::runtime_error("Non zero bytes in header padding");
    }
    
    // Assign attributes from parsed header data. ALL fields are now explicitly assigned.
    this->magic = hdr.magic;
    this->major = hdr.major;
    this->minor = hdr.minor;
    this->model_name = decode_asciiz(hdr.model_name, 32);
    this->sw_version = decode_asciiz(hdr.sw_version, 128);
    this->part_count = hdr.part_count;
    this->chunk_hdrs_hash.assign(hdr.chunk_hdrs_hash, hdr.chunk_hdrs_hash + 16);
    this->secure_image_type = hdr.secure_image_type;
    
    // Compression type parsing
    std::string comp_str = decode_asciiz(hdr.compression, 9);
    if (!comp_str.empty() && std::isalpha(static_cast<unsigned char>(comp_str[0]))) {
        std::transform(comp_str.begin(), comp_str.end(), comp_str.begin(), ::tolower);
        if (comp_str != "zlib" && comp_str != "zstd") {
            throw std::runtime_error("Unknown compression " + comp_str);
        }
        this->compression = comp_str;
    } else {
        if (!std::all_of(hdr.compression + 1, hdr.compression + 9, [](char c){ return c == 0; })) {
            throw std::runtime_error("Non zero bytes after compression type byte");
        }
        if (hdr.compression[0] != 1 && hdr.compression[0] != 4) {
            throw std::runtime_error("Unknown compression type " + std::to_string(hdr.compression[0]));
        }
        this->compression = (hdr.compression[0] == 1) ? "zlib" : "zstd";
    }

    this->data_hash.assign(hdr.data_hash, hdr.data_hash + 16);
    this->swfv = decode_asciiz(hdr.swfv, 50);
    this->build_type = decode_asciiz(hdr.build_type, 16);
    this->header_crc = hdr.header_crc;
    this->android_ver = decode_asciiz(hdr.android_ver, 10);
    this->memory_size = decode_asciiz(hdr.memory_size, 11);
    this->signed_security = decode_asciiz(hdr.signed_security, 4);
    this->is_ufs = (hdr.is_ufs != 0);
    this->anti_rollback_ver = hdr.anti_rollback_ver;
    this->supported_mem = decode_asciiz(hdr.supported_mem, 64);
    this->target_product = decode_asciiz(hdr.target_product, 24);
    this->multi_panel_mask = hdr.multi_panel_mask;
    this->product_fuse_id = hdr.product_fuse_id;
    this->is_factory_image = (hdr.is_factory_image == 'F');
    this->operator_code = split_string(decode_asciiz(hdr.operator_code, 24), '.');

    // Build date parsing
    bool is_build_date_zero = true;
    for (int i = 0; i < 8; ++i) {
        if (hdr.build_date[i] != 0) {
            is_build_date_zero = false;
            break;
        }
    }

    if (!is_build_date_zero) {
        tm t{};
        t.tm_year = hdr.build_date[0] - 1900;
        t.tm_mon = hdr.build_date[1] - 1;
        t.tm_mday = hdr.build_date[3];
        t.tm_hour = hdr.build_date[4];
        t.tm_min = hdr.build_date[5];
        t.tm_sec = hdr.build_date[6];
        t.tm_isdst = 0; // Explicitly disable DST for UTC conversion

        // This interprets the tm struct as UTC, which is what the file contains.
        time_t utc_time = timegm(&t);
        
        if (utc_time == -1) {
            this->build_date = std::nullopt; // Invalid date
        } else {
            this->build_date = std::chrono::system_clock::from_time_t(utc_time);
            // The weekday check: use gmtime for consistent UTC-based check
            tm check_tm = *gmtime(&utc_time);
            // Firmware's tm_wday is 0=Monday..6=Sunday. C/C++'s tm_wday is 0=Sunday..6=Saturday.
            int weekday = (check_tm.tm_wday == 0) ? 6 : check_tm.tm_wday - 1; 
            if (weekday != hdr.build_date[2]) {
                throw std::runtime_error("Invalid build weekday. Expected " + std::to_string(weekday) + ", got " + std::to_string(hdr.build_date[2]));
            }
        }
    } else {
        this->build_date = std::nullopt;
    }

    // Finally, parse all the partition chunk headers.
    parse_part_headers(file, verify_data_hash);
}

void DzHeader::parse_part_headers(std::ifstream& file, bool verify_data_hash) {
    MD5 chunk_hdrs_hash_ctx;
    MD5 data_hash_ctx;

    if (verify_data_hash) {
        // Add header to data hash
        DzMainHeader hdr_for_hash;
        file.seekg(file.tellg() - (std::streamoff)sizeof(DzMainHeader));
        read_filestream(file, hdr_for_hash);
        memset(hdr_for_hash.data_hash, 0xff, 16);
        data_hash_ctx.update(reinterpret_cast<const char*>(&hdr_for_hash), sizeof(DzMainHeader));
    }
    
    uint32_t part_start_sector = 0;
    uint32_t part_sector_count = 0;

    bool is_v0 = (this->minor == 0);

    for (uint32_t i = 0; i < this->part_count; ++i) {
        std::string part_name_str;
        Chunk chunk;
        uint32_t hw_partition;
        std::vector<char> chunk_hdr_data;

        if (is_v0) {
            chunk_hdr_data.resize(sizeof(DzChunkHeaderV0));
            file.read(chunk_hdr_data.data(), chunk_hdr_data.size());
            DzChunkHeaderV0 chunk_hdr;
            std::memcpy(&chunk_hdr, chunk_hdr_data.data(), sizeof(DzChunkHeaderV0));
            
            if (chunk_hdr.magic != DZ_PART_MAGIC) throw std::runtime_error("Invalid part magic");
            part_name_str = decode_asciiz(chunk_hdr.part_name, 32);
            chunk.name = decode_asciiz(chunk_hdr.chunk_name, 64);
            chunk.data_size = chunk_hdr.decompressed_size;
            chunk.file_size = chunk_hdr.compressed_size;
            chunk.hash.assign(chunk_hdr.hash, chunk_hdr.hash + 16);
            chunk.file_offset = file.tellg();
            hw_partition = 0;
            // set defaults for V1 fields
            chunk.crc = 0; chunk.start_sector = 0; chunk.sector_count = 0;
            chunk.part_start_sector = 0; chunk.unique_part_id = 0;
            chunk.is_sparse = false; chunk.is_ubi_image = false;

        } else { // V1
            chunk_hdr_data.resize(sizeof(DzChunkHeaderV1));
            file.read(chunk_hdr_data.data(), chunk_hdr_data.size());
            DzChunkHeaderV1 chunk_hdr;
            std::memcpy(&chunk_hdr, chunk_hdr_data.data(), sizeof(DzChunkHeaderV1));

            if (chunk_hdr.magic != DZ_PART_MAGIC) throw std::runtime_error("Invalid part magic");
            part_name_str = decode_asciiz(chunk_hdr.part_name, 32);
            chunk.name = decode_asciiz(chunk_hdr.chunk_name, 64);
            chunk.data_size = chunk_hdr.decompressed_size;
            chunk.file_size = chunk_hdr.compressed_size;
            chunk.hash.assign(chunk_hdr.hash, chunk_hdr.hash + 16);
            chunk.start_sector = chunk_hdr.start_sector;
            chunk.sector_count = chunk_hdr.sector_count;
            hw_partition = chunk_hdr.hw_partition;
            chunk.crc = chunk_hdr.crc;
            chunk.unique_part_id = chunk_hdr.unique_part_id;
            chunk.is_sparse = (chunk_hdr.is_sparse != 0);
            chunk.is_ubi_image = (chunk_hdr.is_ubi_image != 0);
            chunk.file_offset = file.tellg();

            auto hw_it = std::find_if(parts.begin(), parts.end(), 
                                      [&](const auto& p){ return p.first == hw_partition; });
            
            bool is_new_hw_part = (hw_it == parts.end());
            
            bool is_new_part_name = true;
            if (!is_new_hw_part) {
                auto& part_name_vec = hw_it->second;
                auto name_it = std::find_if(part_name_vec.begin(), part_name_vec.end(),
                                            [&](const auto& p) { return p.first == part_name_str; });
                is_new_part_name = (name_it == part_name_vec.end());
            }

            if (is_new_hw_part) {
                part_start_sector = 0;
                part_sector_count = 0;
                if(chunk_hdr.part_start_sector > part_start_sector && chunk_hdr.part_start_sector <= chunk.start_sector) {
                    part_start_sector = chunk_hdr.part_start_sector;
                }
            } else if (is_new_part_name) {
                if (chunk_hdr.part_start_sector == 0) {
                    part_start_sector = chunk.start_sector;
                } else {
                    part_start_sector += part_sector_count;
                    if(chunk_hdr.part_start_sector > part_start_sector && chunk_hdr.part_start_sector <= chunk.start_sector) {
                        part_start_sector = chunk_hdr.part_start_sector;
                    }
                }
                part_sector_count = 0;
            }
            
            if (chunk_hdr.part_start_sector != 0 && chunk_hdr.part_start_sector != part_start_sector) {
                throw std::runtime_error("Mismatch in part start sector");
            }

            chunk.part_start_sector = part_start_sector;
            part_sector_count = (chunk.start_sector - part_start_sector) + chunk.sector_count;
        }
        
        chunk_hdrs_hash_ctx.update(chunk_hdr_data.data(), chunk_hdr_data.size());
            
        // Logic to populate the vector of pairs, preserving partition order.
        auto hw_it = std::find_if(this->parts.begin(), this->parts.end(),
                                  [&](const auto& p) { return p.first == hw_partition; });

        if (hw_it == this->parts.end()) {
            this->parts.push_back({hw_partition, {{part_name_str, {chunk}}}});
        } else {
            auto& part_name_vec = hw_it->second;
            auto name_it = std::find_if(part_name_vec.begin(), part_name_vec.end(),
                                        [&](const auto& p) { return p.first == part_name_str; });
            if (name_it == part_name_vec.end()) {
                part_name_vec.push_back({part_name_str, {chunk}});
            } else {
                name_it->second.push_back(chunk);
            }
        }
        
        // Update data hash or seek
        if (verify_data_hash) {
            constexpr size_t READ_CHUNK_SIZE = 1048576; // 1MiB
            data_hash_ctx.update(chunk_hdr_data.data(), chunk_hdr_data.size());
            std::vector<char> buffer(READ_CHUNK_SIZE);
            uint64_t remaining = chunk.file_size;
            while (remaining > 0) {
                uint64_t to_read = std::min((uint64_t)READ_CHUNK_SIZE, remaining);
                file.read(buffer.data(), to_read);
                data_hash_ctx.update(buffer.data(), to_read);
                remaining -= to_read;
            }
        } else {
            file.seekg(chunk.file_size, std::ios_base::cur);
        }
    }

    chunk_hdrs_hash_ctx.finalize();
    if (chunk_hdrs_hash_ctx.hexdigest() != bytes_to_hex(this->chunk_hdrs_hash)) {
        throw std::runtime_error("Chunk headers hash mismatch");
    }

    if (verify_data_hash) {
        data_hash_ctx.finalize();
        if (data_hash_ctx.hexdigest() != bytes_to_hex(this->data_hash)) {
            throw std::runtime_error("Data hash mismatch");
        }
    }
}

void DzHeader::print_info() const {
    size_t total_chunks = 0;
    for (const auto& hw_pair : parts) {
        for (const auto& name_pair : hw_pair.second) {
            total_chunks += name_pair.second.size();
        }
    }
    
    std::cout << "DZ header" << std::endl;
    std::cout << "=========" << std::endl;
    std::cout << "magic = " << std::hex << this->magic << std::dec << std::endl;
    std::cout << "version = " << this->major << "." << this->minor << std::endl;
    std::cout << "model name = " << this->model_name << std::endl;
    std::cout << "sw version = " << this->sw_version << std::endl;
    if (this->build_date.has_value()) {
        std::time_t build_time_t = std::chrono::system_clock::to_time_t(this->build_date.value());
        std::cout << "build date = " << std::put_time(std::gmtime(&build_time_t), "%Y-%m-%d %H:%M:%S") << std::endl;
    } else {
        std::cout << "build date = " << "N/A" << std::endl;
    }
    std::cout << "compression = " << this->compression << std::endl;
    std::cout << "secure_image_type = " << (int)this->secure_image_type << std::endl;
    std::cout << "swfv = " << this->swfv << std::endl;
    std::cout << "build_type = " << this->build_type << std::endl;
    std::cout << "android_ver = " << this->android_ver << std::endl;
    std::cout << "memory_size = " << this->memory_size << std::endl;
    std::cout << "signed_security = " << this->signed_security << std::endl;
    std::cout << "anti_rollback_ver = " << std::hex << this->anti_rollback_ver << std::dec << std::endl;
    std::cout << "supported_mem = " << this->supported_mem << std::endl;
    std::cout << "target_product = " << this->target_product << std::endl;
    std::cout << "operator_code = [";
    for(size_t i = 0; i < this->operator_code.size(); ++i) {
        std::cout << "'" << this->operator_code[i] << "'";
        if (i < this->operator_code.size() - 1) {
            std::cout << ", ";
        }
    }
    std::cout << "]" << std::endl;
    std::cout << "multi_panel_mask = " << (int)this->multi_panel_mask << std::endl;
    std::cout << "product_fuse_id = " << (int)this->product_fuse_id << std::endl;
    std::cout << "is_factory_image = " << (this->is_factory_image ? "true" : "false") << std::endl;
    std::cout << "is_ufs = " << (this->is_ufs ? "true" : "false") << std::endl;
    std::cout << "chunk_hdrs_hash = " << bytes_to_hex(this->chunk_hdrs_hash) << std::endl;
    std::cout << "data_hash = " << bytes_to_hex(this->data_hash) << std::endl;
    std::cout << "header_crc = " << std::hex << this->header_crc << std::dec << std::endl;
    std::cout << "parts = " << total_chunks << std::endl << std::endl;
}