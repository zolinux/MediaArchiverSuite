add_executable(test_sqlite
    test_sqlite.cpp
)

target_compile_definitions(test_sqlite PRIVATE NORPC)

target_include_directories(test_sqlite PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(test_sqlite PUBLIC sqlite Threads::Threads)

add_executable(test_inotify
    test_inotify.cpp
   )

# target_compile_definitions(test_inotify PRIVATE NORPC)
target_include_directories(test_inotify PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(test_inotify PUBLIC filesystemwatcher)

add_executable(test_daemon
    test_daemon.cpp
   )

target_include_directories(test_daemon PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(test_daemon PUBLIC rpc MediaArchiverCommon Threads::Threads)

add_executable(test_client
    test_client.cpp
   )

target_include_directories(test_client PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(test_client PUBLIC rpc MediaArchiverCommon MediaArchiverClient Threads::Threads)

add_executable(test_hello
    test_hello.cpp
    )
   
add_test(tests
    test_sqlite
    test_inotify
    test_daemon
    test_client
    test_hello
)

