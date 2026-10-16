cmake_minimum_required(VERSION 3.16)
project(camera_control_video VERSION 0.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--disable-new-dtags")
set(CMAKE_SKIP_RPATH FALSE)

# ---------------------------------------------------------------------------
# Build metadata (written into build_version.h; printed by main.cpp at startup)
# ---------------------------------------------------------------------------
set(APP_VERSION "0.0.0")
string(TIMESTAMP APP_BUILD_TIME "%Y-%m-%d %H:%M:%S")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/build_version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/build_version.h
)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
# Pin Boost to exactly 1.71 (the version available on the target toolchain)
find_package(Boost 1.71 EXACT REQUIRED COMPONENTS system)
if(Boost_FOUND)
    message(STATUS "✅ Boost found!")
    message(STATUS "Boost Version (string): ${Boost_VERSION_STRING}")
    message(STATUS "Boost Major: ${Boost_VERSION_MAJOR}, Minor: ${Boost_VERSION_MINOR}, Patch: ${Boost_VERSION_PATCH}")
endif()

# Thread support
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Directories
# ---------------------------------------------------------------------------
set(SRC_DIR     ${CMAKE_SOURCE_DIR}/src)
set(HIKVISION_SDK_PATH ${CMAKE_SOURCE_DIR}/lib/hcsdk)

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
include_directories(
    ${Boost_INCLUDE_DIRS}
    ${HIKVISION_SDK_PATH}/include
    ${SRC_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# ---------------------------------------------------------------------------
# Sources (listed explicitly; avoids file(GLOB) silently missing files)
# ---------------------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/AeiClient.cpp
    src/CameraDevice.cpp
    src/CameraManager.cpp
    src/Config.cpp
    src/Logger.cpp
    src/Utils.cpp
)

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(${PROJECT_NAME} ${SOURCES})

# ---------------------------------------------------------------------------
# Link libraries
# ---------------------------------------------------------------------------
target_link_libraries(
    ${PROJECT_NAME}
    Boost::system       # Boost libraries
    Threads::Threads    # Link to pthreads if threading is used
)

# All Hikvision SDK shared libraries (listed explicitly to guarantee link
# order and runtime dependencies).
# NOTE: the SDK itself is NOT redistributed in this repository - download it
# from Hikvision and extract it under lib/hcsdk/ first (see README, section
# "Camera SDK - Hikvision HCNetSDK").
set(HIKVISION_LIBRARIES
    ${HIKVISION_SDK_PATH}/libAudioRender.so
    ${HIKVISION_SDK_PATH}/libcrypto.so.1.1
    ${HIKVISION_SDK_PATH}/libHCCore.so
    ${HIKVISION_SDK_PATH}/libhcnetsdk.so
    ${HIKVISION_SDK_PATH}/libhpr.so
    ${HIKVISION_SDK_PATH}/libNPQos.so
    ${HIKVISION_SDK_PATH}/libssl.so.1.1
    ${HIKVISION_SDK_PATH}/libopenal.so.1
    ${HIKVISION_SDK_PATH}/libPlayCtrl.so
    ${HIKVISION_SDK_PATH}/libSuperRender.so
    ${HIKVISION_SDK_PATH}/libz.so
    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libanalyzedata.so
    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libAudioIntercom.so
    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libHCAlarm.so
    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libHCCoreDevCfg.so
    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libHCDisplay.so
    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libHCGeneralCfgMgr.so
    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libHCIndustry.so
    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libHCPlayBack.so
    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libHCVoiceTalk.so
    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libiconv2.so
    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libHCPreview.so
    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libStreamTransClient.so
    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libSystemTransform.so
)
target_link_libraries(${PROJECT_NAME} ${HIKVISION_LIBRARIES})

# ---------------------------------------------------------------------------
# Runtime path (RPATH)
# Lets the executable find the Hikvision SDK shared libraries under
# $ORIGIN/../lib/hcsdk and $ORIGIN/../lib/hcsdk/HCNetSDKCom at runtime,
# without any LD_LIBRARY_PATH.
# ---------------------------------------------------------------------------
set_target_properties(${PROJECT_NAME} PROPERTIES
    INSTALL_RPATH "$ORIGIN/../lib/hcsdk;$ORIGIN/../lib/hcsdk/HCNetSDKCom"
    BUILD_RPATH   "$ORIGIN/../lib/hcsdk;$ORIGIN/../lib/hcsdk/HCNetSDKCom"
)
set(CMAKE_BUILD_RPATH   "$ORIGIN/../lib/hcsdk;$ORIGIN/../lib/hcsdk/HCNetSDKCom")
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib/hcsdk;$ORIGIN/../lib/hcsdk/HCNetSDKCom")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# ---------------------------------------------------------------------------
# Install layout:
#   <prefix>/bin/camera_control_video      executable
#   <prefix>/config/aeiconfig.json         configuration file
#   <prefix>/lib/hcsdk/                    Hikvision SDK (incl. HCNetSDKCom/)
#   <prefix>/logs/                         runtime logs (created at runtime)
#
#   CMAKE_INSTALL_PREFIX defaults to ${CMAKE_SOURCE_DIR}/install
#   (a sibling of build/)
# ---------------------------------------------------------------------------
set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install")

install(TARGETS   ${PROJECT_NAME}                DESTINATION bin)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/lib/hcsdk/ DESTINATION lib/hcsdk)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/config/    DESTINATION config)

# ---------------------------------------------------------------------------
# Clean targets
# ---------------------------------------------------------------------------
add_custom_target(my_clean_target
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/${PROJECT_NAME}
    COMMENT "Cleaning the project"
)
add_custom_target(clean_build
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/*
    COMMENT "Cleaning the build directory without deleting it"
)
