#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <thread>
#include <algorithm>
#include <stdexcept>

// --- Headers required for unpacking ---
#include "kdz_parser.hpp"
#include "secure_partition_parser.hpp"
#include "dz_parser.hpp"
#include "extractor.hpp"
#include "metadata_generator.hpp"

// --- Headers required for repacking ---
#include "secure_partition_builder.hpp"
#include "kdz_builder.hpp"
#include "dz_builder.hpp"

namespace fs = std::filesystem;

void printUsage(const char* progName) {
    std::cerr << "A tool to extract and repack LG KDZ firmware." << std::endl;
    std::cerr << "Usage: " << progName << " <command> [options]" << std::endl << std::endl;
    std::cerr << "Commands:" << std::endl;
    std::cerr << "  extract    Extract a KDZ file to a folder." << std::endl;
    std::cerr << "  repack     Repack an extracted folder into a KDZ file." << std::endl << std::endl;
    std::cerr << "Options for 'extract':" << std::endl;
    std::cerr << "  " << progName << " extract <kdz_file> [-d <path>] [--no-verify]" << std::endl;
    std::cerr << "    <kdz_file>           Path to the input KDZ firmware file." << std::endl;
    std::cerr << "    -d, --dest <path>    The directory to extract files to." << std::endl;
    std::cerr << "                         (If not specified, only header info will be printed)." << std::endl;
    std::cerr << "    --no-verify          Skip DZ data hash verification for faster startup." << std::endl << std::endl;
    std::cerr << "Options for 'repack':" << std::endl;
    std::cerr << "  " << progName << " repack <input_dir> <output_file>" << std::endl;
    std::cerr << "    <input_dir>          Path to the directory containing extracted files and metadata.json." << std::endl;
    std::cerr << "    <output_file>        Path for the new output KDZ file." << std::endl << std::endl;
    std::cerr << "General Options:" << std::endl;
    std::cerr << "  -h, --help           Show this help message and exit." << std::endl;
}

int main(int argc, char* argv[]) {
    // Handle help options in priority
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (argc < 2) {
        std::cerr << "Error: No command specified. Use 'extract' or 'repack'." << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    try {
        std::string command = argv[1];
        size_t num_threads = std::max(1u, std::thread::hardware_concurrency() / 2);
        ThreadPool pool(num_threads);
        
        if (command == "extract") {
            std::string file_path;
            std::optional<std::string> extract_path;
            bool skip_verification = false;

            for (int i = 2; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "--no-verify") {
                    skip_verification = true;
                } else if (arg == "-d" || arg == "--dest") {
                    if (i + 1 < argc) {
                        extract_path = argv[++i];
                    } else {
                        std::cerr << "Error: " << arg << " option requires an argument." << std::endl;
                        printUsage(argv[0]);
                        return 1;
                    }
                } else {
                    if (!file_path.empty()) {
                        std::cerr << "Error: Multiple input files specified for extract. Only one is allowed." << std::endl;
                        printUsage(argv[0]);
                        return 1;
                    }
                    file_path = arg;
                }
            }

            if (file_path.empty()) {
                std::cerr << "Error: Input KDZ file not specified for extract command." << std::endl;
                printUsage(argv[0]);
                return 1;
            }

            std::ifstream in_file(file_path, std::ios::binary);
            if (!in_file) {
                throw std::runtime_error("Cannot open file " + file_path);
            }

            // 1. Parse all headers and store the object
            KdzHeader kdz_header(in_file);
            kdz_header.print_info(in_file);

            std::optional<SecurePartition> sec_part = SecurePartition::parse(in_file);
            if (sec_part.has_value()) {
                sec_part->print_info();
            } else {
                std::cout << "No secure partition found\n" << std::endl;
            }
            
            const KdzHeader::Record* dz_record_ptr = nullptr;
            for (const auto& record : kdz_header.records) {
                if (record.name.size() >= 3 && record.name.substr(record.name.size() - 3) == ".dz") {
                    dz_record_ptr = &record;
                    break;
                }
            }
            if (!dz_record_ptr) {
                throw std::runtime_error("No DZ record in KDZ file");
            }

            DzHeader dz_hdr(in_file, *dz_record_ptr, skip_verification);
            dz_hdr.print_info();

            // 2. If unpacking is requested, extract all embedded objects and their metadata.
            if (extract_path.has_value()) {
                fs::create_directories(*extract_path);

                // Unpacking DLLs and other components
                extract_kdz_components(in_file, kdz_header, *extract_path);
                
                // Use thread pool to unpack DZ partitions
                std::cout << "Initializing thread pool with " << num_threads << " threads for extraction." << std::endl << std::endl;
                extract_dz_parts(file_path, dz_hdr, *extract_path, pool);

                // Unpacking V3's additional information
                extract_additional_data(in_file, kdz_header, *extract_path);

                // 3. Generate and store metadata.json
                generate_metadata(*extract_path, kdz_header, sec_part, dz_hdr);
            
            } else {
                 // If not unpacked, only print detailed information
                 for (const auto& hw_pair : dz_hdr.parts) {
                    std::cout << "Partition " << hw_pair.first << ":" << std::endl;
                    for (const auto& name_pair : hw_pair.second) {
                        std::cout << "  " << name_pair.first << std::endl;
                        int i = 0;
                        for (const auto& chunk : name_pair.second) {
                            std::cout << "    " << i++ << ". " << chunk.name 
                                      << " (" << std::max(chunk.data_size, chunk.sector_count * 4096u) << " bytes, sparse: " 
                                      << (chunk.is_sparse ? "true" : "false") << ")" << std::endl;
                        }
                        std::cout << std::endl;
                    }
                 }
            }

        } else if (command == "repack") {
            if (argc != 4) {
                std::cerr << "Error: Invalid number of arguments for repack command." << std::endl;
                std::cerr << "Usage: " << argv[0] << " repack <input_dir> <output_file>" << std::endl;
                return 1;
            }

            fs::path input_dir(argv[2]);
            fs::path output_file(argv[3]);

            auto metadata_path = input_dir / "metadata.json";
            if (!fs::exists(metadata_path)) {
                throw std::runtime_error("ERROR: metadata.json not found in '" + input_dir.string() + "'");
            }

            std::ifstream meta_file(metadata_path);
            json metadata = json::parse(meta_file);

            // 1. Create Secure Partition data (if it exists)
            SecurePartitionBuilder sec_part_builder(metadata);

            // 2. Use the thread pool to create DZ archive data
            std::cout << "Using " << num_threads << " threads for parallel processing." << std::endl;
            DzBuilder dz_builder(metadata);
            auto dz_binary_data = dz_builder.build(input_dir, pool);

            // 3. Creating the final KDZ profile
            KdzBuilder kdz_builder(metadata);
            kdz_builder.build(output_file, input_dir, dz_binary_data, sec_part_builder.data);
        } else {
            std::cerr << "Error: Unknown command '" << command << "'. Use 'extract' or 'repack'." << std::endl;
            printUsage(argv[0]);
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}