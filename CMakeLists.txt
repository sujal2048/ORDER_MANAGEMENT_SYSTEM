cmake_minimum_required(VERSION 3.12)
project(OrderMatchingEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED COMPONENTS system thread)
find_package(nlohmann_json REQUIRED)

include_directories(include)

add_executable(order_matching_engine
    src/matching_engine.cpp
    src/server.cpp
    src/session.cpp
)

target_link_libraries(order_matching_engine
    Boost::system
    Boost::thread
    nlohmann_json::nlohmann_json
)

# If you have tests later, you can add them
# add_subdirectory(tests)
