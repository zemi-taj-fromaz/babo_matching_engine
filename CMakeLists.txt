cmake_minimum_required(VERSION 3.23)
project(BaboMatchingEngine)

add_executable(babo_matching_engine)
target_sources(babo_matching_engine
        PRIVATE
            src/main.cpp
)
