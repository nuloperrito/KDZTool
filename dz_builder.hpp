#ifndef DZ_BUILDER_HPP
#define DZ_BUILDER_HPP

#include <vector>
#include <filesystem>
#include <cstdint>
#include <mutex>
#include "utils.hpp"
#include "thread_pool.hpp"
#include "shared_structure.hpp"

class DzBuilder {
private:
    const json& meta;
    std::mutex cout_mutex; // Mutex for protecting std::cout
    std::vector<char> compress_data(const std::vector<char>& input) const;
    std::vector<char> md5_hash(const void* data, size_t size) const;

public:
    explicit DzBuilder(const json& metadata) : meta(metadata["dz"]) {}
    std::vector<char> build(const std::filesystem::path& input_dir, ThreadPool& pool);
};

#endif