#include "extractor.hpp"
#include <iostream>
#include <filesystem> // For creating directories, requires C++17
#include <vector>
#include <zlib.h>
#include <zstd.h>
#include <map>
#include <stdexcept>
#include <future>

namespace fs = std::filesystem;

// This function remains unchanged as it extracts small components sequentially.
void extract_kdz_components(std::ifstream& file, const KdzHeader& kdz_hdr, const std::string& out_path) {
    fs::path components_path = fs::path(out_path) / "components";
    fs::create_directories(components_path);

    std::cout << "Extracting KDZ components (DLL, DYLIB, etc.)..." << std::endl;
    bool has_components = false;
    for (const auto& record : kdz_hdr.records) {
        if (record.name.rfind(".dz") == std::string::npos && record.size > 0) {
            has_components = true;
            fs::path out_file_path = components_path / record.name;
            std::cout << "  extracting " << record.name << " (" << record.size << " bytes)..." << std::endl;
            
            std::ofstream out_f(out_file_path, std::ios::binary);
            if (!out_f) {
                throw std::runtime_error("Failed to open output file: " + out_file_path.string());
            }
            
            file.seekg(record.offset);
            std::vector<char> buffer(1024 * 1024);
            uint64_t remaining = record.size;
            while (remaining > 0) {
                uint64_t to_read = std::min((uint64_t)buffer.size(), remaining);
                file.read(buffer.data(), to_read);
                out_f.write(buffer.data(), to_read);
                remaining -= to_read;
            }
        }
    }
    if (!has_components) {
        std::cout << "  No other components to extract." << std::endl;
    }
    std::cout << "Done.\n" << std::endl;
}

// This is the worker function that will be executed by threads in the pool.
// It reads compressed chunk, then decompresses it, and directly writes raw data chunks streaming.
void decompress_and_write_chunk(
    const std::string in_path, 
    const std::string compression_type, 
    uint64_t file_offset, 
    uint32_t file_size, 
    uint64_t out_offset,
    std::shared_ptr<std::mutex> out_mutex,
    std::shared_ptr<std::ofstream> out_f) 
{
    std::ifstream file(in_path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Thread worker failed to open input file: " + in_path);
    }
    
    file.seekg(file_offset);

    if (compression_type == "zlib") {
        z_stream strm = {};
        if (inflateInit(&strm) != Z_OK) throw std::runtime_error("inflateInit failed in worker thread");

        // 1MB buffer respectively for in and out
        std::vector<char> in_buffer(1024 * 1024);
        std::vector<char> out_buffer(1024 * 1024);
        uint64_t remaining_in = file_size;
        uint64_t current_out_offset = out_offset;

        while (remaining_in > 0 || strm.avail_in > 0) {
            if (strm.avail_in == 0 && remaining_in > 0) {
                uint32_t to_read = std::min((uint64_t)in_buffer.size(), remaining_in);
                file.read(in_buffer.data(), to_read);
                strm.avail_in = to_read;
                strm.next_in = (Bytef*)in_buffer.data();
                remaining_in -= to_read;
            }

            strm.avail_out = out_buffer.size();
            strm.next_out = (Bytef*)out_buffer.data();
            
            int ret = inflate(&strm, Z_NO_FLUSH);
            if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR) {
                inflateEnd(&strm);
                throw std::runtime_error("zlib stream error in worker thread");
            }

            size_t have = out_buffer.size() - strm.avail_out;
            if (have > 0) {
                // Mutexes are only applied when writing to the disk
				// and memory data is directly dumped to the offset location on the hard drive.
                std::lock_guard<std::mutex> lock(*out_mutex);
                out_f->seekp(current_out_offset);
                out_f->write(out_buffer.data(), have);
                current_out_offset += have;
            }

            if (ret == Z_STREAM_END) break;
        }
        inflateEnd(&strm);

    } else if (compression_type == "zstd") {
        ZSTD_DStream* dstream = ZSTD_createDStream();
        if (dstream == nullptr) throw std::runtime_error("ZSTD_createDStream() failed in worker thread");
        
        std::vector<char> in_buffer(ZSTD_DStreamInSize());
        std::vector<char> out_buffer(ZSTD_DStreamOutSize());
        uint64_t remaining_in = file_size;
        uint64_t current_out_offset = out_offset;

        while (remaining_in > 0) {
            uint32_t to_read = std::min((uint64_t)in_buffer.size(), remaining_in);
            file.read(in_buffer.data(), to_read);
            remaining_in -= to_read;

            ZSTD_inBuffer input = { in_buffer.data(), to_read, 0 };
            while (input.pos < input.size) {
                ZSTD_outBuffer output = { out_buffer.data(), out_buffer.size(), 0 };
                size_t ret = ZSTD_decompressStream(dstream, &output, &input);
                if (ZSTD_isError(ret)) {
                    ZSTD_freeDStream(dstream);
                    throw std::runtime_error("ZSTD decompress error in worker thread: " + std::string(ZSTD_getErrorName(ret)));
                }
                
                if (output.pos > 0) {
                    // Likewise 
                    std::lock_guard<std::mutex> lock(*out_mutex);
                    out_f->seekp(current_out_offset);
                    out_f->write(out_buffer.data(), output.pos);
                    current_out_offset += output.pos;
                }
            }
        }
        ZSTD_freeDStream(dstream);
    }
}

// Writing task will be directly dispatched to the ThreadPool.
void extract_dz_parts(const std::string& in_path, const DzHeader& dz_hdr, const std::string& out_path, ThreadPool& pool) {
    for (const auto& hw_part_pair : dz_hdr.parts) {
        uint32_t hw_part = hw_part_pair.first;
        const auto& parts = hw_part_pair.second;
        std::cout << "Partition " << hw_part << ":" << std::endl;

        for (const auto& pname_pair : parts) {
            const std::string& pname = pname_pair.first;
            const auto& chunks = pname_pair.second;
            
            fs::path out_file_path = fs::path(out_path) / (std::to_string(hw_part) + "." + pname + ".img");
            std::cout << "  extracting part " << pname << "..." << std::endl;
            
            auto out_f = std::make_shared<std::ofstream>(out_file_path, std::ios::binary | std::ios::trunc);
            if (!(*out_f)) {
                throw std::runtime_error("Failed to open output file: " + out_file_path.string());
            }
            auto out_mutex = std::make_shared<std::mutex>();

            std::vector<std::future<void>> results;
            results.reserve(chunks.size());
            
            uint64_t base_sector = chunks.empty() ? 0 : chunks[0].part_start_sector;

            // Push all block decompression tasks for the entire partition to ThreadPool.
            for (const auto& chunk : chunks) {
                // Accurately calculate the absolute byte offset of the block in the target .img file.
                uint64_t out_offset = ((uint64_t)chunk.start_sector - base_sector) * 4096;
                results.emplace_back(
                    pool.enqueue(decompress_and_write_chunk, in_path, dz_hdr.compression, 
                                 chunk.file_offset, chunk.file_size, out_offset, out_mutex, out_f)
                );
            }

            // Simultaneously wait for all tasks to be written and output progress logs.
            for (size_t i = 0; i < chunks.size(); ++i) {
                const auto& chunk = chunks[i];
                std::cout << "    extracting chunk " << chunk.name << " (" << std::max(chunk.data_size, chunk.sector_count * 4096u) << " bytes)..." << std::endl;
                results[i].get(); 
            }

            out_f->close();

            // Sparse padding
            uint64_t final_size = 0;
            if (!chunks.empty()) {
                const auto& last_chunk = chunks.back();
                final_size = ((uint64_t)last_chunk.start_sector + last_chunk.sector_count - base_sector) * 4096;
                fs::resize_file(out_file_path, final_size);
            }

            std::cout << "  done. extracted size = " << final_size << " bytes" << std::endl << std::endl;
        }
    }
}

void extract_additional_data(std::ifstream& file, const KdzHeader& kdz_hdr, const std::string& out_path) {
    if (kdz_hdr.version < 3) return;

    fs::path components_path = fs::path(out_path) / "components";
    fs::create_directories(components_path);

    std::map<std::string, KdzHeader::AdditionalRecord> data_map = {
        {"suffix_map.dat", kdz_hdr.suffix_map},
        {"sku_map.dat", kdz_hdr.sku_map},
        {"extended_sku_map.dat", kdz_hdr.extended_sku_map},
        {"extended_mem_id.dat", kdz_hdr.extended_mem_id}
    };
    
    for (const auto& pair : data_map) {
        if (pair.second.size > 0) {
            std::cout << "Extracting additional data: " << pair.first << " (" << pair.second.size << " bytes)" << std::endl;
            file.seekg(pair.second.offset);
            std::vector<char> data(pair.second.size);
            file.read(data.data(), data.size());

            std::ofstream out_f(components_path / pair.first, std::ios::binary);
            out_f.write(data.data(), data.size());
        }
    }
}