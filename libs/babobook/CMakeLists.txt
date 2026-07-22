# --- babobook ---------------------------------------------------------------
# Header-only matching engine. Consumers get the public headers and C++20
# requirement by linking this target; there is no babobook binary to ship.
add_library(babobook INTERFACE)
add_library(babobook::babobook ALIAS babobook)

target_include_directories(babobook INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(babobook INTERFACE cxx_std_20)

# The example is part of the normal build so public API usage stays
# compile-checked for every contributor.
add_subdirectory(examples)
