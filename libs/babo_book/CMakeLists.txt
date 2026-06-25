# --- babo_book -------------------------------------------------------------
# Your own order book + matching algorithm. Currently a stub (see src) -- this
# is the library you'll be writing.
add_library(babo_book STATIC
        src/order_book.cpp
)

target_include_directories(babo_book PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(babo_book PUBLIC cxx_std_20)
