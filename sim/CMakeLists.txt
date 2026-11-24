cmake_minimum_required(VERSION 3.20)
project(branch_predictor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# run_trace_gz: self-contained trace driver (one Bimodal, GShare, TAGE over the
# conditional branches of a .gz trace). Needs zlib; everything else is headers.
find_package(ZLIB REQUIRED)
add_executable(run_trace_gz run_trace_gz.cpp)
target_include_directories(run_trace_gz PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(run_trace_gz PRIVATE ZLIB::ZLIB)

# sizing: report each predictor's get_size() against the budget.
add_executable(sizing sizing.cpp)
target_include_directories(sizing PRIVATE ${CMAKE_CURRENT_LIST_DIR})
