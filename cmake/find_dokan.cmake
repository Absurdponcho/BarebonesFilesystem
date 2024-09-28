MACRO(ls result curdir)
  FILE(GLOB children ${curdir}/*)
  SET(dirlist "")
  FOREACH(child ${children})
    IF(IS_DIRECTORY ${child})
      LIST(APPEND dirlist ${child})
    ENDIF()
  ENDFOREACH()
  SET(${result} ${dirlist})
ENDMACRO()

macro(find_dokan)
    if(DEFINED DOKAN_LIB)
        if(NOT EXISTS ${DOKAN_LIB})
            message(FATAL_ERROR "User-specified Dokan lib does not exist: ${DOKAN_LIB}")
        endif()
    else()
        message(STATUS "Looking for Dokan libs")
        ls(dokan_folders "C:/Program Files/Dokan")
        list(LENGTH dokan_folders num_results)
        if(num_results EQUAL 0)
            message(WARNING "No Dokan installation auto detected, unless the libs are in your PATH this will fail")
            find_library(
                DOKAN_LIB
                dokan2 REQUIRED
            )
        else()
            list(GET dokan_folders -1 dokan_hint_folder)
            if(${num_results} GREATER 1)
                message(WARNING "Multiple Dokan installations found, using the one that sorted last: ${dokan_hint_folder}")
                message("Set the DOKAN_LIB variable to the path to dokan2.lib in order to silence this warning")
            else(
                message(STATUS "Found Dokan lib at ${dokan_hint_folder}")
            )
            endif()

            find_library(
                DOKAN_LIB
                dokan2 REQUIRED
                HINTS ${dokan_hint_folder}
            )
        endif()
    endif()
endmacro()