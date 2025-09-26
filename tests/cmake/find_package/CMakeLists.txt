cmake_minimum_required(VERSION 3.22)

project(orpheus_find_package_smoke LANGUAGES CXX)

find_package(OrpheusSDK REQUIRED CONFIG)

add_executable(orpheus_find_package_smoke main.cpp)

target_link_libraries(orpheus_find_package_smoke PRIVATE Orpheus::orpheus_core)
