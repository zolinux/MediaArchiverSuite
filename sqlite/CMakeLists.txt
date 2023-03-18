add_library(sqlite3 SHARED
    sqlite3.c
)

target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_LIST_DIR})
set(SQLITE3_LIBRARY "sqlite3" CACHE STRING "" FORCE)
set(SQLITE3_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR} CACHE PATH "" FORCE)
