#include "kdz_builder.hpp"
#include "secure_partition_builder.hpp"
#include <cstring>
#include <iostream>
#include <fstream>
#include <stdexcept>

std::vector<char> KdzBuilder::build_v1_header(const std::map<std::string, RecordInfo> &records_info)
{
    std::vector<char> buffer(KDZV1_HDR_SIZE, 0);

    BaseHeader base_hdr{KDZV1_HDR_SIZE, KDZV1_MAGIC};
    std::memcpy(buffer.data(), &base_hdr, sizeof(base_hdr));

    std::string dz_name, dll_name;
    for (const auto &rec : meta["records"])
    {
        std::string name = rec["name"];
        if (name.find(".dz") != std::string::npos)
            dz_name = name;
        if (name.find(".dll") != std::string::npos)
            dll_name = name;
    }

    KDZ_V1RECORD_FMT dz_rec{};
    const auto &dz_info = records_info.at(dz_name);
    auto dz_name_vec = encode_asciiz(dz_name, sizeof(dz_rec.name));
    std::memcpy(dz_rec.name, dz_name_vec.data(), sizeof(dz_rec.name));
    dz_rec.size = dz_info.size;
    dz_rec.offset = dz_info.offset;
    std::memcpy(buffer.data() + sizeof(BaseHeader), &dz_rec, sizeof(dz_rec));

    KDZ_V1RECORD_FMT dll_rec{};
    const auto &dll_info = records_info.at(dll_name);
    auto dll_name_vec = encode_asciiz(dll_name, sizeof(dll_rec.name));
    std::memcpy(dll_rec.name, dll_name_vec.data(), sizeof(dll_rec.name));
    dll_rec.size = dll_info.size;
    dll_rec.offset = dll_info.offset;
    std::memcpy(buffer.data() + sizeof(BaseHeader) + sizeof(KDZ_V1RECORD_FMT), &dll_rec, sizeof(dll_rec));

    return buffer;
}

std::vector<char> KdzBuilder::build_v2_header(const std::map<std::string, RecordInfo> &records_info)
{
    std::vector<char> buffer(KDZV2_HDR_SIZE, 0);
    size_t current_offset = 0;

    BaseHeader base_hdr{KDZV2_HDR_SIZE, KDZV2_MAGIC};
    std::memcpy(buffer.data(), &base_hdr, sizeof(base_hdr));
    current_offset += sizeof(base_hdr);

    std::string dz_name, dll_name, dylib_name;
    for (const auto &rec : meta["records"])
    {
        std::string name = rec["name"];
        if (name.find(".dz") != std::string::npos)
            dz_name = name;
        if (name.find(".dll") != std::string::npos)
            dll_name = name;
        if (name.find(".dylib") != std::string::npos)
            dylib_name = name;
    }

    auto pack_v2_record = [&](const std::string &name)
    {
        KDZ_V2RECORD_FMT rec{};
        if (!name.empty() && records_info.count(name))
        {
            const auto &info = records_info.at(name);
            auto name_vec = encode_asciiz(name, sizeof(rec.name));
            std::memcpy(rec.name, name_vec.data(), sizeof(rec.name));
            rec.size = info.size;
            rec.offset = info.offset;
        }
        return rec;
    };

    auto dz_rec = pack_v2_record(dz_name);
    std::memcpy(buffer.data() + current_offset, &dz_rec, sizeof(dz_rec));
    current_offset += sizeof(dz_rec);

    auto dll_rec = pack_v2_record(dll_name);
    std::memcpy(buffer.data() + current_offset, &dll_rec, sizeof(dll_rec));
    current_offset += sizeof(dll_rec);

    buffer[current_offset++] = 0x03; // Marker byte

    auto dylib_rec = pack_v2_record(dylib_name);
    std::memcpy(buffer.data() + current_offset, &dylib_rec, sizeof(dylib_rec));
    current_offset += sizeof(dylib_rec);

    KDZ_V2RECORD_FMT empty_rec{}; // Empty unknown record
    std::memcpy(buffer.data() + current_offset, &empty_rec, sizeof(empty_rec));

    return buffer;
}

// Safer helper to write POD types to a vector buffer
template <typename T>
void KdzBuilder::write_to_buffer(std::vector<char> &buffer, size_t &offset, const T &value)
{
    if (offset + sizeof(T) > buffer.size())
    {
        throw std::out_of_range("Write operation exceeds buffer bounds.");
    }
    std::memcpy(&buffer[offset], &value, sizeof(T));
    offset += sizeof(T);
}

// Safer helper to write char arrays to a vector buffer
void KdzBuilder::write_to_buffer(std::vector<char> &buffer, size_t &offset, const std::vector<char> &data, size_t max_len)
{
    if (offset + max_len > buffer.size())
    {
        throw std::out_of_range("Write operation exceeds buffer bounds.");
    }
    std::memcpy(&buffer[offset], data.data(), std::min(data.size(), max_len));
    offset += max_len;
}

std::vector<char> KdzBuilder::build_v3_header(const std::map<std::string, RecordInfo> &records_info, const std::map<std::string, RecordInfo> &additional_records)
{
    auto header = build_v2_header(records_info);

    // Overwrite magic and size for V3
    BaseHeader base_hdr{KDZV3_HDR_SIZE, KDZV3_MAGIC};
    std::memcpy(header.data(), &base_hdr, sizeof(base_hdr));

    // V3-specific fields start at offset 1097
    size_t offset = 1097;

    uint32_t ext_mem_id_size = additional_records.count("extended_mem_id") ? additional_records.at("extended_mem_id").size : 0;
    write_to_buffer(header, offset, ext_mem_id_size);

    auto tag_vec = encode_asciiz(meta["tag"], 5);
    write_to_buffer(header, offset, tag_vec, 5);

    const auto suffix_map_info = additional_records.count("suffix_map") ? additional_records.at("suffix_map") : RecordInfo{0, 0};
    const auto sku_map_info = additional_records.count("sku_map") ? additional_records.at("sku_map") : RecordInfo{0, 0};
    const auto ext_sku_map_info = additional_records.count("extended_sku_map") ? additional_records.at("extended_sku_map") : RecordInfo{0, 0};

    uint64_t total_add_rec_size = suffix_map_info.size + sku_map_info.size + ext_sku_map_info.size;
    write_to_buffer(header, offset, total_add_rec_size);

    write_to_buffer(header, offset, suffix_map_info.offset);
    write_to_buffer(header, offset, static_cast<uint32_t>(suffix_map_info.size));

    write_to_buffer(header, offset, sku_map_info.offset);
    write_to_buffer(header, offset, static_cast<uint32_t>(sku_map_info.size));

    auto ftm_model_name_vec = encode_asciiz(meta["ftm_model_name"], 32);
    write_to_buffer(header, offset, ftm_model_name_vec, 32);

    write_to_buffer(header, offset, ext_sku_map_info.offset);
    write_to_buffer(header, offset, static_cast<uint32_t>(ext_sku_map_info.size));

    return header;
}

void KdzBuilder::build(const std::filesystem::path &output_path, const std::filesystem::path &input_dir,
                       const std::vector<char> &dz_data, const std::vector<char> &sec_part_data)
{

    std::cout << "\nAssembling final KDZ file..." << std::endl;

    std::ofstream f(output_path, std::ios::binary | std::ios::trunc);
    if (!f)
        throw std::runtime_error("Failed to create output file: " + output_path.string());

    // 1. Write placeholder for the KDZ header
    std::vector<char> placeholder(meta["size"].get<uint32_t>(), 0);
    f.write(placeholder.data(), placeholder.size());

    // 2. Write Secure Partition if it exists
    if (!sec_part_data.empty())
    {
        f.seekp(SP_OFFSET);
        f.write(sec_part_data.data(), sec_part_data.size());
    }

    // 3. Write all components and record their final offsets and sizes
    std::map<std::string, RecordInfo> final_records_info;
    auto components_path = input_dir / "components";

    // Sort records by original offset to maintain file layout
    json sorted_records = meta["records"];
    std::sort(sorted_records.begin(), sorted_records.end(), [](const json &a, const json &b)
              { return a["offset"].get<uint64_t>() < b["offset"].get<uint64_t>(); });

    for (const auto &record_meta : sorted_records)
    {
        std::string name = record_meta["name"];
        std::cout << "  Writing component: " << name << std::endl;

        // Seek to the original offset to preserve padding/layout
        uint64_t original_offset = record_meta["offset"];
        if (static_cast<uint64_t>(f.tellp()) < original_offset)
        {
            f.seekp(original_offset);
        }

        uint64_t current_offset = f.tellp();
        uint64_t current_size = 0;

        if (name.find(".dz") != std::string::npos)
        {
            f.write(dz_data.data(), dz_data.size());
            current_size = dz_data.size();
        }
        else
        {
            auto component_file = components_path / name;
            if (!std::filesystem::exists(component_file))
            {
                // Allow for empty optional records like dylib
                if (record_meta["size"] != 0)
                {
                    throw std::runtime_error("ERROR: Component file not found: " + component_file.string());
                }
            }
            else
            {
                auto data = read_filepath(component_file);
                f.write(data.data(), data.size());
                current_size = data.size();
            }
        }
        final_records_info[name] = {current_offset, current_size};
    }

    // Handle V3 additional data
    std::map<std::string, RecordInfo> additional_records;
    if (meta["version"] == 3)
    {
        std::cout << "  Writing V3 additional data..." << std::endl;
        // Use a vector of pairs to ensure the correct write order.
        const std::vector<std::pair<std::string, std::string>> additional_files_map = {
            {"suffix_map", "suffix_map.dat"},
            {"sku_map", "sku_map.dat"},
            {"extended_sku_map", "extended_sku_map.dat"},
            {"extended_mem_id", "extended_mem_id.dat"}};

        for (const auto &[key, filename] : additional_files_map)
        {
            auto filepath = components_path / filename;
            if (std::filesystem::exists(filepath))
            {
                // The offset for extended_mem_id is fixed. Others are placed at the current end of the file.
                uint64_t write_offset = (key == "extended_mem_id") ? EXTENDED_MEM_ID_OFFSET : static_cast<uint64_t>(f.tellp());

                auto data = read_filepath(filepath);
                f.seekp(write_offset);
                f.write(data.data(), data.size());

                additional_records[key] = {write_offset, data.size()};
                std::cout << "    - Wrote " << filename << " (" << data.size() << " bytes at offset " << write_offset << ")" << std::endl;
            }
        }
    }

    // 4. Build the final KDZ header
    std::vector<char> final_header;
    int version = meta["version"];
    if (version == 1)
    {
        final_header = build_v1_header(final_records_info);
    }
    else if (version == 2)
    {
        final_header = build_v2_header(final_records_info);
    }
    else if (version == 3)
    {
        final_header = build_v3_header(final_records_info, additional_records);
    }
    else
    {
        throw std::runtime_error("Unsupported KDZ version: " + std::to_string(version));
    }

    // 5. Seek to beginning and write the final header
    f.seekp(0);
    f.write(final_header.data(), final_header.size());

    std::cout << "\nKDZ file '" << output_path.string() << "' created successfully!" << std::endl;
}
