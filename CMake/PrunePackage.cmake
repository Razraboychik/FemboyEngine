# CPack pre-build step.
#
# The engine bundles box2d and enet via add_subdirectory(), and both register
# their own install() rules. Those headers and static libraries are build-time
# dependencies of the engine, not files a player needs. Staging dirs are
# temporary, so pruning here is safe and keeps the shipped archive honest:
# executable, assets, runtime DLLs, docs.

if(DEFINED CPACK_TEMPORARY_DIRECTORY)
    foreach(_dir include lib)
        if(EXISTS "${CPACK_TEMPORARY_DIRECTORY}/${_dir}")
            file(REMOVE_RECURSE "${CPACK_TEMPORARY_DIRECTORY}/${_dir}")
        endif()
    endforeach()
endif()

