# --- babo_book -------------------------------------------------------------
# Your own order book + matching algorithm. Currently a stub (see src) -- this
# is the library you'll be writing.
add_library(babo_book STATIC
        src/simple_order.cpp
        include/book/types.h
        include/book/comparable_price.h
        include/book/callback.h
        include/book/order.hpp
        include/book/order_tracker.h
        include/book/order_listener.h
        include/book/order_book.h
        include/book/order_book_listener.h
        include/book/trade_listener.h
        include/book/logger.h
        include/book/depth_constants.h
        include/book/depth_level.h
        include/book/depth.h
        include/book/depth_listener.h
        include/book/depth_order_book.h
        include/book/bbo_listener.h
        include/simple/simple_order.h
        include/simple/simple_order_book.h
        include/book/fastmap.h
)

target_include_directories(babo_book PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(babo_book PUBLIC cxx_std_20)
