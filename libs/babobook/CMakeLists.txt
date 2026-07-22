# --- babo_book -------------------------------------------------------------
# Your own order book + matching algorithm. Currently a stub (see src) -- this
# is the library you'll be writing.
add_library(babobook STATIC
        src/simple_order.cpp
)

target_include_directories(babobook PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(babobook PUBLIC cxx_std_20)

# Always build the minimal integration example with the library. Besides being
# executable documentation, this keeps the public API usage compile-checked.
add_executable(babo_basic_book
        ${PROJECT_SOURCE_DIR}/examples/basic_book.cpp
)
target_link_libraries(babo_basic_book PRIVATE babobook)
target_compile_features(babo_basic_book PRIVATE cxx_std_20)
