cmake_minimum_required(VERSION 3.22.1)
project(amy_hello_client LANGUAGES CXX)

add_library(amy_hello_client SHARED amy_hello_client.cpp)

target_compile_features(amy_hello_client PRIVATE cxx_std_17)
target_compile_options(amy_hello_client PRIVATE -Wall -Wextra -Werror)
target_link_libraries(amy_hello_client PRIVATE log)
