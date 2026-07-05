#add_library(babobook_shared SHARED babobook_adapter.cpp)
#target_link_libraries(babobook_shared PRIVATE babobook)

add_library(liquibook_shared SHARED liquibook_adapter.cpp)
target_link_libraries(liquibook_shared PRIVATE liquibook_ref)