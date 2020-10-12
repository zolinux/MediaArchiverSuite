cmake_minimum_required(VERSION 3.10.0)

add_executable(test_sqlite
    test_sqlite.cpp
)

target_compile_definitions(test_sqlite PRIVATE NORPC)

target_include_directories(test_sqlite PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(test_sqlite PUBLIC sqlite)

add_executable(test_inotify
    test_inotify.cpp
   )

# target_compile_definitions(test_inotify PRIVATE NORPC)
target_include_directories(test_inotify PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(test_inotify PUBLIC filesystemwatcher)

add_test(tests
    test_sqlite
    test_inotify
)

