#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <nlohmann/json.hpp>

// Use nlohmann::ordered_json to preserve the order of elements from metadata.json
using json = nlohmann::ordered_json;

// Decodes a null-terminated ASCII string from a char array.
std::string decode_asciiz(const char* buffer, size_t max_len);

// Encodes a string into a null-padded vector of chars of a specific length.
std::vector<char> encode_asciiz(const std::string& s, size_t length);

// Converts a vector of bytes to a hex string.
std::string bytes_to_hex(const std::vector<uint8_t>& bytes);
std::string bytes_to_hex(const uint8_t* bytes, size_t size);

// Decodes a hex string into a vector of bytes.
std::vector<uint8_t> unhexlify(const std::string& hex_str);

// Splits a string by a delimiter.
std::vector<std::string> split_string(const std::string& s, char delimiter);

// Reads the entire content of a file into a vector of chars.
std::vector<char> read_filepath(const std::filesystem::path& path);

// Reads a specific amount of data from a file into a vector.
template<typename T>
void read_filestream(std::ifstream& file, T& buffer) {
    file.read(reinterpret_cast<char*>(&buffer), sizeof(T));
    if (!file) {
        throw std::runtime_error("Failed to read from file.");
    }
}
#endif // UTILS_HPP