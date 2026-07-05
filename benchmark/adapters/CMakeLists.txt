#add_library(babobook_shared SHARED babobook_adapter.cpp)
#target_link_libraries(babobook_shared PRIVATE babobook)

add_library(liquibook_shared SHARED liquibook_adapter.cpp)
target_link_libraries(liquibook_shared PRIVATE liquibook_ref)
# On Windows, extern "C" fixes the symbol NAME but does not EXPORT it from the
# DLL — GetProcAddress would find nothing. This auto-generates the export table
# for all functions (the engine_* ABI), so the adapter loads without adding
# __declspec(dllexport) to source written for Linux. No-op on Linux/.so.
set_target_properties(liquibook_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)