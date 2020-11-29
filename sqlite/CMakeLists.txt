add_library(sqlite3 SHARED
    sqlite3.c
)

target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_LIST_DIR})
