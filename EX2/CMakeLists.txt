cmake_minimum_required(VERSION 3.28)
project(os_ex1a)

set(CMAKE_CXX_STANDARD 11)

include_directories(.)

add_executable(os_ex1a
        demo_itimer.c
        demo_jmp.c
        demo_singInt_handler.c
        Thread.h
        uthreads.cpp
        uthreads.h
        extra_tests.cpp)
