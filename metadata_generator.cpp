#include "metadata_generator.hpp"
#include "utils.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <filesystem>

void generate_metadata(
    const std::string& out_path,
    const KdzHeader& kdz_hdr,
    const std::optional<SecurePartition>& sec_part,
    const DzHeader& dz_hdr
) {
    std::cout << "Generating metadata.json..." << std::endl;

    json metadata;

    // KDZ metadata
    json kdz_json;
    kdz_json["version"] = kdz_hdr.version;
    kdz_json["magic"] = kdz_hdr.magic;
    kdz_json["size"] = kdz_hdr.size;
    kdz_json["tag"] = kdz_hdr.tag;
    kdz_json["ftm_model_name"] = kdz_hdr.ftm_model_name;
    json records_json = json::array();
    for(const auto& r : kdz_hdr.records) {
        records_json.push_back({
            {"name", r.name},
            {"size", r.size},
            {"offset", r.offset}
        });
    }
    kdz_json["records"] = records_json;
    metadata["kdz"] = kdz_json;

    // Secure Partition metadata
    if (sec_part.has_value()) {
        json sec_part_json;
        sec_part_json["magic"] = sec_part->magic;
        sec_part_json["flags"] = sec_part->flags;
        sec_part_json["part_count"] = sec_part->part_count;
        sec_part_json["signature"] = bytes_to_hex(sec_part->signature);
        json partitions_json = json::array();
        // The data structure will be changed to vector<pair<...>> to preserve order
        for (const auto& hw_pair : sec_part->parts) {
            for (const auto& name_pair : hw_pair.second) {
                for (const auto& p : name_pair.second) {
                    partitions_json.push_back({
                        {"name", p.name},
                        {"hw_part", p.hw_part},
                        {"logical_part", p.logical_part},
                        {"start_sect", p.start_sect},
                        {"end_sect", p.end_sect},
                        {"data_sect_cnt", p.data_sect_cnt},
                        {"reserved", p.reserved},
                        {"hash", bytes_to_hex(p.hash)}
                    });
                }
            }
        }
        sec_part_json["partitions"] = partitions_json;
        metadata["secure_partition"] = sec_part_json;
    }

    // DZ metadata
    json dz_json;
    dz_json["magic"] = dz_hdr.magic;
    dz_json["major"] = dz_hdr.major;
    dz_json["minor"] = dz_hdr.minor;
    dz_json["model_name"] = dz_hdr.model_name;
    dz_json["sw_version"] = dz_hdr.sw_version;
    dz_json["part_count"] = dz_hdr.part_count;
    dz_json["chunk_hdrs_hash"] = bytes_to_hex(dz_hdr.chunk_hdrs_hash);
    dz_json["data_hash"] = bytes_to_hex(dz_hdr.data_hash);
    dz_json["header_crc"] = dz_hdr.header_crc;
    dz_json["secure_image_type"] = dz_hdr.secure_image_type;
    if (dz_hdr.build_date.has_value()) {
        std::time_t build_time_t = std::chrono::system_clock::to_time_t(dz_hdr.build_date.value());
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&build_time_t), "%Y-%m-%dT%H:%M:%S");
        dz_json["build_date"] = ss.str();
    } else {
        dz_json["build_date"] = nullptr;
    }
    dz_json["compression"] = dz_hdr.compression;
    dz_json["swfv"] = dz_hdr.swfv;
    dz_json["build_type"] = dz_hdr.build_type;
    dz_json["android_ver"] = dz_hdr.android_ver;
    dz_json["memory_size"] = dz_hdr.memory_size;
    dz_json["signed_security"] = dz_hdr.signed_security;
    dz_json["is_ufs"] = dz_hdr.is_ufs;
    dz_json["anti_rollback_ver"] = dz_hdr.anti_rollback_ver;
    dz_json["supported_mem"] = dz_hdr.supported_mem;
    dz_json["target_product"] = dz_hdr.target_product;
    dz_json["multi_panel_mask"] = dz_hdr.multi_panel_mask;
    dz_json["product_fuse_id"] = dz_hdr.product_fuse_id;
    dz_json["is_factory_image"] = dz_hdr.is_factory_image;
    dz_json["operator_code"] = dz_hdr.operator_code;
    
    json dz_parts_json;
    // The data structure will be changed to vector<pair<...>> to preserve order
    for (const auto& hw_pair : dz_hdr.parts) {
        json pname_json;
        for (const auto& name_pair : hw_pair.second) {
            json chunks_json = json::array();
            for (const auto& c : name_pair.second) {
                chunks_json.push_back({
                    {"name", c.name},
                    {"data_size", c.data_size},
                    {"file_offset", c.file_offset},
                    {"file_size", c.file_size},
                    {"hash", bytes_to_hex(c.hash)},
                    {"crc", c.crc},
                    {"start_sector", c.start_sector},
                    {"sector_count", c.sector_count},
                    {"part_start_sector", c.part_start_sector},
                    {"unique_part_id", c.unique_part_id},
                    {"is_sparse", c.is_sparse},
                    {"is_ubi_image", c.is_ubi_image}
                });
            }
            pname_json[name_pair.first] = chunks_json;
        }
        dz_parts_json[std::to_string(hw_pair.first)] = pname_json;
    }
    dz_json["parts"] = dz_parts_json;
    metadata["dz"] = dz_json;

    std::filesystem::path metadata_path = std::filesystem::path(out_path) / "metadata.json";
    std::ofstream out_f(metadata_path);
    out_f << metadata.dump(4);
    std::cout << "Metadata saved to " << metadata_path << std::endl;
}