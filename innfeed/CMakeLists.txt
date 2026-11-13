# innfeed and imapfeed — converted from innfeed/Makefile.  The configuration
# parser is generated from configfile.y and configfile.l with the system's
# yacc and lex, as the Make build does.  procbatch is a script generated from
# procbatch.in (inn_scripts, cmake/InnPaths.cmake).

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

# Same output names as the Make build ("yacc -d", then renamed).
bison_target(innfeed_config configfile.y ${CMAKE_CURRENT_BINARY_DIR}/config_y.cc
             DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/config_y.h
             COMPILE_FLAGS "-y")
flex_target(innfeed_lexer configfile.l ${CMAKE_CURRENT_BINARY_DIR}/config_l.cc)
add_flex_bison_dependency(innfeed_lexer innfeed_config)

set(INNFEED_COMMON
    article.cc buffer.cc endpoint.cc host.cc innlistener.cc main.cc misc.cc tape.cc
    ${BISON_innfeed_config_OUTPUTS} ${FLEX_innfeed_lexer_OUTPUTS})

set(_sasl)
if(HAVE_SASL)
    set(_sasl ${INN_SASL_LIB})
endif()

# endpoint.cc runs the event loop on Boost.Asio (ASIO_PLAN.md, phase 3).
inn_program(innfeed  SOURCES ${INNFEED_COMMON} connection.cc LIBS inn_asio)
inn_program(imapfeed SOURCES ${INNFEED_COMMON} imap_connection.cc
            LIBS inn_asio ${_sasl})
target_include_directories(innfeed  PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(imapfeed PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

inn_scripts(procbatch)

# innfeed/Makefile's install target: innfeed and imapfeed (LI_XPRI),
# procbatch (CP_XPRI).
inn_install(TARGETS innfeed imapfeed DESTINATION ${bindir} MODE XPRI)
inn_install(FILES procbatch DESTINATION ${bindir} MODE XPRI)
