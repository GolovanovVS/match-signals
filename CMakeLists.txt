cmake_minimum_required(VERSION 3.16)
project(match_signals VERSION 0.1.0 LANGUAGES CXX)

add_library(match_signals
    src/amount.cpp
    src/sports.cpp
    src/market.cpp
    src/portfolio.cpp
    src/models.cpp
    src/engine.cpp
)
target_compile_features(match_signals PUBLIC cxx_std_17)
target_include_directories(match_signals PUBLIC include)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(match_signals PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(CURL REQUIRED)
find_package(LibXml2 REQUIRED)
add_library(match_signals_feeds
    src/curl.cpp
    src/sports_xml.cpp
    src/sports_xml_parse.cpp
    src/sports_feed.cpp
    src/order_book.cpp
    src/market_data.cpp
)
target_link_libraries(match_signals_feeds PUBLIC match_signals PRIVATE CURL::libcurl LibXml2::LibXml2)
target_include_directories(match_signals_feeds SYSTEM PRIVATE third_party)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(match_signals_feeds PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(pipeline_example examples/pipeline.cpp)
target_link_libraries(pipeline_example PRIVATE match_signals_feeds)

include(CTest)
if(BUILD_TESTING)
    foreach(name amount engine portfolio)
        add_executable(${name}_test tests/${name}_test.cpp)
        target_link_libraries(${name}_test PRIVATE match_signals)
        add_test(NAME ${name} COMMAND ${name}_test)
    endforeach()
    foreach(name sports_xml market_data)
        add_executable(${name}_test tests/${name}_test.cpp)
        target_link_libraries(${name}_test PRIVATE match_signals_feeds)
        add_test(NAME ${name} COMMAND ${name}_test)
    endforeach()
    add_test(NAME pipeline COMMAND pipeline_example)
    set_tests_properties(pipeline PROPERTIES PASS_REGULAR_EXPRESSION "Filled: cash=97 position=5")
endif()
