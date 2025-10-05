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
// It reads a single compressed chunk, decompresses it, and returns the raw data.
std::vector<char> decompress_chunk(
    const std::string in_path, 
    const std::string compression_type, 
    uint64_t file_offset, 
    uint32_t file_size, 
    uint32_t data_size) 
{
    std::ifstream file(in_path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Thread worker failed to open input file: " + in_path);
    }
    
    file.seekg(file_offset);

    std::vector<char> compressed_data(file_size);
    file.read(compressed_data.data(), file_size);
    
    std::vector<char> decompressed_data;
    decompressed_data.reserve(data_size);

    if (compression_type == "zlib") {
        z_stream strm = {};
        if (inflateInit(&strm) != Z_OK) throw std::runtime_error("inflateInit failed in worker thread");

        strm.avail_in = file_size;
        strm.next_in = (Bytef*)compressed_data.data();

        std::vector<char> out_buffer(1024 * 1024); // 1MB chunk
        do {
            strm.avail_out = out_buffer.size();
            strm.next_out = (Bytef*)out_buffer.data();
            int ret = inflate(&strm, Z_NO_FLUSH);
            if (ret == Z_STREAM_ERROR) throw std::runtime_error("zlib stream error in worker thread");
            size_t have = out_buffer.size() - strm.avail_out;
            decompressed_data.insert(decompressed_data.end(), out_buffer.begin(), out_buffer.begin() + have);
        } while (strm.avail_out == 0);
        
        inflateEnd(&strm);

    } else if (compression_type == "zstd") {
        ZSTD_DStream* dstream = ZSTD_createDStream();
        if (dstream == nullptr) throw std::runtime_error("ZSTD_createDStream() failed in worker thread");
        
        ZSTD_inBuffer input = { compressed_data.data(), file_size, 0 };
        std::vector<char> out_buffer(ZSTD_DStreamOutSize());

        while (input.pos < input.size) {
            ZSTD_outBuffer output = { out_buffer.data(), out_buffer.size(), 0 };
            size_t ret = ZSTD_decompressStream(dstream, &output, &input);
            if (ZSTD_isError(ret)) {
                ZSTD_freeDStream(dstream);
                throw std::runtime_error("ZSTD decompress error in worker thread: " + std::string(ZSTD_getErrorName(ret)));
            }
            decompressed_data.insert(decompressed_data.end(), out_buffer.begin(), out_buffer.begin() + output.pos);
        }
        ZSTD_freeDStream(dstream);
    }
    
    return decompressed_data;
}

void extract_dz_parts(const std::string& in_path, const DzHeader& dz_hdr, const std::string& out_path, ThreadPool& pool) {
    std::vector<char> write_fill(4096 * 100, '\0');

    for (const auto& hw_part_pair : dz_hdr.parts) {
        uint32_t hw_part = hw_part_pair.first;
        const auto& parts = hw_part_pair.second;
        std::cout << "Partition " << hw_part << ":" << std::endl;

        for (const auto& pname_pair : parts) {
            const std::string& pname = pname_pair.first;
            const auto& chunks = pname_pair.second;
            
            fs::path out_file_path = fs::path(out_path) / (std::to_string(hw_part) + "." + pname + ".img");
            std::cout << "  extracting part " << pname << "..." << std::endl;
            std::ofstream out_f(out_file_path, std::ios::binary);
            if (!out_f) {
                throw std::runtime_error("Failed to open output file: " + out_file_path.string());
            }

            // Enqueue all decompression tasks for the current partition.
            std::vector<std::future<std::vector<char>>> results;
            results.reserve(chunks.size());
            for (const auto& chunk : chunks) {
                results.emplace_back(
                    pool.enqueue(decompress_chunk, in_path, dz_hdr.compression, chunk.file_offset, chunk.file_size, chunk.data_size)
                );
            }

            // Process results in order to ensure correct output and logging.
            uint64_t current_offset = (uint64_t)chunks[0].part_start_sector * 4096;
            for (size_t i = 0; i < chunks.size(); ++i) {
                const auto& chunk = chunks[i];

                std::cout << "    extracting chunk " << chunk.name << " (" << std::max(chunk.data_size, chunk.sector_count * 4096u) << " bytes)..." << std::endl;

                // .get() will block until this specific chunk is decompressed.
                // Since we iterate sequentially, we guarantee ordered processing.
                std::vector<char> decompressed_data = results[i].get();

                // Write sparse padding if necessary.
                uint64_t expected_offset = (uint64_t)chunk.start_sector * 4096;
                while (current_offset < expected_offset) {
                    uint64_t to_write = std::min(expected_offset - current_offset, (uint64_t)write_fill.size());
                    out_f.write(write_fill.data(), to_write);
                    current_offset += to_write;
                }
                
                // Write the decompressed data.
                out_f.write(decompressed_data.data(), decompressed_data.size());
                current_offset += decompressed_data.size();
            }

            // Add the final padding logic after processing all chunks for a partition.
            // This ensures the image file has the correct final sparse size.
            if (!chunks.empty()) {
                const auto& last_chunk = chunks.back();
                uint64_t expected_final_offset = ((uint64_t)last_chunk.start_sector + last_chunk.sector_count) * 4096;
                while (current_offset < expected_final_offset) {
                    uint64_t to_write = std::min(expected_final_offset - current_offset, (uint64_t)write_fill.size());
                    out_f.write(write_fill.data(), to_write);
                    current_offset += to_write;
                }
            }

            std::cout << "  done. extracted size = " << current_offset - ((uint64_t)chunks[0].part_start_sector * 4096) << " bytes" << std::endl << std::endl;
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