# matching_engine_api.h adapters. Depth ON vs OFF is a compile-time toggle, so
# each engine ships two adapters from the SAME source:
#   <engine>_adapter        -> depth OFF  (lean; the fair head-to-head baseline)
#   <engine>_depth_adapter  -> depth ON   (stock: maintains an aggregate depth)
# babo toggles on BABO_NO_DEPTH; liquibook on LIQUI_NO_DEPTH (liqui_book_type.h).
# All export the C ABI via WINDOWS_EXPORT_ALL_SYMBOLS and drop the "lib" prefix so
# the harness's --baseline / engine-under-test flags find <name>.{dll,so}.

# helper: one SHARED adapter target with the shared property set + an optional
# depth macro.
function(add_adapter target source engine_lib)
    add_library(${target} SHARED ${source})
    target_link_libraries(${target} PRIVATE ${engine_lib})
    set_target_properties(${target} PROPERTIES
            WINDOWS_EXPORT_ALL_SYMBOLS ON
            PREFIX "")
    if(ARGN)
        target_compile_definitions(${target} PRIVATE ${ARGN})
    endif()
endfunction()

# --- babo -------------------------------------------------------------------
add_adapter(babobook_adapter       babobook_adapter.cpp  babobook  BABO_NO_DEPTH)  # depth OFF
add_adapter(babobook_depth_adapter babobook_adapter.cpp  babobook)                 # depth ON

# --- liquibook --------------------------------------------------------------
add_adapter(liquibook_adapter       liquibook_adapter.cpp  liquibook_ref  LIQUI_NO_DEPTH)  # depth OFF
add_adapter(liquibook_depth_adapter liquibook_adapter.cpp  liquibook_ref)                  # depth ON
