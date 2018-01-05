cmake_minimum_required(VERSION 3.9)
project(homegear_ccu2)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        src/Factory.cpp
        src/Factory.h
        src/GD.cpp
        src/GD.h
        src/Interfaces.cpp
        src/Interfaces.h
        src/MyCentral.cpp
        src/MyCentral.h
        src/MyFamily.cpp
        src/MyFamily.h
        src/MyPacket.cpp
        src/MyPacket.h
        src/MyPeer.cpp
        src/MyPeer.h src/PhysicalInterfaces/Ccu2.cpp src/PhysicalInterfaces/Ccu2.h)

add_custom_target(homegear COMMAND ../../makeAll.sh SOURCES ${SOURCE_FILES})

add_library(homegear_ccu2 ${SOURCE_FILES})