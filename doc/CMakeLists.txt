option(ENABLE_DOC "Enable documentation generation with Doxygen" OFF)

if(ENABLE_DOC)
	find_package(Doxygen REQUIRED)

	function(download_doxygen_tag name url)
		set(tag_file "${CMAKE_CURRENT_BINARY_DIR}/${name}.tag")
		file(DOWNLOAD
			"${url}"
			"${tag_file}"
			STATUS download_status
		)
		list(GET download_status 0 download_error)
		if(NOT download_error EQUAL 0)
			message(FATAL_ERROR "Could not fetch ${name}.tag: ${download_status}")
		endif()
		set(${name}_TAG_FILE "${tag_file}" PARENT_SCOPE)
	endfunction()

	download_doxygen_tag(StormByte "https://dev.stormbyte.org/StormByte/StormByte.tag")
	download_doxygen_tag(StormByte-Buffer "https://dev.stormbyte.org/StormByte-Buffer/StormByte-Buffer.tag")
	download_doxygen_tag(StormByte-Logger "https://dev.stormbyte.org/StormByte-Logger/StormByte-Logger.tag")
	download_doxygen_tag(StormByte-String "https://dev.stormbyte.org/StormByte-String/StormByte-String.tag")
	download_doxygen_tag(StormByte-System "https://dev.stormbyte.org/StormByte-System/StormByte-System.tag")

	set(DOXYGEN_PROJECT_NAME "StormByte-Network")
	set(DOXYGEN_PROJECT_NUMBER "${PROJECT_VERSION}")
	set(DOXYGEN_PROJECT_BRIEF "C++26 networking module of the StormByte suite")
	set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/docs")
	set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${CMAKE_SOURCE_DIR}/README.md")

	set(DOXYGEN_RECURSIVE YES)
	set(DOXYGEN_FILE_PATTERNS "*.hxx" "*.h" "*.txx" "*.md")
	set(DOXYGEN_EXCLUDE_PATTERNS
		"*.cxx"
		"*/thirdparty/*"
	)

	set(DOXYGEN_INCLUDE_PATH
		"${CMAKE_SOURCE_DIR}/lib/public"
		"${CMAKE_SOURCE_DIR}/thirdparty/StormByte/buffer/src/lib/public"
		"${CMAKE_SOURCE_DIR}/thirdparty/StormByte/buffer/src/thirdparty/StormByte/logger/src/lib/public"
		"${CMAKE_SOURCE_DIR}/thirdparty/StormByte/buffer/src/thirdparty/StormByte/system/src/lib/public"
		"${CMAKE_SOURCE_DIR}/thirdparty/StormByte/buffer/src/thirdparty/StormByte/system/src/thirdparty/StormByte/string/src/lib/public"
		"${CMAKE_SOURCE_DIR}/thirdparty/StormByte/buffer/src/thirdparty/StormByte/system/src/thirdparty/StormByte/string/src/thirdparty/StormByte/base/src/lib/public"
	)
	set(DOXYGEN_STRIP_FROM_PATH "${CMAKE_SOURCE_DIR}")
	set(DOXYGEN_STRIP_FROM_INC_PATH "${CMAKE_SOURCE_DIR}/lib/public")

	set(DOXYGEN_ENABLE_PREPROCESSING YES)
	set(DOXYGEN_MACRO_EXPANSION YES)
	set(DOXYGEN_EXPAND_ONLY_PREDEF YES)
	set(DOXYGEN_SEARCH_INCLUDES YES)
	set(DOXYGEN_PREDEFINED
		"STORMBYTE_NETWORK_PUBLIC:="
		"STORMBYTE_NETWORK_PRIVATE:="
		"STORMBYTE_BUFFER_PUBLIC:="
		"STORMBYTE_BUFFER_PRIVATE:="
		"STORMBYTE_SYSTEM_PUBLIC:="
		"STORMBYTE_STRING_PUBLIC:="
		"STORMBYTE_LOGGER_PUBLIC:="
		"STORMBYTE_PUBLIC:="
		"WINDOWS="
		"LINUX="
		"MACOS="
		"UNIX="
		"CLANG="
		"GCC="
		"MSVC="
	)

	set(DOXYGEN_EXTRACT_ALL YES)
	set(DOXYGEN_EXTRACT_STATIC YES)
	set(DOXYGEN_EXTRACT_LOCAL_CLASSES YES)
	set(DOXYGEN_HIDE_UNDOC_MEMBERS NO)
	set(DOXYGEN_JAVADOC_AUTOBRIEF YES)
	set(DOXYGEN_BUILTIN_STL_SUPPORT YES)
	set(DOXYGEN_MARKDOWN_SUPPORT YES)
	set(DOXYGEN_MARKDOWN_ID_STYLE GITHUB)
	set(DOXYGEN_TOC_INCLUDE_HEADINGS 3)
	set(DOXYGEN_AUTOLINK_SUPPORT YES)

	set(DOXYGEN_GENERATE_HTML YES)
	set(DOXYGEN_HTML_OUTPUT ".")
	set(DOXYGEN_GENERATE_TREEVIEW YES)
	set(DOXYGEN_DISABLE_INDEX YES)
	set(DOXYGEN_FULL_SIDEBAR NO)
	set(DOXYGEN_HTML_DYNAMIC_MENUS YES)
	set(DOXYGEN_HTML_COLORSTYLE DARK)
	set(DOXYGEN_HTML_COLORSTYLE_HUE 160)
	set(DOXYGEN_HTML_COLORSTYLE_SAT 40)
	set(DOXYGEN_HTML_COLORSTYLE_GAMMA 90)
	set(DOXYGEN_HTML_EXTRA_STYLESHEET "${CMAKE_CURRENT_SOURCE_DIR}/doxygen-extra.css")

	set(DOXYGEN_GENERATE_TAGFILE "${CMAKE_SOURCE_DIR}/docs/StormByte-Network.tag")
	set(DOXYGEN_TAGFILES
		"${StormByte_TAG_FILE}=https://dev.stormbyte.org/StormByte"
		"${StormByte-Buffer_TAG_FILE}=https://dev.stormbyte.org/StormByte-Buffer"
		"${StormByte-Logger_TAG_FILE}=https://dev.stormbyte.org/StormByte-Logger"
		"${StormByte-String_TAG_FILE}=https://dev.stormbyte.org/StormByte-String"
		"${StormByte-System_TAG_FILE}=https://dev.stormbyte.org/StormByte-System"
	)

	set(DOXYGEN_GENERATE_MAN NO)
	set(DOXYGEN_GENERATE_LATEX NO)
	set(DOXYGEN_QUIET NO)

	doxygen_add_docs(
		StormByteNetworkDoc
		"${CMAKE_SOURCE_DIR}/lib/public"
		"${CMAKE_SOURCE_DIR}/README.md"
		"${CMAKE_SOURCE_DIR}/CHANGELOG.md"
		"${CMAKE_SOURCE_DIR}/CONTRIBUTING.md"
		"${CMAKE_SOURCE_DIR}/CODING_STYLE.md"
		"${CMAKE_SOURCE_DIR}/LICENSE"
		"${CMAKE_SOURCE_DIR}/COPYING.LGPLv3"
		"${CMAKE_SOURCE_DIR}/doc/client-server-invariants.md"
		COMMENT "Generate HTML pages"
	)
endif()
