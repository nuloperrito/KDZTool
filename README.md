# LG KDZ Firmware Tool

A high-performance, cross-platform command-line utility for extracting and repacking LG official firmware files (`.kdz`). This tool is written in modern C++ and is designed for power users, developers, and researchers who need to inspect or modify LG device firmware.

## Overview

LG's official firmware is distributed in a proprietary container format known as KDZ. Inside a KDZ file, the core operating system images are contained within a `.dz` archive, which itself is a container for compressed partition chunks. This tool provides a complete solution to deconstruct these files into their base components and, crucially, to rebuild them back into a valid KDZ file that can be flashed to a device.

The tool uses multi-threading to accelerate the computationally intensive tasks of compression and decompression, significantly speeding up the workflow.

## Features

  - **Full KDZ Support:** Parses and builds KDZ format versions V1, V2, and V3.
  - **Firmware Extraction:** Extracts all contents from a KDZ file, including:
      - The main `.dz` firmware archive.
      - Supporting DLLs and dylib files.
      - The `SecurePartition` block.
      - V3-specific metadata maps (`suffix_map`, `sku_map`, etc.).
  - **Partition Image Reconstruction:** Reconstructs full partition images (e.g., `system.img`, `boot.img`) from compressed chunks within the `.dz` file, correctly handling sparse layouts.
  - **Firmware Repacking:** Repacks an extracted directory—including any modified partition images—back into a single, flashable KDZ file with updated checksums.
  - **High Performance:** Leverages a thread pool to perform parallel compression (`zlib`/`zstd`) and decompression, maximizing CPU usage for faster operation.
  - **Metadata Management:** On extraction, generates a comprehensive `metadata.json` file that describes the entire structure of the original KDZ. This file is the blueprint for the repacking process.
  - **Cross-Platform:** Built with CMake, allowing it to be compiled and run on Windows, macOS, and Linux.

## How It Works

The tool operates in two main modes: `extract` and `repack`.

#### Extraction Process

1.  **Parse KDZ Header:** The tool first reads the main KDZ header to identify its version (V1/V2/V3) and locate all primary components like the `.dz` archive and any accompanying `.dll` files.
2.  **Parse DZ & Secure Partition:** It then parses the `SecurePartition` block and the main `.dz` header, verifying magic numbers and checksums to ensure file integrity.
3.  **Decompress in Parallel:** The core task of decompression is parallelized. Each compressed data chunk from the `.dz` file is assigned to a worker thread.
4.  **Reconstruct Images:** As chunks are decompressed, they are written to the correct sparse offset within their corresponding output image file (e.g., `0.boot.img`). This reconstructs the original, full-sized partition images for all the partitions (e.g., `boot`, `system`, `modem`).
5.  **Extract Components:** Ancillary files (`.dll`, `.dylib`, `suffix_map.dat`, etc.) are extracted into a `components` subdirectory.
6.  **Generate Metadata:** Finally, all structural information—offsets, sizes, checksums, version info, partition layouts, and more—is saved to a human-readable `metadata.json` file.

#### Repacking Process

1.  **Read Metadata:** The repacking process is driven entirely by the `metadata.json` file from an extracted firmware directory.
2.  **Compress in Parallel:** The tool reads the raw partition images (`.img`), slices them into chunks according to the metadata, and compresses each chunk in a worker thread.
3.  **Rebuild DZ Archive:** It calculates new MD5 hashes for the compressed chunks and assembles them into a new `.dz` file in memory. A new main DZ header is generated with updated `chunk_hdrs_hash`, `data_hash`, and `header_crc`.
4.  **Rebuild Secure Partition:** The `SecurePartition` block is rebuilt from the information stored in the metadata.
5.  **Assemble Final KDZ:** The tool creates the final KDZ file. It writes the rebuilt `.dz` archive, the `SecurePartition` block, and the other components from the `components` directory at their original offsets.
6.  **Write Final Header:** With all data in place, the final offsets and sizes are known. The tool constructs the definitive KDZ header (V1, V2, or V3) and writes it to the beginning of the file, completing the process.

## Prerequisites

To build this project, you will need:

  - A C++17 compliant compiler (e.g., GCC, Clang, MSVC)
  - CMake (version 3.15 or newer)
  - **Zlib** library (development headers)
  - **Zstandard (zstd)** library (development headers)

## Building

The project can be built using a standard C++ compiler and CMake. For example, under Linux distros, you can compile with a command like this:

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Usage

The tool is operated via the command line with two main commands: `extract` and `repack`.

```
A tool to extract and repack LG KDZ firmware.
Usage: ./kdz-tool <command> [options]

Commands:
  extract    Extract a KDZ file to a folder.
  repack     Repack an extracted folder into a KDZ file.

General Options:
  -h, --help           Show this help message and exit.
```

### Extracting a KDZ

This command parses a KDZ file and extracts its contents into a specified directory. If no directory is provided, it will only print the header information without writing any files.

**Syntax:**

```
./kdz-tool extract <kdz_file> [-d <path>] [--no-verify]
```

  - `<kdz_file>`: Path to the input KDZ firmware file.
  - `-d, --dest <path>`: The directory to extract files to.
  - `--no-verify`: (Optional) Skip the full DZ data hash verification for a faster initial parse. Useful for quick inspection.

**Example:**

```bash
./kdz-tool extract G850UM20A_00_NAO_US_OP_0416.kdz -d G850_extracted
```

After extraction, the output directory will have the following structure:

```
G850_extracted/
├── 0.abl.img
├── 0.aop.img
├── 0.boot.img
├── ... (other partition images)
├── components/
│   ├── LGE_COMMON.dll
│   ├── LGE_VER.dll
│   ├── suffix_map.dat
│   └── ... (other components)
└── metadata.json
```

### Repacking a Directory

This command rebuilds a KDZ file from an extracted directory containing partition images, components, and a `metadata.json` file.

**Syntax:**

```
./kdz-tool repack <input_dir> <output_file>
```

  - `<input_dir>`: Path to the directory containing extracted files and `metadata.json`.
  - `<output_file>`: Path for the new output KDZ file to be created.

**Example:**

```bash
./kdz-tool repack G850_extracted my_custom_firmware.kdz
```

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Acknowledgments

This tool relies on several excellent open-source libraries:

  - [**nlohmann/json**](https://github.com/nlohmann/json): For easy and robust JSON parsing and serialization.
  - [**zlib**](https://www.zlib.net/): For handling `zlib` compression.
  - [**Zstandard (zstd)**](https://facebook.github.io/zstd/): For handling `zstd` compression.
  - The MD5 implementation is based on the work of **bzflag**, available at [www.zedwood.com](http://www.zedwood.com/article/cpp-md5-function).

-----

## Appendix: Detailed Header Structure and Field Analysis

### KDZ File Structure (`KdzHeader`)

#### KDZ Header (V1 Version)
Total Size: 1304 Bytes

| Field Name | Starting Offset | Size | Data Type | Description |
| :--- | :--- | :--- | :--- | :--- |
| `size` | 0 | 4 | Unsigned Int | Header size, fixed at 1304 |
| `magic` | 4 | 4 | Unsigned Int | Magic number, fixed at `0x50447932` |
| `dz_record` | 8 | 264 | Struct | DZ file record (see table below) |
| `dll_record` | 272 | 264 | Struct | DLL file record (see table below) |
| `padding` | 536 | 768 | Byte Array | Zero-padding |

**V1 Record Structure (`V1_RECORD_FMT`)**
Size: 264 Bytes

| Field Name | Size | Data Type | Description |
| :--- | :--- | :--- | :--- |
| `name` | 256 | Char Array | File name (null-terminated ASCII string) |
| `size` | 4 | Unsigned Int | File size |
| `offset` | 4 | Unsigned Int | Starting offset of the file within the KDZ |

---

#### KDZ Header (V2 Version)
Total Size: 1320 Bytes

| Field Name | Starting Offset | Size | Data Type | Description |
| :--- | :--- | :--- | :--- | :--- |
| `size` | 0 | 4 | Unsigned Int | Header size, fixed at 1320 |
| `magic` | 4 | 4 | Unsigned Int | Magic number, fixed at `0x80253134` |
| `dz_record` | 8 | 272 | Struct | DZ file record (see table below) |
| `dll_record` | 280 | 272 | Struct | DLL file record (see table below) |
| `marker` | 552 | 1 | Byte | Marker byte, usually `0x00` or `0x03` |
| `dylib_record` | 553 | 272 | Struct | dylib file record (see table below) |
| `unknown_record`| 825 | 272 | Struct | Unknown record (usually empty) (see table below) |
| `padding` | 1097 | 223 | Byte Array | Zero-padding |

**V2/V3 Record Structure (`V2_RECORD_FMT`)**
Size: 272 Bytes

| Field Name | Size | Data Type | Description |
| :--- | :--- | :--- | :--- |
| `name` | 256 | Char Array | File name (null-terminated ASCII string) |
| `size` | 8 | Unsigned Long | File size (upgraded to 64-bit to support >4GB files) |
| `offset` | 8 | Unsigned Long | Starting offset of the file within the KDZ (upgraded to 64-bit) |

---

#### KDZ Header (V3 Version)
Total Size: 1320 Bytes

| Field Name | Starting Offset | Size | Data Type | Description |
| :--- | :--- | :--- | :--- | :--- |
| `size` | 0 | 4 | Unsigned Int | Header size, fixed at 1320 |
| `magic` | 4 | 4 | Unsigned Int | Magic number, fixed at `0x25223824` |
| `dz_record` | 8 | 272 | Struct | DZ file record (structure same as V2) |
| `dll_record` | 280 | 272 | Struct | DLL file record (structure same as V2) |
| `marker` | 552 | 1 | Byte | Marker byte, usually `0x00` or `0x03` |
| `dylib_record` | 553 | 272 | Struct | dylib file record (structure same as V2) |
| `unknown_record`| 825 | 272 | Struct | Unknown record (usually empty) (structure same as V2) |
| `extended_mem_id_size` | 1097 | 4 | Unsigned Int | Size of extended memory ID |
| `tag` | 1101 | 5 | Char Array | Tag information (e.g., "5.19") |
| `additional_records_size`| 1106 | 8 | Unsigned Long | Total size of all additional records |
| `suffix_map_offset` | 1114 | 8 | Unsigned Long | Offset of the Suffix Map |
| `suffix_map_size` | 1122 | 4 | Unsigned Int | Size of the Suffix Map |
| `sku_map_offset` | 1126 | 8 | Unsigned Long | Offset of the SKU Map |
| `sku_map_size` | 1134 | 4 | Unsigned Int | Size of the SKU Map |
| `ftm_model_name`| 1138 | 32 | Char Array | FTM (Factory Test Mode) model name |
| `extended_sku_map_offset`| 1170 | 8 | Unsigned Long | Offset of the Extended SKU Map |
| `extended_sku_map_size` | 1178 | 4 | Unsigned Int | Size of the Extended SKU Map |
| `padding` | 1182 | 138 | Byte Array | Zero-padding |

---

### Secure Partition Structure (`SecurePartition`)
Total Size: 82448 Bytes, Fixed Offset: 1320

#### Secure Partition Header
Size: 528 Bytes

| Field Name | Starting Offset | Size | Data Type | Description |
| :--- | :--- | :--- | :--- | :--- |
| `magic` | 0 | 4 | Unsigned Int | Magic number, fixed at `0x53430799` |
| `flags` | 4 | 4 | Unsigned Int | Flag bits |
| `part_count` | 8 | 4 | Unsigned Int | Total number of partition records |
| `sig_size` | 12 | 4 | Unsigned Int | Size of the signature |
| `signature` | 16 | 512 | Byte Array | Secure signature (maximum 512 bytes) |

#### Secure Partition Record Structure
Size: 80 Bytes

| Field Name | Size | Data Type | Description |
| :--- | :--- | :--- | :--- |
| `name` | 30 | Char Array | Partition name (null-terminated ASCII string) |
| `hw_part` | 1 | Byte | Hardware partition number |
| `logical_part` | 1 | Byte | Logical partition number |
| `start_sect` | 4 | Unsigned Int | Starting sector |
| `end_sect` | 4 | Unsigned Int | Ending sector |
| `data_sect_cnt` | 4 | Unsigned Int | Number of sectors occupied by data |
| `reserved` | 4 | Unsigned Int | Reserved field, should be 0 |
| `hash` | 32 | Byte Array | Checksum of the partition (SHA-256) |

---

### DZ File Structure (`DzHeader`)

#### DZ Main Header (`HDR_FMT`)
Total Size: 512 Bytes

| Field Name | Size | Data Type | Description |
| :--- | :--- | :--- | :--- |
| `magic` | 4 | Unsigned Int | DZ header magic number, fixed at `0x74189632` |
| `major` | 4 | Unsigned Int | DZ format major version number |
| `minor` | 4 | Unsigned Int | DZ format minor version number |
| `reserved` | 4 | Unsigned Int | Reserved field, should be 0 |
| `model_name` | 32 | Char Array | Device model name |
| `sw_version` | 128 | Char Array | Software version number |
| `build_date` | 16 | 8x Unsigned Short | Build date (Year/Month/Week/Day/Hour/Minute/Second/Millisecond) |
| `part_count` | 4 | Unsigned Int | Total number of partition chunks |
| `chunk_hdrs_hash`| 16 | Byte Array | MD5 checksum of all chunk headers |
| `secure_image_type`| 1 | Byte | Secure image type |
| `compression` | 9 | Char Array / Byte | Compression type ('zlib'/'zstd' or 1/4) |
| `data_hash` | 16 | Byte Array | MD5 checksum of all data |
| `swfv` | 50 | Char Array | Software Firmware Version (SWFV) |
| `build_type` | 16 | Char Array | Build type (USER/DEBUG) |
| `unknown_0` | 4 | Unsigned Int | Unknown field, should be 0 |
| `header_crc` | 4 | Unsigned Int | CRC32 checksum of the DZ main header |
| `android_ver` | 10 | Char Array | Android version number |
| `memory_size` | 11 | Char Array | Memory size |
| `signed_security` | 4 | Char Array | Whether it's a secure signature ('Y' or 'N') |
| `is_ufs` | 4 | Unsigned Int | Whether it's UFS storage (non-zero is yes) |
| `anti_rollback_ver`| 4 | Unsigned Int | Anti-rollback version number |
| `supported_mem`| 64 | Char Array | List of supported memory types |
| `target_product` | 24 | Char Array | Target product name |
| `multi_panel_mask` | 1 | Byte | Bitmask for multi-panel support |
| `product_fuse_id` | 1 | Byte | Product fuse ID (ASCII digit or byte from 0-9) |
| `unknown_1` | 4 | Unsigned Int | Unknown field, should be 0 or `0xFFFFFFFF` |
| `is_factory_image` | 1 | Byte | Whether it's factory firmware (ASCII 'F' means yes) |
| `operator_code` | 24 | Char Array | Operator code |
| `unknown_2` | 4 | Unsigned Int | Unknown field, should be 0 or 1 |
| `padding` | 44 | Byte Array | Zero-padding |

#### DZ Chunk Header (V0 Version)
Total Size: 124 Bytes

| Field Name | Size | Data Type | Description |
| :--- | :--- | :--- | :--- |
| `magic` | 4 | Unsigned Int | Chunk magic number, fixed at `0x78951230` |
| `part_name` | 32 | Char Array | Name of the parent partition |
| `chunk_name` | 64 | Char Array | Name of the chunk |
| `decompressed_size`| 4 | Unsigned Int | Size after decompression |
| `compressed_size` | 4 | Unsigned Int | Size after compression |
| `hash` | 16 | Byte Array | MD5 checksum of the chunk data |

#### DZ Chunk Header (V1 Version)
Total Size: 512 Bytes

| Field Name | Size | Data Type | Description |
| :--- | :--- | :--- | :--- |
| `magic` | 4 | Unsigned Int | Chunk magic number, fixed at `0x78951230` |
| `part_name` | 32 | Char Array | Name of the parent partition |
| `chunk_name` | 64 | Char Array | Name of the chunk |
| `decompressed_size`| 4 | Unsigned Int | Size after decompression |
| `compressed_size` | 4 | Unsigned Int | Size after compression |
| `hash` | 16 | Byte Array | MD5 checksum of the chunk data |
| `start_sector` | 4 | Unsigned Int | Starting sector on the device |
| `sector_count` | 4 | Unsigned Int | Number of sectors occupied |
| `hw_partition` | 4 | Unsigned Int | Hardware partition number |
| `crc` | 4 | Unsigned Int | CRC32 checksum of the chunk |
| `unique_part_id` | 4 | Unsigned Int | Unique partition ID |
| `is_sparse` | 4 | Unsigned Int | Whether it's a Sparse Image |
| `is_ubi_image` | 4 | Unsigned Int | Whether it's a UBI Image |
| `part_start_sector`| 4 | Unsigned Int | Starting sector of the entire partition |
| `padding` | 356 | Byte Array | Zero-padding |

### Note
* All multi-byte integer types (such as short, int, long) are stored in **Little-endian** format.
* All size units are in **Bytes**.