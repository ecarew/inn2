# innfeed and imapfeed — converted from innfeed/Makefile.  The configuration
# parser is generated from configfile.y and configfile.l with the system's
# yacc and lex, as the Make build does.  procbatch is a script generated from
# procbatch.in and is not built here yet.

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

# Same output names as the Make build ("yacc -d", then renamed).
bison_target(innfeed_config configfile.y ${CMAKE_CURRENT_BINARY_DIR}/config_y.c
             DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/config_y.h
             COMPILE_FLAGS "-y")
flex_target(innfeed_lexer configfile.l ${CMAKE_CURRENT_BINARY_DIR}/config_l.c)
add_flex_bison_dependency(innfeed_lexer innfeed_config)

set(INNFEED_COMMON
    article.c buffer.c endpoint.c host.c innlistener.c main.c misc.c tape.c
    ${BISON_innfeed_config_OUTPUTS} ${FLEX_innfeed_lexer_OUTPUTS})

set(_sasl)
if(HAVE_SASL)
    set(_sasl ${INN_SASL_LIB})
endif()

inn_program(innfeed  SOURCES ${INNFEED_COMMON} connection.c)
inn_program(imapfeed SOURCES ${INNFEED_COMMON} imap_connection.c LIBS ${_sasl})
target_include_directories(innfeed  PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(imapfeed PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
