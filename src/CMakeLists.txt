add_library(engine STATIC
    engine/matching_engine.cpp
    engine/matching_engine.h
)

target_include_directories(engine PUBLIC ${CMAKE_SOURCE_DIR}/src)


