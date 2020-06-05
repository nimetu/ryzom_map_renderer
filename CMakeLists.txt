# 'client_sheets' and 'game_share'
#INCLUDE_DIRECTORIES(
#	${CMAKE_SOURCE_DIR}/ryzom/client/src
#	${CMAKE_SOURCE_DIR}/ryzom/common/src
#	)

# compile client sheets even when client is not compiled
#IF(NOT WITH_RYZOM_CLIENT)
#	ADD_SUBDIRECTORY(${CMAKE_SOURCE_DIR}/ryzom/client/src/client_sheets "${CMAKE_BINARY_DIR}/ryzom/client/src/client_sheets")
#ENDIF(NOT WITH_RYZOM_CLIENT)

LINK_DIRECTORIES(${LINK_DIRECTORIES} ${CMAKE_LIBRARY_DIR})

FILE(GLOB SRC src/*.cpp src/*.h)

ADD_EXECUTABLE(map_renderer ${SRC}
	${CMAKE_SOURCE_DIR}/ryzom/client/src/sheet_manager.cpp
	${CMAKE_SOURCE_DIR}/ryzom/client/src/sheet_manager.h
	${CMAKE_SOURCE_DIR}/ryzom/client/src/zone_util.cpp
	${CMAKE_SOURCE_DIR}/ryzom/client/src/zone_util.h
	)

INCLUDE_DIRECTORIES(
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/ryzom/client/src
	${CMAKE_SOURCE_DIR}/ryzom/common/src
	${LIBXML2_INCLUDE_DIR}
	)

TARGET_LINK_LIBRARIES(map_renderer
	ryzom_clientsheets
	ryzom_gameshare
	nel3d
	nelmisc
	nelpacs
	)

NL_DEFAULT_PROPS(map_renderer "Ryzom, Tools: Map Renderer")
NL_ADD_RUNTIME_FLAGS(map_renderer)

INSTALL(TARGETS map_renderer RUNTIME DESTINATION bin COMPONENT tools)

