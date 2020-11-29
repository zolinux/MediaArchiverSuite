cmake_minimum_required(VERSION 3.10.0)

LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

project(MediaArchiverClient VERSION 0.0.1 LANGUAGES C CXX) 

find_package(Threads REQUIRED)
#find_package(SQLite3 REQUIRED)

enable_testing()
if(WIN32)
    add_compile_definitions( _WIN32_WINNT=0x0601)
endif()

set(CMAKE_CXX_STANDARD 14)
add_subdirectory(rpclib)
add_subdirectory(tests)
add_subdirectory(sqlite)

add_library(Loguru STATIC 
    ${CMAKE_CURRENT_LIST_DIR}/loguru/loguru.cpp
    ${CMAKE_CURRENT_LIST_DIR}/loguru/loguru.hpp
)

target_include_directories(Loguru PUBLIC ${CMAKE_CURRENT_LIST_DIR}/loguru)
target_link_libraries(Loguru PUBLIC dl)

add_library(MediaArchiverCommon OBJECT
    RpcFunctions.hpp
    ServerIf.hpp
    IMediaArchiverServer.hpp
)
set_target_properties(MediaArchiverCommon PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(MediaArchiverCommon
    INTERFACE $<INSTALL_INTERFACE:rpc>
    PUBLIC .
)
target_link_libraries(MediaArchiverCommon 
    PUBLIC rpc
    PUBLIC Loguru
    PUBLIC $<$<BOOL:${MINGW}>:ws2_32>
)

add_library(MediaArchiverClient OBJECT
    MediaArchiverClient.cpp
    MediaArchiverClient.hpp
    MediaArchiverConfig.hpp
    MediaArchiverClientConfig.hpp    
)
target_link_libraries(MediaArchiverClient 
    PRIVATE MediaArchiverCommon Threads::Threads
)

add_executable(MediaArchiverClientMain
    MediaArchiverClientMain.cpp
)

target_link_libraries(MediaArchiverClientMain
    PRIVATE MediaArchiverCommon MediaArchiverClient Threads::Threads
)

add_executable(MediaArchiverDaemon
     MediaArchiverDaemon.cpp
     MediaArchiverDaemon.hpp
     MediaArchiverDaemonConfig.hpp

     IMediaArchiverServer.hpp

     IDatabase.hpp
     
     IFileCopier.hpp
     FileCopierLinux.cpp
     FileCopierLinux.hpp
 )

add_library(filesystemwatcher OBJECT
     IFileSystemWatcher.hpp
     FileSystemWatcher.hpp
     FileSystemWatcherLinux.hpp     
     FileSystemWatcherLinux.cpp
)

target_link_libraries(filesystemwatcher 
    PUBLIC Threads::Threads
)

target_include_directories(filesystemwatcher PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

add_library(sqlite OBJECT
    IDatabase.hpp
    SQLite.hpp
    SQLite.cpp
)

target_link_libraries(sqlite 
PUBLIC Threads::Threads rpc Loguru)

target_link_libraries(sqlite 
PUBLIC ${SQLITE3_LIBRARIES}
)

target_include_directories(sqlite  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

target_link_libraries(MediaArchiverDaemon 
    PRIVATE MediaArchiverCommon Threads::Threads pthread sqlite filesystemwatcher Loguru sqlite3
)
# target_include_directories(MediaArchiverDaemon INTERFACE
#     $<BUILD_INTERFACE:rpc>
# )

