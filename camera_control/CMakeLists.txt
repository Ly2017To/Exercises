cmake_minimum_required(VERSION 3.16)

# Set project name and version
project(camera_control VERSION 1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--disable-new-dtags")
set(CMAKE_SKIP_RPATH FALSE)

# Find Boost (specifically the system component)
find_package(Boost REQUIRED COMPONENTS system)

# Optionally, find Threads (for pthread support)
find_package(Threads REQUIRED)

# Set directories
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

# Set Hikvision SDK path (adjust the path based on your system)
set(HIKVISION_SDK_PATH ${CMAKE_SOURCE_DIR}/lib/hcsdk)

# Include directories for Boost, Hikvision SDK, and custom includes
include_directories(
    ${Boost_INCLUDE_DIRS}
    ${HIKVISION_SDK_PATH}/include
    ${INCLUDE_DIR}
)

# Set source files
file(GLOB SOURCES ${SRC_DIR}/*.cpp)

# Add executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries
target_link_libraries(
	${PROJECT_NAME}
    ${Boost_LIBRARIES}    # Boost libraries
	Threads::Threads        # Link to pthreads if threading is used
)

# Group all Hikvision SDK libraries
# set(HIKVISION_LIBRARIES
#    ${HIKVISION_SDK_PATH}/libAudioRender.so
#    ${HIKVISION_SDK_PATH}/libcrypto.so.1.1
#    ${HIKVISION_SDK_PATH}/libHCCore.so
#    ${HIKVISION_SDK_PATH}/libhcnetsdk.so
#    ${HIKVISION_SDK_PATH}/libhpr.so
#    ${HIKVISION_SDK_PATH}/libNPQos.so
#    ${HIKVISION_SDK_PATH}/libssl.so.1.1
#    ${HIKVISION_SDK_PATH}/libopenal.so.1
#    ${HIKVISION_SDK_PATH}/libPlayCtrl.so
#    ${HIKVISION_SDK_PATH}/libSuperRender.so
#    ${HIKVISION_SDK_PATH}/libz.so
#    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libanalyzedata.so
#    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libAudioIntercom.so
#    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libHCAlarm.so
#    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libHCCoreDevCfg.so
#    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libHCDisplay.so
#    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libHCGeneralCfgMgr.so
#    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libHCIndustry.so
#    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libHCPlayBack.so
#    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libHCVoiceTalk.so
#    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libiconv2.so
#    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libHCPreview.so
#    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libStreamTransClient.so
#    ${HIKVISION_SDK_PATH}/HCNetSDKCom/libSystemTransform.so
#)

# Find all .so files in the SDK directory
file(GLOB HIKVISION_LIBRARIES 
    ${HIKVISION_SDK_PATH}/lib*.so
    ${HIKVISION_SDK_PATH}/HCNetSDKCom/lib*.so
)

# Link Hikvision SDK libraries
target_link_libraries(${PROJECT_NAME} ${HIKVISION_LIBRARIES})

# Set the runtime path for the executable
set_target_properties(${PROJECT_NAME} PROPERTIES
    INSTALL_RPATH "$ORIGIN/../lib/hcsdk;$ORIGIN/../lib/hcsdk/HCNetSDKCom"
    BUILD_RPATH "$ORIGIN/../lib/hcsdk;$ORIGIN/../lib/hcsdk/HCNetSDKCom"
)

# Enable verbose linking to debug
set(CMAKE_VERBOSE_MAKEFILE ON)

# Set runtime paths to look in the correct directories
set(CMAKE_BUILD_RPATH "$ORIGIN/../lib/hcsdk;$ORIGIN/../lib/hcsdk/HCNetSDKCom")
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib/hcsdk;$ORIGIN/../lib/hcsdk/HCNetSDKCom")

# Allow CMake to add the link paths automatically to rpath
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Set installation prefix (where everything will be installed)
set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install")

# Install the target with the proper runtime path for dependencies
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)

# Install the libraries to the correct location
install(DIRECTORY ${CMAKE_SOURCE_DIR}/lib/hcsdk/ DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/hcsdk)

# Install the configuration folder to the correct location
install(DIRECTORY ${CMAKE_SOURCE_DIR}/configuration/ DESTINATION ${CMAKE_INSTALL_PREFIX}/configuration)

# Clean Targets
add_custom_target(my_clean_target
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/${PROJECT_NAME}
    COMMENT "Cleaning the project"
)

add_custom_target(clean_build
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/*
    COMMENT "Cleaning the build directory without deleting it"
)
