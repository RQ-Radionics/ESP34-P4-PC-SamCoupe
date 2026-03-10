cmake_minimum_required(VERSION 3.16)

# Target must be set before including project.cmake — without this IDF
# defaults to esp32 (xtensa) on a clean clone, ignoring sdkconfig.defaults.
set(IDF_TARGET esp32p4)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(simcoupe-esp32)
