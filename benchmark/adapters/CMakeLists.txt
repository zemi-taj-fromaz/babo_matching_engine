# Canonical matching_engine_api.h adapters. Pull-based depth is part of the
# default babobook configuration; no duplicate no-depth adapter matrix is built.
# All adapters export the C ABI via WINDOWS_EXPORT_ALL_SYMBOLS and drop the "lib"
# prefix so the harness finds <name>.{dll,so}.

# helper: one SHARED adapter target with the shared property set and optional
# compile definitions.
function(add_adapter target source engine_target)
    add_library(${target} SHARED ${source})
    target_link_libraries(${target} PRIVATE ${engine_target})
    set_target_properties(${target} PROPERTIES
            WINDOWS_EXPORT_ALL_SYMBOLS ON
            PREFIX "")
    if(ARGN)
        target_compile_definitions(${target} PRIVATE ${ARGN})
    endif()
endfunction()

add_adapter(babobook_adapter   babobook_adapter.cpp   babobook::babobook)
add_adapter(liquibook_adapter  liquibook_adapter.cpp  liquibook_ref)
