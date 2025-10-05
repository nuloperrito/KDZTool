#include "dz_builder.hpp"
#include <md5.hpp>
#include <thread_pool.hpp>
#include <zlib.h>
#include <zstd.h>
#include <algorithm>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <ctime>
#include <thread>
#include <future>

std::vector<char> DzBuilder::compress_data(const std::vector<char> &input) const
{
    std::string comp_type = meta["compression"];
    std::vector<char> compressed_data;

    if (comp_type == "zlib")
    {
        z_stream strm = {};
        if (deflateInit(&strm, Z_DEFAULT_COMPRESSION) != Z_OK)
        {
            throw std::runtime_error("zlib deflateInit failed");
        }
        uLong bound = deflateBound(&strm, input.size());
        compressed_data.resize(bound);
        strm.avail_in = input.size();
        strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.data()));
        strm.avail_out = compressed_data.size();
        strm.next_out = reinterpret_cast<Bytef *>(compressed_data.data());

        if (deflate(&strm, Z_FINISH) != Z_STREAM_END)
        {
            deflateEnd(&strm);
            throw std::runtime_error("zlib deflate failed");
        }
        compressed_data.resize(strm.total_out);
        deflateEnd(&strm);
    }
    else if (comp_type == "zstd")
    {
        size_t bound = ZSTD_compressBound(input.size());
        compressed_data.resize(bound);
        size_t compressed_size = ZSTD_compress(compressed_data.data(), bound, input.data(), input.size(), ZSTD_CLEVEL_DEFAULT);
        if (ZSTD_isError(compressed_size))
        {
            throw std::runtime_error("zstd compression failed: " + std::string(ZSTD_getErrorName(compressed_size)));
        }
        compressed_data.resize(compressed_size);
    }
    else
    {
        throw std::runtime_error("Unknown compression type: " + comp_type);
    }
    return compressed_data;
}

std::vector<char> DzBuilder::md5_hash(const void *data, size_t size) const
{
    MD5 hasher;
    hasher.update(static_cast<const unsigned char *>(data), size);
    hasher.finalize();
    auto raw_digest = hasher.get_raw_digest();
    return std::vector<char>(raw_digest.begin(), raw_digest.end());
}

std::vector<char> DzBuilder::build(const std::filesystem::path &input_dir, ThreadPool& pool)
{
    std::cout << "Building DZ file..." << std::endl;

    // Stage 1: Processing and compressing all partition chunks
    std::cout << "  Stage 1: Processing and compressing all partition chunks..." << std::endl;

    // A struct to hold the return type from a chunk processing task
    using ChunkResult = std::pair<std::vector<char>, std::vector<char>>; // {header, data}

    // A struct to hold all information needed to process one chunk
    struct ChunkTaskInfo
    {
        size_t task_index;
        int hw_part;
        std::string pname;
        json chunk_meta;
        std::filesystem::path img_filename;
        size_t chunk_of_total; // For logging (e.g., "chunk 3/10")
    };

    // --- Task Collection Phase (Sequential) ---
    std::vector<ChunkTaskInfo> tasks_to_process;
    size_t total_chunk_count = meta["part_count"].get<size_t>();
    size_t current_chunk_index = 0;

    for (auto const &[hw_part_str, parts] : meta["parts"].items())
    {
        int hw_part = std::stoi(hw_part_str);
        for (auto const &[pname, chunks] : parts.items())
        {
            auto img_filename = input_dir / (std::to_string(hw_part) + "." + pname + ".img");
            if (!std::filesystem::exists(img_filename))
            {
                throw std::runtime_error("ERROR: Image file not found: " + img_filename.string());
            }

            for (const auto &chunk : chunks)
            {
                tasks_to_process.push_back({
                    current_chunk_index++,
                    hw_part,
                    pname,
                    chunk,
                    img_filename,
                    chunks.size()
                });
            }
        }
    }

    std::vector<std::future<ChunkResult>> future_results;
    future_results.reserve(total_chunk_count);

    bool is_v0 = meta["minor"] == 0;

    for (const auto &task_info : tasks_to_process)
    {
        future_results.emplace_back(
            pool.enqueue([this, task_info, is_v0]
            {
                // This lambda is the task executed by a worker thread.

                // Print progress in a thread-safe manner
                {
                    std::lock_guard<std::mutex> lock(cout_mutex);
                    std::cout << "    Processing hw_part " << task_info.hw_part 
                              << ", partition '" << task_info.pname 
                              << "', chunk '" << task_info.chunk_meta["name"].get<std::string>() << "'..." << std::endl;
                }
                
                // Read the specific part of the image file for this chunk.
                // Note: ifstream is not inherently thread-safe, but each thread creates its own instance,
                // and the OS handles concurrent reads from a single file. This is generally safe and performant.
                std::ifstream f_img(task_info.img_filename, std::ios::binary);
                if (!f_img) {
                    throw std::runtime_error("Failed to open image file in thread: " + task_info.img_filename.string());
                }

                uint64_t offset = (task_info.chunk_meta["start_sector"].get<uint64_t>() - task_info.chunk_meta["part_start_sector"].get<uint64_t>()) * 4096;
                uint32_t size = task_info.chunk_meta["data_size"];
                
                f_img.seekg(offset);
                std::vector<char> decompressed_data(size);
                f_img.read(decompressed_data.data(), size);
                f_img.close();

                std::vector<char> compressed_data = this->compress_data(decompressed_data);
                auto data_md5_vec = this->md5_hash(compressed_data.data(), compressed_data.size());

                std::vector<char> chunk_header_data;
                if (is_v0)
                {
                    DzChunkHeaderV0 header{};
                    header.magic = DZ_PART_MAGIC;
                    auto part_name_vec = encode_asciiz(task_info.pname, sizeof(header.part_name));
                    std::memcpy(header.part_name, part_name_vec.data(), sizeof(header.part_name));
                    auto chunk_name_vec = encode_asciiz(task_info.chunk_meta["name"], sizeof(header.chunk_name));
                    std::memcpy(header.chunk_name, chunk_name_vec.data(), sizeof(header.chunk_name));
                    header.decompressed_size = size;
                    header.compressed_size = compressed_data.size();
                    std::memcpy(header.hash, data_md5_vec.data(), sizeof(header.hash));
                    chunk_header_data.assign(reinterpret_cast<char *>(&header), reinterpret_cast<char *>(&header) + sizeof(header));
                }
                else // v1
                {
                    DzChunkHeaderV1 header{};
                    header.magic = DZ_PART_MAGIC;
                    auto part_name_vec = encode_asciiz(task_info.pname, sizeof(header.part_name));
                    std::memcpy(header.part_name, part_name_vec.data(), sizeof(header.part_name));
                    auto chunk_name_vec = encode_asciiz(task_info.chunk_meta["name"], sizeof(header.chunk_name));
                    std::memcpy(header.chunk_name, chunk_name_vec.data(), sizeof(header.chunk_name));
                    header.decompressed_size = task_info.chunk_meta["data_size"];
                    header.compressed_size = compressed_data.size();
                    std::memcpy(header.hash, data_md5_vec.data(), sizeof(header.hash));
                    header.start_sector = task_info.chunk_meta["start_sector"];
                    header.sector_count = task_info.chunk_meta["sector_count"];
                    header.hw_partition = task_info.hw_part;
                    header.crc = crc32(0L, reinterpret_cast<const Bytef *>(compressed_data.data()), compressed_data.size());
                    header.unique_part_id = task_info.chunk_meta["unique_part_id"];
                    header.is_sparse = task_info.chunk_meta["is_sparse"];
                    header.is_ubi_image = task_info.chunk_meta["is_ubi_image"];
                    header.part_start_sector = task_info.chunk_meta["part_start_sector"];
                    std::memset(header.padding, 0, sizeof(header.padding));
                    chunk_header_data.assign(reinterpret_cast<char *>(&header), reinterpret_cast<char *>(&header) + sizeof(header));
                }

                return std::make_pair(std::move(chunk_header_data), std::move(compressed_data));
            })
        );
    }
    
    // --- Result Collection Phase (Sequential to preserve order) ---
    std::vector<std::vector<char>> chunk_headers_list(total_chunk_count);
    std::vector<std::vector<char>> chunk_data_list(total_chunk_count);

    for (size_t i = 0; i < total_chunk_count; ++i)
    {
        // .get() will block until the future is ready.
        // We iterate sequentially from 0 to N-1 to ensure the final lists are in the correct order.
        ChunkResult result = future_results[i].get();
        chunk_headers_list[i] = std::move(result.first);
        chunk_data_list[i] = std::move(result.second);
    }

    // Stage 2: Calculating final hashes for the DZ header
    std::cout << "  Stage 2: Calculating final hashes for the DZ header..." << std::endl;

    // Calculate chunk_hdrs_hash
    std::vector<char> all_chunk_headers;
    for (const auto &hdr : chunk_headers_list)
    {
        all_chunk_headers.insert(all_chunk_headers.end(), hdr.begin(), hdr.end());
    }
    auto chunk_hdrs_hash_vec = md5_hash(all_chunk_headers.data(), all_chunk_headers.size());

    // Prepare fields for header packing
    DzMainHeader proto_header{};
    std::memset(&proto_header, 0, sizeof(proto_header)); // Zero out the structure initially
    proto_header.magic = meta["magic"];
    proto_header.major = meta["major"];
    proto_header.minor = meta["minor"];

    auto model_name_vec = encode_asciiz(meta["model_name"], sizeof(proto_header.model_name));
    std::memcpy(proto_header.model_name, model_name_vec.data(), sizeof(proto_header.model_name));
    auto sw_version_vec = encode_asciiz(meta["sw_version"], sizeof(proto_header.sw_version));
    std::memcpy(proto_header.sw_version, sw_version_vec.data(), sizeof(proto_header.sw_version));

    if (meta.contains("build_date") && !meta["build_date"].is_null())
    {
        std::string dt_str = meta["build_date"];
        std::tm tm = {};
        std::stringstream ss(dt_str);
        // Parse Y-m-dTH:M:S from ISO string
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

        int year = tm.tm_year + 1900;
        int month = tm.tm_mon + 1;
        int day = tm.tm_mday;

        // Sakamoto's algorithm for day of the week calculation (timezone-independent)
        // https://en.wikipedia.org/wiki/Determination_of_the_day_of_the_week#Sakamoto's_methods
        static int const t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
        if (month < 3) year--;
        int weekday_result = (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7;
        // The algorithm returns 0 for Sunday, 1 for Monday, ..., 6 for Saturday.
        // The KDZ format expects 0 for Monday, ..., 6 for Sunday.
        // We need to convert: (Sun:0, Mon:1, ..) -> (Mon:0, Tue:1, ..., Sun:6)
        int final_weekday = (weekday_result == 0) ? 6 : weekday_result - 1;

        proto_header.build_date[0] = tm.tm_year + 1900;
        proto_header.build_date[1] = tm.tm_mon + 1;
        proto_header.build_date[2] = final_weekday; // Use purely calculated value
        proto_header.build_date[3] = tm.tm_mday;
        proto_header.build_date[4] = tm.tm_hour;
        proto_header.build_date[5] = tm.tm_min;
        proto_header.build_date[6] = tm.tm_sec;
        // Milliseconds are not present in the ISO string, remains 0.
    }

    proto_header.part_count = meta["part_count"];
    std::memcpy(proto_header.chunk_hdrs_hash, chunk_hdrs_hash_vec.data(), sizeof(proto_header.chunk_hdrs_hash));
    proto_header.secure_image_type = meta["secure_image_type"];

    std::string comp_meta = meta["compression"];
    std::vector<char> compression_field;
    if (comp_meta == "zlib")
        compression_field = encode_asciiz("\x01", 9);
    else if (comp_meta == "zstd")
        compression_field = encode_asciiz("\x04", 9);
    else
        compression_field = encode_asciiz(comp_meta, 9);
    std::memcpy(proto_header.compression, compression_field.data(), sizeof(proto_header.compression));

    auto swfv_vec = encode_asciiz(meta["swfv"], sizeof(proto_header.swfv));
    std::memcpy(proto_header.swfv, swfv_vec.data(), sizeof(proto_header.swfv));
    auto build_type_vec = encode_asciiz(meta["build_type"], sizeof(proto_header.build_type));
    std::memcpy(proto_header.build_type, build_type_vec.data(), sizeof(proto_header.build_type));
    auto android_ver_vec = encode_asciiz(meta["android_ver"], sizeof(proto_header.android_ver));
    std::memcpy(proto_header.android_ver, android_ver_vec.data(), sizeof(proto_header.android_ver));
    auto memory_size_vec = encode_asciiz(meta["memory_size"], sizeof(proto_header.memory_size));
    std::memcpy(proto_header.memory_size, memory_size_vec.data(), sizeof(proto_header.memory_size));
    auto signed_sec_vec = encode_asciiz(meta["signed_security"], sizeof(proto_header.signed_security));
    std::memcpy(proto_header.signed_security, signed_sec_vec.data(), sizeof(proto_header.signed_security));
    proto_header.is_ufs = meta["is_ufs"];
    proto_header.anti_rollback_ver = meta["anti_rollback_ver"];
    auto supp_mem_vec = encode_asciiz(meta["supported_mem"], sizeof(proto_header.supported_mem));
    std::memcpy(proto_header.supported_mem, supp_mem_vec.data(), sizeof(proto_header.supported_mem));
    auto target_prod_vec = encode_asciiz(meta["target_product"], sizeof(proto_header.target_product));
    std::memcpy(proto_header.target_product, target_prod_vec.data(), sizeof(proto_header.target_product));
    proto_header.multi_panel_mask = meta["multi_panel_mask"];
    proto_header.product_fuse_id = meta["product_fuse_id"];
    proto_header.unknown_1 = meta.value("unknown_1", 0u);
    proto_header.is_factory_image = meta["is_factory_image"] ? 'F' : 0;
    std::string op_code_str;
    for (const auto &code : meta["operator_code"])
    {
        op_code_str += code.get<std::string>() + ".";
    }
    if (!op_code_str.empty())
        op_code_str.pop_back();
    auto op_code_vec = encode_asciiz(op_code_str, sizeof(proto_header.operator_code));
    std::memcpy(proto_header.operator_code, op_code_vec.data(), sizeof(proto_header.operator_code));
    proto_header.unknown_2 = meta.value("unknown_2", 0u);

    // Calculate header_crc (with crc field=0 and data_hash field=empty)
    DzMainHeader header_for_crc = proto_header;
    header_for_crc.header_crc = 0;
    std::memset(header_for_crc.data_hash, 0, sizeof(header_for_crc.data_hash));
    uint32_t header_crc = crc32(0L, reinterpret_cast<const Bytef *>(&header_for_crc), sizeof(header_for_crc));

    // Calculate data_hash (with final crc and data_hash placeholder=0xFF*16)
    DzMainHeader header_for_data_hash = proto_header;
    header_for_data_hash.header_crc = header_crc;
    std::memset(header_for_data_hash.data_hash, 0xFF, sizeof(header_for_data_hash.data_hash));

    MD5 data_hasher;
    data_hasher.update(reinterpret_cast<const unsigned char *>(&header_for_data_hash), sizeof(header_for_data_hash));
    for (size_t i = 0; i < chunk_headers_list.size(); ++i)
    {
        data_hasher.update(reinterpret_cast<const unsigned char *>(chunk_headers_list[i].data()), chunk_headers_list[i].size());
        data_hasher.update(reinterpret_cast<const unsigned char *>(chunk_data_list[i].data()), chunk_data_list[i].size());
    }
    data_hasher.finalize();
    auto data_hash_digest_vec = data_hasher.get_raw_digest();

    // Stage 3: Assembling the final DZ file
    std::cout << "  Stage 3: Assembling the final DZ file..." << std::endl;
    DzMainHeader final_header = proto_header;
    final_header.header_crc = header_crc;
    std::memcpy(final_header.data_hash, data_hash_digest_vec.data(), data_hash_digest_vec.size());

    std::vector<char> dz_buffer;
    dz_buffer.insert(dz_buffer.end(), reinterpret_cast<char *>(&final_header), reinterpret_cast<char *>(&final_header) + sizeof(final_header));
    for (size_t i = 0; i < chunk_headers_list.size(); ++i)
    {
        dz_buffer.insert(dz_buffer.end(), chunk_headers_list[i].begin(), chunk_headers_list[i].end());
        dz_buffer.insert(dz_buffer.end(), chunk_data_list[i].begin(), chunk_data_list[i].end());
    }

    std::cout << "DZ file built successfully (" << dz_buffer.size() << " bytes)." << std::endl;
    return dz_buffer;
}