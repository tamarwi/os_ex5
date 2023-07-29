cmake_minimum_required(VERSION 3.21)
project(os_ex5)

set(CMAKE_CXX_STANDARD 11)

add_executable(os_ex5
        main.cpp
        server.h
        client.h
        globals.h)
