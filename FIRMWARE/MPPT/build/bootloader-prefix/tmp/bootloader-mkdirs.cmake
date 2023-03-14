# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "D:/HUST/Embedded/MandeviceSLAB/ESP32-IDF/Espressif/frameworks/esp-idf-v4.4.3/components/bootloader/subproject"
  "D:/HUST/Embedded/MandeviceSLAB/ESP32-IDF/SUN SOUND IDF/build/bootloader"
  "D:/HUST/Embedded/MandeviceSLAB/ESP32-IDF/SUN SOUND IDF/build/bootloader-prefix"
  "D:/HUST/Embedded/MandeviceSLAB/ESP32-IDF/SUN SOUND IDF/build/bootloader-prefix/tmp"
  "D:/HUST/Embedded/MandeviceSLAB/ESP32-IDF/SUN SOUND IDF/build/bootloader-prefix/src/bootloader-stamp"
  "D:/HUST/Embedded/MandeviceSLAB/ESP32-IDF/SUN SOUND IDF/build/bootloader-prefix/src"
  "D:/HUST/Embedded/MandeviceSLAB/ESP32-IDF/SUN SOUND IDF/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/HUST/Embedded/MandeviceSLAB/ESP32-IDF/SUN SOUND IDF/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
