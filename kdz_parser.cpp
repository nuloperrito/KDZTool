#include "kdz_parser.hpp"
#include "utils.hpp"
#include <iostream>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <algorithm>

KdzHeader::KdzHeader(std::ifstream& file) {
    file.seekg(0);
    std::vector<char> hdr_data(KDZV3_HDR_SIZE);
    file.read(hdr_data.data(), KDZV3_HDR_SIZE);

    uint32_t read_size, read_magic;
    std::memcpy(&read_size, hdr_data.data(), sizeof(uint32_t));
    std::memcpy(&read_magic, hdr_data.data() + 4, sizeof(uint32_t));

    if (read_size == KDZV3_HDR_SIZE && read_magic == KDZV3_MAGIC) {
        parse_v3_header(hdr_data);
    } else if (read_size == KDZV2_HDR_SIZE && read_magic == KDZV2_MAGIC) {
        parse_v2_header(hdr_data);
    } else if (read_size == KDZV1_HDR_SIZE && read_magic == KDZV1_MAGIC) {
        parse_v1_header(hdr_data);
    } else {
        throw std::runtime_error("Unknown KDZ header (size=" + std::to_string(read_size) + ", magic=0x" + bytes_to_hex(reinterpret_cast<uint8_t*>(&read_magic), 4) + ")");
    }
    this->magic = read_magic;
    this->size = read_size;
}

void KdzHeader::parse_v1_header(const std::vector<char>& data) {
    this->version = 1;
    const char* p = data.data() + 8; // Skip size and magic

    KDZ_V1RECORD_FMT dz_rec, dll_rec;
    std::memcpy(&dz_rec, p, sizeof(KDZ_V1RECORD_FMT));
    p += sizeof(KDZ_V1RECORD_FMT);
    std::memcpy(&dll_rec, p, sizeof(KDZ_V1RECORD_FMT));
    
    records.push_back({decode_asciiz(dz_rec.name, 256), dz_rec.size, dz_rec.offset});
    records.push_back({decode_asciiz(dll_rec.name, 256), dll_rec.size, dll_rec.offset});

    // Fill in fields used by other versions with defaults
    this->tag = "";
    this->ftm_model_name = "";
    this->additional_records_size = 0;
    this->extended_mem_id = {0, 0};
    this->suffix_map = {0, 0};
    this->sku_map = {0, 0};
    this->extended_sku_map = {0, 0};
}

void KdzHeader::parse_v2_header(const std::vector<char>& data) {
    this->version = 2;
    const char* p = data.data() + 8;

    KDZ_V2RECORD_FMT dz_rec, dll_rec, dylib_rec, unknown_rec;
    std::memcpy(&dz_rec, p, sizeof(KDZ_V2RECORD_FMT));
    p += sizeof(KDZ_V2RECORD_FMT);
    std::memcpy(&dll_rec, p, sizeof(KDZ_V2RECORD_FMT));
    p += sizeof(KDZ_V2RECORD_FMT);
    
    uint8_t marker = *reinterpret_cast<const uint8_t*>(p);
    if (marker != 0x00 && marker != 0x03) {
         throw std::runtime_error("Unexpected byte after DLL record: 0x" + bytes_to_hex(&marker, 1));
    }
    p += 1;

    std::memcpy(&dylib_rec, p, sizeof(KDZ_V2RECORD_FMT));
    p += sizeof(KDZ_V2RECORD_FMT);
    std::memcpy(&unknown_rec, p + 272, sizeof(KDZ_V2RECORD_FMT)); // Offset 825 in header

    KDZ_V2RECORD_FMT all_recs[] = {dz_rec, dll_rec, dylib_rec, unknown_rec};
    for (const auto& rec : all_recs) {
        std::string name = decode_asciiz(rec.name, 256);
        if (!name.empty()) {
            records.push_back({name, rec.size, rec.offset});
        }
    }

    // Fill in fields used by other versions
    this->tag = "";
    this->ftm_model_name = "";
    this->additional_records_size = 0;
    this->extended_mem_id = {0, 0};
    this->suffix_map = {0, 0};
    this->sku_map = {0, 0};
    this->extended_sku_map = {0, 0};
}


void KdzHeader::parse_v3_header(const std::vector<char>& data) {
    this->version = 3;
    const char* p = data.data() + 8;

    KDZ_V2RECORD_FMT dz_rec, dll_rec, dylib_rec, unknown_rec;
    std::memcpy(&dz_rec, p, sizeof(KDZ_V2RECORD_FMT)); p += sizeof(KDZ_V2RECORD_FMT);
    std::memcpy(&dll_rec, p, sizeof(KDZ_V2RECORD_FMT)); p += sizeof(KDZ_V2RECORD_FMT);

    uint8_t marker = *reinterpret_cast<const uint8_t*>(p);
    if (marker != 0x00 && marker != 0x03) {
         throw std::runtime_error("Unexpected byte after DLL record: 0x" + bytes_to_hex(&marker, 1));
    }
    p += 1;

    std::memcpy(&dylib_rec, p, sizeof(KDZ_V2RECORD_FMT)); p += sizeof(KDZ_V2RECORD_FMT);
    // V3 has a different layout for the unknown_record vs v2
    std::memcpy(&unknown_rec, data.data() + 825, sizeof(KDZ_V2RECORD_FMT));

    KDZ_V2RECORD_FMT all_recs[] = {dz_rec, dll_rec, dylib_rec, unknown_rec};
    for (const auto& rec : all_recs) {
        std::string name = decode_asciiz(rec.name, 256);
        if (!name.empty()) {
            records.push_back({name, rec.size, rec.offset});
        }
    }
    
    uint32_t ext_mem_id_size;
    char tag_buf[6] = {0};
    std::memcpy(&ext_mem_id_size, data.data() + 1097, sizeof(uint32_t));
    std::memcpy(tag_buf, data.data() + 1101, 5);
    this->tag = decode_asciiz(tag_buf, 5);

    std::memcpy(&this->additional_records_size, data.data() + 1106, sizeof(uint64_t));
    std::memcpy(&this->suffix_map.offset, data.data() + 1114, sizeof(uint64_t));
    std::memcpy(&this->suffix_map.size, data.data() + 1122, sizeof(uint32_t));
    std::memcpy(&this->sku_map.offset, data.data() + 1126, sizeof(uint64_t));
    std::memcpy(&this->sku_map.size, data.data() + 1134, sizeof(uint32_t));
    
    char ftm_buf[33] = {0};
    std::memcpy(ftm_buf, data.data() + 1138, 32);
    this->ftm_model_name = decode_asciiz(ftm_buf, 32);

    std::memcpy(&this->extended_sku_map.offset, data.data() + 1170, sizeof(uint64_t));
    std::memcpy(&this->extended_sku_map.size, data.data() + 1178, sizeof(uint32_t));

    this->extended_mem_id = {EXTENDED_MEM_ID_OFFSET, ext_mem_id_size};
}

void KdzHeader::print_info(std::ifstream& file) const {
    auto read_asciiz_data = [&](uint64_t offset, uint32_t size) -> std::string {
        if (size == 0) return "";
        std::vector<char> buffer(size);
        file.seekg(offset);
        file.read(buffer.data(), size);
        // Use the utility function for consistency, though not strictly required here.
        // It's important to pass the actual size read, not the buffer's capacity.
        return decode_asciiz(buffer.data(), file.gcount());
    };

    auto format_as_list = [](const std::vector<std::string>& vec) {
        std::stringstream ss;
        ss << "[";
        for (size_t i = 0; i < vec.size(); ++i) {
            ss << "'" << vec[i] << "'";
            if (i < vec.size() - 1) {
                ss << ", ";
            }
        }
        ss << "]";
        return ss.str();
    };
    
    std::cout << "KDZ Header" << std::endl;
    std::cout << "==========" << std::endl;
    std::cout << "version = " << this->version << ", magic = " << std::hex << this->magic << std::dec << ", size = " << this->size << std::endl;
    std::cout << "records = " << this->records.size() << std::endl;
    for (const auto& rec : this->records) {
        std::cout << "  Record(name='" << rec.name << "', size=" << rec.size << ", offset=" << rec.offset << ")" << std::endl;
    }
    std::cout << "tag = " << this->tag << std::endl;
    std::cout << "extended_mem_id = AdditionalRecord(offset=" << this->extended_mem_id.offset << ", size=" << this->extended_mem_id.size << ")" << std::endl;
    std::cout << "  data = " << read_asciiz_data(this->extended_mem_id.offset, this->extended_mem_id.size) << std::endl;
    std::cout << "additional_records_size = " << this->additional_records_size << std::endl;
    std::cout << "  suffix_map = AdditionalRecord(offset=" << this->suffix_map.offset << ", size=" << this->suffix_map.size << ")" << std::endl;
    std::cout << "    data = " << format_as_list(split_string(read_asciiz_data(this->suffix_map.offset, this->suffix_map.size), '\n')) << std::endl;
    std::cout << "  sku_map = AdditionalRecord(offset=" << this->sku_map.offset << ", size=" << this->sku_map.size << ")" << std::endl;
    std::cout << "    data = " << format_as_list(split_string(read_asciiz_data(this->sku_map.offset, this->sku_map.size), '\n')) << std::endl;
    std::cout << "  extended_sku_map = AdditionalRecord(offset=" << this->extended_sku_map.offset << ", size=" << this->extended_sku_map.size << ")" << std::endl;
    std::cout << "    data =" << std::endl;
    if (this->extended_sku_map.size > 0) {
        std::cout << "      " << read_asciiz_data(this->extended_sku_map.offset, this->extended_sku_map.size) << std::endl;
    }
    std::cout << "ftm_model_name = " << this->ftm_model_name << std::endl << std::endl;
}