cmake_minimum_required(VERSION 3.16)
project(os_ex5)

set(CMAKE_CXX_STANDARD 11)

add_executable(os_ex5
        main.cpp
        server.h
        server.cpp
        client.h
        client.cpp
        globals.h)
