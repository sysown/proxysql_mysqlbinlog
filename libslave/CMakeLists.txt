CMAKE_MINIMUM_REQUIRED (VERSION 2.8)
PROJECT (libslave)

# Build flags
IF (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
 OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Common options
    ADD_DEFINITIONS (-pipe)
    ADD_DEFINITIONS (-Wall)
    ADD_DEFINITIONS (-O2)
    ADD_DEFINITIONS (-std=c++11)
    ADD_DEFINITIONS (-DUSE_PSI_1)
    ADD_DEFINITIONS (-DHAVE_PSI_1)
ENDIF ()

FIND_PACKAGE (Boost REQUIRED)
INCLUDE_DIRECTORIES (${Boost_INCLUDE_DIRS})
FIND_PACKAGE (Boost 1.41.0 COMPONENTS unit_test_framework system)

INCLUDE (FindPackageHandleStandardArgs)
FIND_PATH (IMYSQL mysql/mysql.h)
FIND_LIBRARY (LMYSQLCLIENT_R NAMES libmysqlclient_r.a
                                   libmysqlclient.a
                                   libmysqlclient_r.so
                                   libmysqlclient.so PATH_SUFFIXES mysql)
SET (Mysql_FIND_REQUIRED YES)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Mysql DEFAULT_MSG IMYSQL LMYSQLCLIENT_R)
INCLUDE_DIRECTORIES ("${IMYSQL}/mysql")

# Check whether we've found static or dynamic library
STRING (REGEX MATCH ".*\\.a$" MYSQL_IS_STATIC ${LMYSQLCLIENT_R})
IF (MYSQL_IS_STATIC STREQUAL LMYSQLCLIENT_R)
    SET (MYSQL_IS_STATIC ON)
ELSE ()
    SET (MYSQL_IS_STATIC OFF)
ENDIF ()

FILE (GLOB HDR "*.h")
INSTALL (FILES ${HDR} DESTINATION include)
AUX_SOURCE_DIRECTORY (${CMAKE_SOURCE_DIR} SRC)

ADD_LIBRARY (slave_a ${SRC})
SET_TARGET_PROPERTIES (slave_a PROPERTIES OUTPUT_NAME slave)
TARGET_LINK_LIBRARIES (slave_a ${LMYSQLCLIENT_R} dl)
INSTALL (TARGETS slave_a DESTINATION lib64)

# Most probably statc mysql is built without fPIC, so, we can't build dynamic library with it
IF (NOT MYSQL_IS_STATIC)
    ADD_LIBRARY (slave_so SHARED ${SRC})
    SET_TARGET_PROPERTIES (slave_so PROPERTIES OUTPUT_NAME slave)
    TARGET_LINK_LIBRARIES (slave_so ${LMYSQLCLIENT_R})
    INSTALL (TARGETS slave_so DESTINATION lib64)
ENDIF ()

IF (Boost_FOUND)
    ENABLE_TESTING ()
ENDIF (Boost_FOUND)
ADD_SUBDIRECTORY (test)
