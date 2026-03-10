#include "utils.hpp"
#include <cstring>

std::string decode_asciiz(const char* buffer, size_t max_len) {
    // Find the actual length of the null-terminated string
    size_t len = 0;
    while (len < max_len && buffer[len] != '\0') len++;
    return std::string(buffer, len);
}

std::vector<char> encode_asciiz(const std::string& s, size_t length) {
    std::vector<char> buffer(length, 0);
    std::copy(s.begin(), s.end(), buffer.begin());
    return buffer;
}

std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
    return bytes_to_hex(bytes.data(), bytes.size());
}

std::string bytes_to_hex(const uint8_t* bytes, size_t size) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < size; ++i) {
        oss << std::setw(2) << static_cast<unsigned>(bytes[i]);
    }
    return oss.str();
}

std::vector<uint8_t> unhexlify(const std::string& hex_str) {
    std::vector<uint8_t> bytes;
    bytes.reserve(hex_str.length() / 2);
    for (size_t i = 0; i < hex_str.length(); i += 2) {
        std::string byteString = hex_str.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(strtol(byteString.c_str(), nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

std::vector<std::string> split_string(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string::size_type start = 0;
    std::string::size_type end = 0;
    
    while ((end = s.find(delimiter, start)) != std::string::npos) {
        tokens.push_back(s.substr(start, end - start));
        start = end + 1;
    }
    tokens.push_back(s.substr(start));
    
    return tokens;
}

std::vector<char> read_filepath(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) {
        throw std::runtime_error("Failed to read file: " + path.string());
    }
    return buffer;
}
