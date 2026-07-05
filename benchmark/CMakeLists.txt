
find_package(Threads REQUIRED)
FetchContent_Declare(SPSCQueue
        GIT_REPOSITORY https://github.com/rigtorp/SPSCQueue.git
        GIT_TAG master)
FetchContent_MakeAvailable(SPSCQueue)


add_subdirectory(adapters)




# Binary  data generator
add_executable(generator workload/generator.cpp)
target_include_directories(generator PRIVATE api)



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
target_link_libraries(harness PRIVATE SPSCQueue::SPSCQueue ${CMAKE_DL_LIBS})

