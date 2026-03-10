#include "secure_partition_builder.hpp"
#include <cstring>
#include <string>
#include <iostream>
#include <utility>

SecurePartitionBuilder::SecurePartitionBuilder(const json &metadata)
{
    if (!metadata.contains("secure_partition")) return;

    std::cout << "Building Secure Partition block..." << std::endl;
    const auto &sec_meta = metadata["secure_partition"];

    std::vector<char> buffer;
    buffer.reserve(SP_SIZE);

    // Write header
    SecurePartitionHeader header{};
    header.magic = sec_meta["magic"];
    header.flags = sec_meta["flags"];
    header.part_count = sec_meta["partitions"].size();

    auto signature_vec = unhexlify(sec_meta["signature"].get<std::string>());
    header.sig_size = signature_vec.size();
    std::memset(header.signature, 0, sizeof(header.signature));
    std::memcpy(header.signature, signature_vec.data(), signature_vec.size());

    buffer.insert(buffer.end(), reinterpret_cast<char *>(&header), reinterpret_cast<char *>(&header) + sizeof(header));

    // Write partition records
    for (const auto &part : sec_meta["partitions"])
    {
        SecurePartitionRecord record{};
        auto name_vec = encode_asciiz(part["name"], sizeof(record.name));
        std::memcpy(record.name, name_vec.data(), sizeof(record.name));

        record.hw_part = part["hw_part"];
        record.logical_part = part["logical_part"];
        record.start_sect = part["start_sect"];
        record.end_sect = part["end_sect"];
        record.data_sect_cnt = part["data_sect_cnt"];
        record.reserved = part["reserved"];

        auto hash_vec = unhexlify(part["hash"].get<std::string>());
        std::memcpy(record.hash, hash_vec.data(), sizeof(record.hash));

        buffer.insert(buffer.end(), reinterpret_cast<char *>(&record), reinterpret_cast<char *>(&record) + sizeof(record));
    }

    // Pad to the full size
    buffer.resize(SP_SIZE, 0);
    this->data = std::move(buffer);
    std::cout << "Secure Partition block built (" << this->data.size() << " bytes)." << std::endl;
}
