cmake_minimum_required(VERSION 3.16)
project(cage_oracle LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    find_package(SQLite3 REQUIRED)
    if(NOT TARGET SQLite3::SQLite3)
        if(NOT SQLite3_LIBRARIES)
            set(SQLite3_LIBRARIES "${SQLite3_LIBRARY}")
        endif()
        if(NOT SQLite3_INCLUDE_DIRS)
            set(SQLite3_INCLUDE_DIRS "${SQLite3_INCLUDE_DIR}")
        endif()
        add_library(SQLite3::SQLite3 UNKNOWN IMPORTED)
        set_target_properties(SQLite3::SQLite3 PROPERTIES
            IMPORTED_LOCATION "${SQLite3_LIBRARIES}"
            INTERFACE_INCLUDE_DIRECTORIES "${SQLite3_INCLUDE_DIRS}")
    endif()
else()
    find_library(UFC_SQLITE3_LIBRARY NAMES sqlite3 REQUIRED)
    find_path(UFC_SQLITE3_INCLUDE_DIR NAMES sqlite3.h REQUIRED)
    add_library(SQLite3::SQLite3 UNKNOWN IMPORTED GLOBAL)
    set_target_properties(SQLite3::SQLite3 PROPERTIES
        IMPORTED_LOCATION "${UFC_SQLITE3_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${UFC_SQLITE3_INCLUDE_DIR}")
endif()

set(UFC_DB_SOURCES
    src/Fighter.cpp
    src/FighterCareerStats.cpp
    src/FighterArchetype.cpp
    src/FighterMomentum.cpp
    src/FighterResume.cpp
    src/FighterAttributes.cpp
    src/PrefightCareer.cpp
    src/SimilaritySearch.cpp
    src/Fight.cpp
    src/RoundStats.cpp
    src/Matchup.cpp
    src/UfcDatabase.cpp
    src/JsonSerialize.cpp
    src/ArchetypeMatchupHistory.cpp
    src/ScoreBreakdown.cpp
    src/WinProbability.cpp
    src/MatchupAnalysis.cpp
    src/ufc_db.cpp
)

add_library(ufc_db_static STATIC ${UFC_DB_SOURCES})
target_include_directories(ufc_db_static PUBLIC include)
target_link_libraries(ufc_db_static PUBLIC SQLite3::SQLite3)

add_library(ufc_db SHARED ${UFC_DB_SOURCES})
target_include_directories(ufc_db PUBLIC include)
target_link_libraries(ufc_db PUBLIC SQLite3::SQLite3)
target_compile_definitions(ufc_db PRIVATE UFC_DB_BUILDING)
set_target_properties(ufc_db PROPERTIES
    OUTPUT_NAME ufc_db
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
)

if(NOT WIN32)
    target_compile_options(ufc_db PRIVATE -fvisibility=hidden)
    target_compile_options(ufc_db PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fvisibility-inlines-hidden>")
endif()

add_executable(ufc_server src/ufc_server.cpp)
target_include_directories(ufc_server PRIVATE include third_party)
target_link_libraries(ufc_server PRIVATE ufc_db_static)
set_target_properties(ufc_server PROPERTIES OUTPUT_NAME ufc_server_app)
if(WIN32)
    target_link_libraries(ufc_server PRIVATE ws2_32)
endif()
