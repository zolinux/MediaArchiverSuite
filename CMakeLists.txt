cmake_minimum_required(VERSION 3.10.0)

LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

project(MediaArchiverClient VERSION 0.0.1 LANGUAGES C CXX) 

find_package(Threads REQUIRED)
find_package(SQLite3 QUIET)

set(THREADS_PREFER_PTHREAD_FLAG ON)

enable_testing()
if(WIN32)
    add_compile_definitions( _WIN32_WINNT=0x0601)
    # find_package(dlfcn-win32 REQUIRED)
    set(CMAKE_DL_LIBS )
    # set(CMAKE_DL_LIBS dlfcn-win32::dl)
    add_compile_options( -mwindows)
elseif(LINUX)
    set(CMAKE_DL_LIBS dl)
endif()

set(CMAKE_CXX_STANDARD 14)
add_subdirectory(rpclib)
add_subdirectory(tests)

# FixMe:
# on the router there is the sqlite3.so but no header files, therefore it
# will be compiled but the .so ignored
if(NOT $<BOOL:${SQLITE3_INCLUDE_DIR}>)
    add_subdirectory(sqlite)
endif()

add_library(Loguru SHARED 
    ${CMAKE_CURRENT_LIST_DIR}/loguru/loguru.cpp
    ${CMAKE_CURRENT_LIST_DIR}/loguru/loguru.hpp
)
target_compile_definitions(Loguru PRIVATE LOGURU_STACKTRACES=0)
target_include_directories(Loguru PUBLIC ${CMAKE_CURRENT_LIST_DIR}/loguru)
target_link_libraries(Loguru PUBLIC ${CMAKE_DL_LIBS})

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
PUBLIC ${SQLITE3_LIBRARIES} Threads::Threads rpc Loguru)

target_include_directories(sqlite  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

target_link_libraries(MediaArchiverDaemon 
    PRIVATE MediaArchiverCommon Threads::Threads pthread sqlite filesystemwatcher Loguru sqlite3
)
# target_include_directories(MediaArchiverDaemon INTERFACE
#     $<BUILD_INTERFACE:rpc>
# )

