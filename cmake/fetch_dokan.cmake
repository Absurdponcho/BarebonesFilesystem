macro(fetch_dokan)
    if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/FilesystemTest/ThirdParty/Dokany/lib/dokan2.lib)
        message(STATUS "Downloading dokan2 libraries...")
        file(DOWNLOAD https://github.com/dokan-dev/dokany/releases/download/v2.2.0.1000/dokan.zip ${CMAKE_CURRENT_LIST_DIR}/FilesystemTest/ThirdParty/Dokany/lib/dokan.zip)
        file(ARCHIVE_EXTRACT INPUT ${CMAKE_CURRENT_LIST_DIR}/FilesystemTest/ThirdParty/Dokany/lib/dokan.zip PATTERNS x64/Release/dokan*2.lib DESTINATION ${CMAKE_CURRENT_LIST_DIR}/FilesystemTest/ThirdParty/Dokany/lib/)
        file(REMOVE ${CMAKE_CURRENT_LIST_DIR}/FilesystemTest/ThirdParty/Dokany/lib/dokan.zip)
        file(GLOB dokan_libs RELATIVE ${CMAKE_CURRENT_LIST_DIR}/FilesystemTest/ThirdParty/Dokany/lib/x64/Release/ ${CMAKE_CURRENT_LIST_DIR}/FilesystemTest/ThirdParty/Dokany/lib/x64/Release/*)
        foreach(lib ${dokan_libs})
            file(RENAME ${CMAKE_CURRENT_LIST_DIR}/FilesystemTest/ThirdParty/Dokany/lib/x64/Release/${lib} ${CMAKE_CURRENT_LIST_DIR}/FilesystemTest/ThirdParty/Dokany/lib/${lib})
        endforeach()
        file(REMOVE_RECURSE ${CMAKE_CURRENT_LIST_DIR}/FilesystemTest/ThirdParty/Dokany/lib/x64)
    endif()
   
    set(DOKAN_LIB ${CMAKE_CURRENT_LIST_DIR}/FilesystemTest/ThirdParty/Dokany/lib/dokan2.lib)

    if(NOT EXISTS C:/Windows/System32/dokan2.dll)
        message(WARNING "The Dokan driver does not appear to be installed!  Unless you know otherwise, please run choco install dokany2.")
    endif()
endmacro()