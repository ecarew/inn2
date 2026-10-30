# Make the tests' sandbox (tests/CMakeLists.txt): a fresh copy of tests/data
# (a symlink would resolve ../.. from its target), with the build's program
# directories, samples and include beside it.  Run with -DSOURCE=, -DBUILD=
# and -DSANDBOX=.  Keeps tests/overview, where the build put the ovsqlite
# helpers.

file(REMOVE_RECURSE ${SANDBOX}/tests/data)
file(MAKE_DIRECTORY ${SANDBOX}/tests)
file(COPY ${SOURCE}/tests/data DESTINATION ${SANDBOX}/tests)
foreach(d authprogs backends control expire frontends history innfeed nnrpd
          samples storage include)
    if(EXISTS ${BUILD}/${d} AND NOT EXISTS ${SANDBOX}/${d})
        file(CREATE_LINK ${BUILD}/${d} ${SANDBOX}/${d} SYMBOLIC)
    endif()
endforeach()
