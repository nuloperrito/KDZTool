#ifndef SECURE_PARTITION_BUILDER_HPP
#define SECURE_PARTITION_BUILDER_HPP

#include <vector>
#include "utils.hpp"
#include "shared_structure.hpp"

class SecurePartitionBuilder {
public:
    std::vector<char> data;
    explicit SecurePartitionBuilder(const json& metadata);
};

#endif
