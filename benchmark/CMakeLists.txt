
find_package(Threads REQUIRED)
FetchContent_Declare(SPSCQueue
        GIT_REPOSITORY https://github.com/rigtorp/SPSCQueue.git
        GIT_TAG master)
FetchContent_MakeAvailable(SPSCQueue)


add_subdirectory(adapters)




# Binary  data generator
add_executable(generator workload/generator.cpp)
target_include_directories(generator PRIVATE api)
# Deterministic workload: pin bit-stable FP (no FMA contraction, no LTO) so
# aggressive Release flags cannot change the generated stream and invalidate the
# correctness references. Helper defined in cmake/CompilerSettings.cmake.
target_deterministic_fp(generator)



#Harness - actual measurement
add_executable(harness
        src/harness.cpp
        src/transport.cpp
        src/correctness.cpp
        src/platform.cpp
        src/audit.cpp
        third_party/sha256.c
        third_party/RigtorpSPSCQueueWrapper.h
)

target_include_directories(harness PRIVATE api third_party)
target_link_libraries(harness PRIVATE SPSCQueue::SPSCQueue Threads::Threads ${CMAKE_DL_LIBS})
if (WIN32)
    target_link_libraries(harness PRIVATE advapi32)   # platform.cpp RegGetValueA
endif()

# The harness resolves reference/correctness_hash.txt relative to the CURRENT
# WORKING DIRECTORY, not the source tree — so stage reference/ next to
# harness.exe on every build. Run the harness from its own output dir and it
# finds the hashes (PASS/FAIL instead of NO REFERENCE).
add_custom_command(TARGET harness POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/reference
                $<TARGET_FILE_DIR:harness>/reference
        COMMENT "Staging reference/ next to harness.exe")

# The harness dlopens the baseline adapter BY NAME from its own directory
# (--baseline and the audit look for ./liquibook_adapter.<dll|so>), so stage the
# adapter dll next to harness.exe. The harness does not LINK the adapter (it
# loads it at runtime), so there is no implicit build order — add_dependencies
# forces the adapter to build first, before this copy runs.
add_dependencies(harness liquibook_adapter)
add_custom_command(TARGET harness POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:liquibook_adapter> $<TARGET_FILE_DIR:harness>
        COMMENT "Staging liquibook_adapter next to harness.exe")

