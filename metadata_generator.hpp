#ifndef METADATA_GENERATOR_HPP
#define METADATA_GENERATOR_HPP

#include "kdz_parser.hpp"
#include "secure_partition_parser.hpp"
#include "dz_parser.hpp"
#include <string>

void generate_metadata(
    const std::string& out_path,
    const KdzHeader& kdz_hdr,
    const std::optional<SecurePartition>& sec_part,
    const DzHeader& dz_hdr
);

#endif // METADATA_GENERATOR_HPP