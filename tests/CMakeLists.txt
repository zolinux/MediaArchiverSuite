add_executable(test_sqlite
    test_sqlite.cpp
)

target_compile_definitions(test_sqlite PRIVATE NORPC)

target_include_directories(test_sqlite PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(test_sqlite PUBLIC sqlite Threads::Threads)

add_executable(test_inotify
    test_inotify.cpp
    ../ServerIf.hpp
    ../ServerFactory.cpp
   )

# target_compile_definitions(test_inotify PRIVATE NORPC)
target_include_directories(test_inotify PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(test_inotify PUBLIC filesystemwatcher Loguru)

add_executable(test_daemon
    test_daemon.cpp
    ../FileCopierLinux.cpp
    ../ServerIf.hpp
    ../ServerFactory.cpp
   )

target_include_directories(test_daemon PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(test_daemon PUBLIC rpc MediaArchiverCommon Threads::Threads)

add_executable(test_client
    test_client.cpp
    ../ServerIf.hpp
    ../ServerFactory.cpp
    )

target_include_directories(test_client PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(test_client PUBLIC MediaArchiverCommon MediaArchiverClient Threads::Threads)

add_executable(test_client_offline
    test_client_offline.cpp
    ServerMock.hpp
   )

target_include_directories(test_client_offline PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(test_client_offline PUBLIC MediaArchiverCommon MediaArchiverClient Threads::Threads stdc++fs)

add_executable(test_hello
    test_hello.cpp
    )
   
# Tests shall be run from the build folder
add_test(tests
    test_sqlite
    test_inotify
    test_daemon
    test_client
    test_hello
)
