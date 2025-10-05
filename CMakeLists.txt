cmake_minimum_required(VERSION 3.16)
project(KDZTool CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ZLIB REQUIRED)
if(WIN32)
    if(MSVC)
        # Enable parallel build
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
        # Set CMake flag to use static runtime library
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

        # Alternatively, set compiler flags to explicitly specify /MT and /MTd
        foreach(flag_var
            CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
            CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO
            CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
            if(${flag_var} MATCHES "/MD")
            string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
            endif(${flag_var} MATCHES "/MD")
        endforeach(flag_var)
    endif()
    find_package(ZSTD REQUIRED)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ZSTD REQUIRED libzstd)
endif()

add_executable(kdz-tool
    main.cpp
    dz_builder.cpp
    dz_parser.cpp
    extractor.cpp
    kdz_builder.cpp
    kdz_parser.cpp
    metadata_generator.cpp
    secure_partition_builder.cpp
    secure_partition_parser.cpp
    common/utils.cpp
    common/md5.cpp
)

target_include_directories(kdz-tool PRIVATE
    ${PROJECT_SOURCE_DIR}/common
)

if(WIN32)
    target_include_directories(kdz-tool PRIVATE
        ${ZLIB_INCLUDE_DIRS}
    )
    target_link_libraries(kdz-tool PRIVATE
        ${ZLIB_LIBRARIES}
        zstd::libzstd_static
    )
else()
    target_include_directories(kdz-tool PRIVATE
        ${ZLIB_INCLUDE_DIRS}
        ${ZSTD_INCLUDE_DIRS}
    )
    target_link_libraries(kdz-tool PRIVATE
        ${ZLIB_LIBRARIES}
        ${ZSTD_LIBRARIES}
    )
endif()
