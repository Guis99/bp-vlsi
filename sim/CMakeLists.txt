cmake_minimum_required(VERSION 3.20)
project(branch_predictor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -O2")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_TESTS           "Build test executable"                ON)
option(BUILD_RUN_TRACES      "Build trace runner executable"        ON)
option(BUILD_EVAL_PREDICTORS "Build predictor evaluation executable" ON)
option(BUILD_REGRESSION      "Build TAGEImproved regression guard"  ON)

# export: static library
add_library(branch_predictor_lib STATIC
    traces.cpp
)
target_include_directories(branch_predictor_lib PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

# test: executable depending on branch_predictor_lib
if(BUILD_TESTS)
    add_executable(test_primitives tests/test_primitives.cpp)
    target_link_libraries(test_primitives PRIVATE branch_predictor_lib)
    target_include_directories(test_primitives PRIVATE ${CMAKE_CURRENT_LIST_DIR})

    add_custom_target(test-prim
        COMMAND $<TARGET_FILE:test_primitives>
        DEPENDS test_primitives
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()

# trace runner: executable depending on branch_predictor_lib
if(BUILD_RUN_TRACES)
    add_executable(run_traces runners/run_traces.cpp)
    target_link_libraries(run_traces PRIVATE branch_predictor_lib)
    target_include_directories(run_traces PRIVATE ${CMAKE_CURRENT_LIST_DIR})

    add_custom_target(eval-bp-basic
        COMMAND $<TARGET_FILE:run_traces>
        DEPENDS run_traces
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()

# evaluation: executable depending on branch_predictor_lib
if(BUILD_EVAL_PREDICTORS)
    add_executable(eval_predictors runners/eval_predictors.cpp)
    target_link_libraries(eval_predictors PRIVATE branch_predictor_lib)
    target_include_directories(eval_predictors PRIVATE ${CMAKE_CURRENT_LIST_DIR})

    add_custom_target(eval-bp
        COMMAND $<TARGET_FILE:eval_predictors>
        DEPENDS eval_predictors
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()

# regression: TAGEImproved must stay within MIN_RATIO of TAGE on sanity traces
if(BUILD_REGRESSION)
    add_executable(regression_tage runners/regression_tage.cpp)
    target_link_libraries(regression_tage PRIVATE branch_predictor_lib)
    target_include_directories(regression_tage PRIVATE ${CMAKE_CURRENT_LIST_DIR})

    add_custom_target(regression-tage
        COMMAND $<TARGET_FILE:regression_tage>
        DEPENDS regression_tage
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()
