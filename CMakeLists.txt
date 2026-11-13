cmake_minimum_required(VERSION 3.16)
set(PASSPORT_APP "manager" CACHE STRING "Passport target: manager, claw, or pomodoro")
set_property(CACHE PASSPORT_APP PROPERTY STRINGS manager claw pomodoro)

# List components explicitly because ESP-IDF also discovers children of main/ by default.
set(COMPONENT_DIRS
    "$ENV{IDF_PATH}/components"
    "${CMAKE_CURRENT_LIST_DIR}/components"
    "${CMAKE_CURRENT_LIST_DIR}/main/common"
)

if(PASSPORT_APP STREQUAL "manager")
    set(target_dir main/runner)
    set(target_component runner)
    set(passport_project FoloToy-AI-Passport)
elseif(PASSPORT_APP STREQUAL "claw")
    set(target_dir main/apps/esp-claw)
    set(target_component esp-claw)
    set(passport_project Passport-ESP-Claw)
    list(APPEND COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/third_party/esp-claw/components")
elseif(PASSPORT_APP STREQUAL "pomodoro")
    set(target_dir main/apps/pomodoro)
    set(target_component pomodoro)
    set(passport_project Passport-Pomodoro)
else()
    message(FATAL_ERROR "Unknown PASSPORT_APP: ${PASSPORT_APP}")
endif()

set(COMPONENTS ${target_component})
list(APPEND COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/${target_dir}")
if(NOT PASSPORT_APP STREQUAL "pomodoro")
    list(APPEND COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/main/common/network")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

if(PASSPORT_APP STREQUAL "manager")
    if(NOT SDKCONFIG_DEFAULTS)
        set(SDKCONFIG_DEFAULTS "$ENV{SDKCONFIG_DEFAULTS}")
    endif()
    if(NOT SDKCONFIG_DEFAULTS)
        set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
    endif()
    list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/main/runner/sdkconfig.defaults")
else()
    idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/${target_dir}/dependencies.lock")
endif()

project(${passport_project})

if(PASSPORT_APP STREQUAL "manager")
    set(empty_store "${CMAKE_BINARY_DIR}/empty-app-store")
    file(MAKE_DIRECTORY "${empty_store}")
    littlefs_create_partition_image(app_store "${empty_store}" FLASH_IN_PROJECT)
endif()
