if(NOT EasyLoggingPP_FIND_VERSION)
	set(EasyLoggingPP_FIND_VERSION 9.96.7)
endif()

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/easyloggingpp")
file(
		DOWNLOAD
		"https://github.com/amrayn/easyloggingpp/archive/v${EasyLoggingPP_FIND_VERSION}.zip"
		"${CMAKE_BINARY_DIR}/easyloggingpp/v${EasyLoggingPP_FIND_VERSION}.zip"
		SHOW_PROGRESS
)
file(
		ARCHIVE_EXTRACT
		INPUT "${CMAKE_BINARY_DIR}/easyloggingpp/v${EasyLoggingPP_FIND_VERSION}.zip"
		DESTINATION "${CMAKE_BINARY_DIR}/easyloggingpp"
)
set(EasyLoggingPP_ROOT_DIR "${CMAKE_BINARY_DIR}/easyloggingpp/easyloggingpp-${EasyLoggingPP_FIND_VERSION}")

if (EXISTS "${EasyLoggingPP_ROOT_DIR}/src/easylogging++.cc")
	set(EasyLoggingPP_INCLUDE_DIRS "${EasyLoggingPP_ROOT_DIR}/src")
	set(EasyLoggingPP_SRC "${EasyLoggingPP_ROOT_DIR}/src/easylogging++.cc")
	
	add_library(EasyLoggingPP::EasyLoggingPP INTERFACE IMPORTED)
	set_target_properties(
			EasyLoggingPP::EasyLoggingPP PROPERTIES
			INTERFACE_INCLUDE_DIRECTORIES "${EasyLoggingPP_INCLUDE_DIRS}"
			INTERFACE_SOURCES "${EasyLoggingPP_SRC}"
	)
else()
	unset(EasyLoggingPP_FOUND)
endif()
