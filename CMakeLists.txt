cmake_minimum_required(VERSION 3.22)
project(VVTierOverride LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Preloader SDK (same pinned tag the Levi docs use). If you already have a
# checkout, add -DFETCHCONTENT_SOURCE_DIR_PRELOADER_ANDROID=/path/to/it
include(FetchContent)
FetchContent_Declare(
    preloader_android
    GIT_REPOSITORY https://github.com/LiteLDev/preloader-android.git
    GIT_TAG 0.2.2)
FetchContent_MakeAvailable(preloader_android)

add_library(VVTierOverride SHARED src/VVTierOverride.cpp)
target_link_libraries(VVTierOverride PRIVATE preloader android log)
target_compile_options(VVTierOverride PRIVATE -fvisibility=hidden -Wall -Wextra)
