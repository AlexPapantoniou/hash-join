cmake_minimum_required(VERSION 3.16)

project(SigmodContest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

Include(FetchContent)

FetchContent_Declare(
    Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.8.0.tar.gz
)

FetchContent_MakeAvailable(Catch2)

FetchContent_Declare(
    abseil
    URL https://github.com/abseil/abseil-cpp/releases/download/20240722.1/abseil-cpp-20240722.1.tar.gz
)

set(ABSL_PROPAGATE_CXX_STD ON)
set(ABSL_ENABLE_INSTALL ON)
FetchContent_MakeAvailable(abseil)

FetchContent_Declare(
    re2
    URL https://github.com/google/re2/releases/download/2024-07-02/re2-2024-07-02.tar.gz
)

FetchContent_MakeAvailable(re2)

FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)

FetchContent_MakeAvailable(json)

FetchContent_Declare(
    sql-parser
    URL https://github.com/a858438680/sql-parser/archive/refs/tags/win-port-2.tar.gz
)
set(HSQL_ENABLE_WERROR OFF)
FetchContent_MakeAvailable(sql-parser)

FetchContent_Declare(
    range-v3
    URL https://github.com/ericniebler/range-v3/archive/refs/tags/0.12.0.tar.gz
)

FetchContent_MakeAvailable(range-v3)

FetchContent_Declare(
    fmtlib
    URL https://github.com/fmtlib/fmt/releases/download/11.1.3/fmt-11.1.3.zip
)

FetchContent_MakeAvailable(fmtlib)

FetchContent_Declare(
    duckdb
    URL https://github.com/duckdb/duckdb/archive/refs/tags/v1.2.1.tar.gz
)
set(ENABLE_SANITIZER OFF)
set(ENABLE_UBSAN OFF)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "ppc|powerpc|ppc64|ppc64le")
    message("Disabling jemalloc extension of DuckDB on Power.")
    set(SKIP_EXTENSIONS jemalloc)
endif()
FetchContent_MakeAvailable(duckdb)

# Include all sources from /src directory. CONFIGURE_DEPENDS can be unreliable.
# Try re-running cmake in case changes are not recognized.
file(GLOB SIGMODPC_SRC
    CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/hashmap_*.hpp"
)

# Enable server-specific compiler optimizations.
# Use march=native for all but Power servers, which results in the following error:
#    clang++-18: error: unsupported option '-march=' for target 'powerpc64le-unknown-linux-gnu'
# This flag works on other Power systems, but for now, we disable march=native on all Power machines.
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "ppc|powerpc|ppc64|ppc64le")
    add_compile_options(-march=native)
endif()

# Targets
add_executable(run ${SIGMODPC_SRC} tests/read_sql.cpp)
add_executable(build_database tests/build_database.cpp)
add_executable(unit_tests ${SIGMODPC_SRC} tests/unit_tests.cpp)
add_executable(robinhood_tests tests/robinhood_tests.cpp)
add_executable(hopscotch_tests tests/hopscotch_tests.cpp)
add_executable(cuckoo_tests tests/cuckoo_tests.cpp)
add_executable(unchained_tests tests/unchained_tests.cpp)
add_executable(columnar_utils_tests tests/columnar_utils_tests.cpp)
add_executable(column_store_utils_tests tests/column_store_utils_tests.cpp)
add_executable(slab_allocator_tests tests/slab_allocator_tests.cpp)

#compile execute.cpp with each algorithm
add_custom_target(robinhood
    COMMAND cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -Wno-dev
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}  # ensures fast runs in source dir
)

add_custom_target(hopscotch
    COMMAND cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -Wno-dev
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_target(cuckoo
    COMMAND cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -Wno-dev
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_target(unordered
    COMMAND cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -Wno-dev
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_target(unchained
    COMMAND cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -Wno-dev
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_target(columnar_utils
    COMMAND cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -Wno-dev
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_target(column_store_utils
    COMMAND cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -Wno-dev
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

#tests
add_custom_target(run_robinhood
    COMMAND cmake --build build --target robinhood_tests -j 3
    COMMAND ./build/robinhood_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_target(run_hopscotch
    COMMAND cmake --build build --target hopscotch_tests -j 3
    COMMAND ./build/hopscotch_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_target(run_cuckoo
    COMMAND cmake --build build --target cuckoo_tests -j 3
    COMMAND ./build/cuckoo_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_target(run_unchained
    COMMAND cmake --build build --target unchained_tests -j 3
    COMMAND ./build/unchained_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_target(run_columnar_utils
    COMMAND cmake --build build --target columnar_utils_tests -j 3
    COMMAND ./build/columnar_utils_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_target(run_column_store_utils
    COMMAND cmake --build build --target column_store_utils_tests -j 3
    COMMAND ./build/column_store_utils_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_target(run_slab_tests
    COMMAND cmake --build build --target slab_allocator_tests -j 3
    COMMAND ./build/slab_allocator_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_target(run_unit
    COMMAND cmake --build build --target unit_tests -j 3
    COMMAND ./build/unit_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

#queries
add_custom_target(queries
    COMMAND $<TARGET_FILE:fast> "${CMAKE_SOURCE_DIR}/plans.json"
    DEPENDS fast
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# For all executables
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(run PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(build_database PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(unit_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_executable(build_cache ${SIGMODPC_SRC} tests/read_sql.cpp)
    add_executable(fast ${SIGMODPC_SRC} tests/read_sql.cpp)
    add_executable(leaderboard ${SIGMODPC_SRC} tests/read_sql.cpp)

    target_link_libraries(build_cache PRIVATE re2 fmt range-v3 nlohmann_json::nlohmann_json sqlparser duckdb)
    target_compile_definitions(build_cache PRIVATE TEAMOPT_USE_DUCKDB TEAMOPT_BUILD_CACHE)
    target_compile_definitions(leaderboard PRIVATE)
    target_link_libraries(fast PRIVATE re2 fmt range-v3 nlohmann_json::nlohmann_json sqlparser)
    target_link_libraries(leaderboard PRIVATE re2 fmt range-v3 nlohmann_json::nlohmann_json sqlparser)
    target_include_directories(build_cache PRIVATE include)
    target_include_directories(fast PRIVATE include)
    target_include_directories(leaderboard PRIVATE include)
endif()

target_link_libraries(run PRIVATE re2 fmt range-v3 nlohmann_json::nlohmann_json sqlparser duckdb)
target_compile_definitions(run PRIVATE TEAMOPT_USE_DUCKDB)
target_link_libraries(build_database PRIVATE fmt duckdb)
target_link_libraries(unit_tests PRIVATE re2 range-v3 fmt Catch2::Catch2WithMain)
target_link_libraries(robinhood_tests PRIVATE Catch2::Catch2WithMain fmt)
target_link_libraries(hopscotch_tests PRIVATE Catch2::Catch2WithMain fmt)
target_link_libraries(cuckoo_tests PRIVATE Catch2::Catch2WithMain fmt)
target_link_libraries(unchained_tests PRIVATE Catch2::Catch2WithMain fmt)
target_link_libraries(columnar_utils_tests PRIVATE re2 range-v3 Catch2::Catch2WithMain fmt)
target_link_libraries(column_store_utils_tests PRIVATE re2 range-v3 Catch2::Catch2WithMain fmt)
target_link_libraries(slab_allocator_tests PRIVATE re2 range-v3 Catch2::Catch2WithMain fmt)

target_include_directories(run PRIVATE include)
target_include_directories(build_database PRIVATE include)
target_include_directories(unit_tests PRIVATE include)
target_include_directories(robinhood_tests PRIVATE include)
target_include_directories(hopscotch_tests PRIVATE include)
target_include_directories(cuckoo_tests PRIVATE include)
target_include_directories(unchained_tests PRIVATE include)
target_include_directories(columnar_utils_tests PRIVATE include)
target_include_directories(column_store_utils_tests PRIVATE include)
target_include_directories(slab_allocator_tests PRIVATE include)
