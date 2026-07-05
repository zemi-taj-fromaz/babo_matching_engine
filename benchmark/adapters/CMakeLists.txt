add_library(babobook_adapter SHARED babobook_adapter.cpp)
target_link_libraries(babobook_adapter PRIVATE babobook)
set_target_properties(babobook_adapter PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        PREFIX ""

add_library(liquibook_adapter SHARED liquibook_adapter.cpp)
target_link_libraries(liquibook_adapter PRIVATE liquibook_ref)
set_target_properties(liquibook_adapter PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        PREFIX "")   # no "lib" prefix, so --baseline finds liquibook_adapter.{dll,so}