# doc/ — man pages and text documents from POD, and the documents
# installed to docdir.  Converted from doc/pod/Makefile (which only
# maintainers ran), doc/man/Makefile and doc/Makefile, plus the man-page
# rules of backends/, control/, frontends/ and perl/.  Everything here is
# built at build time and never committed (the architect, 2026-11-15).

find_program(INN_POD2MAN pod2man REQUIRED)
find_program(INN_POD2TEXT pod2text REQUIRED)

# podlators 5.0 and later have --guesswork; =none turns off heuristics
# meant for Perl's own documentation, which mangle patterns such as
# C<@@NCM> in perl-nocem(8).  configure probed pod2text for it, as here.
set(_inn_pod_probe ${CMAKE_CURRENT_BINARY_DIR}/probe.pod)
file(WRITE ${_inn_pod_probe} "=head1 test\n")
execute_process(COMMAND ${INN_POD2TEXT} --guesswork=none ${_inn_pod_probe}
                RESULT_VARIABLE _inn_rc OUTPUT_QUIET ERROR_QUIET)
set(INN_POD_GUESSWORK)
if(_inn_rc EQUAL 0)
    set(INN_POD_GUESSWORK --guesswork=none)
endif()

set(_inn_man_opts ${INN_POD_GUESSWORK} -c "InterNetNews Documentation"
    -r "INN ${PROJECT_VERSION}")
set(_inn_man_out ${CMAKE_CURRENT_BINARY_DIR}/man)
set(_inn_text_out ${CMAKE_CURRENT_BINARY_DIR}/text)
file(MAKE_DIRECTORY ${_inn_man_out} ${_inn_text_out})
set(INN_DOC_OUTPUTS)

# _inn_man(<source> <page> <section> [<name>]): pod2man <source> into
# man/<page>.<section>; <name> is the NAME in the header when it is not
# the page's (embedded POD in scripts and modules).
function(_inn_man source page section)
    set(out ${_inn_man_out}/${page}.${section})
    set(name)
    if(ARGC GREATER 3)
        set(name -n ${ARGV3})
    elseif(section STREQUAL "3")
        # For section 3, pod2man takes the title from the source's path as
        # a module name (Users::...::doc::pod::libinn), which leaked the
        # build directory into the page and made builds differ by path;
        # the Makefile ran it inside doc/pod.  The page name is the title.
        set(name -n ${page})
    endif()
    add_custom_command(OUTPUT ${out}
        COMMAND ${INN_POD2MAN} ${_inn_man_opts} -s ${section} ${name}
                ${source} ${out}
        DEPENDS ${source}
        COMMENT "pod2man ${page}.${section}" VERBATIM)
    set(INN_DOC_OUTPUTS ${INN_DOC_OUTPUTS} ${out} PARENT_SCOPE)
endfunction()

set(INN_MAN1 convdate fastrm gencancel getlist grephistory inews innconfval
    innmail nntpget pullnews rnews shrinkfile simpleftp)
set(INN_MAN3 libinn libinnauth libinnhist libinnstorage libinn_clientlib
    libinn_dbz libinn_inndcomm libinn_qio libinn_nativemat)
set(INN_MAN5 active active.times buffindexed.conf control.ctl cycbuff.conf
    distrib.pats distributions expire.ctl hissqlite history incoming.conf
    inn.conf innfeed.conf innreport.conf inn-secrets.conf innwatch.ctl
    moderators motd.news newsfeeds newsgroups newslog nnrpd.track
    nntpsend.ctl ovdb ovsqlite passwd.nntp inn-radius.conf readers.conf
    storage.conf subscriptions)
set(INN_MAN8 actmerge actsync archive batcher buffchan ckpasswd
    cnfsheadconf cnfsstat controlbatch controlchan ctlinnd cvtbatch
    docheckgroups domain expire expireover expirerm hissqlite-convert
    hissqlite-util ident innbind inncheck innd inndf innfeed innreport
    innstat innupgrade innwatch innxbatch innxmit makedbz makehistory
    mod-active news.daily news2mail ninpaths nnrpd nntpsend ovdb_init
    ovdb_monitor ovdb_server ovdb_stat overchan ovsqlite-server
    ovsqlite-util procbatch prunehistory radius rc.news scanlogs scanspool
    send-ihave sendinpaths shlock sm tally.control tdx-util tinyleaf
    writelog)
# auth_krb5 is built only with Kerberos (authprogs/CMakeLists.txt).
if(TARGET auth_krb5)
    list(APPEND INN_MAN8 auth_krb5)
endif()

foreach(section 1 3 5 8)
    foreach(page ${INN_MAN${section}})
        _inn_man(${CMAKE_CURRENT_SOURCE_DIR}/pod/${page}.pod ${page}
                 ${section})
    endforeach()
endforeach()

# Embedded POD in scripts and Perl modules.
set(src ${CMAKE_SOURCE_DIR})
_inn_man(${src}/backends/delayer.in delayer 8 DELAYER)
_inn_man(${src}/control/perl-nocem.in perl-nocem 8 PERL-NOCEM)
_inn_man(${src}/control/pgpverify.in pgpverify 1 PGPVERIFY)
_inn_man(${src}/frontends/mailpost.in mailpost 8 MAILPOST)
_inn_man(${src}/perl/INN/Config.pm.in INN::Config 3pm INN::Config)
_inn_man(${src}/perl/INN/Utils/Shlock.pm.in INN::Utils::Shlock 3pm
         INN::Utils::Shlock)
_inn_man(${src}/perl/INN/ovsqlite_client.pm INN::ovsqlite_client 3pm
         INN::ovsqlite_client)
list(APPEND INN_MAN1 pgpverify)
list(APPEND INN_MAN8 delayer perl-nocem mailpost)
set(INN_MAN3PM INN::Config INN::Utils::Shlock INN::ovsqlite_client)

# Other names for pages (doc/man/Makefile's links), as relative symbolic
# links made in the build tree; install() copies a link as a link.
set(INN_MAN_LINKS
    5 hisv6 history.5
    5 motd.innd motd.news.5
    5 motd.nnrpd motd.news.5
    5 localgroups ../man8/docheckgroups.8
    5 nocem.ctl ../man8/perl-nocem.8
    8 actsyncd actsync.8
    8 imapfeed innfeed.8
    8 inpaths ninpaths.8
    8 sendxbatches innxbatch.8)
set(_inn_links)
while(INN_MAN_LINKS)
    list(POP_FRONT INN_MAN_LINKS section name target)
    set(dir ${_inn_man_out}/links/man${section})
    file(MAKE_DIRECTORY ${dir})
    file(CREATE_LINK ${target} ${dir}/${name}.${section} SYMBOLIC)
    list(APPEND _inn_links_${section} ${dir}/${name}.${section})
endwhile()

# Text documents (doc/pod/Makefile's TEXT): pod2text -s -l.
set(INN_TEXT_DOCS hacking:HACKING install:INSTALL news:NEWS
    checklist:checklist external-auth:external-auth hook-perl:hook-perl
    hook-python:hook-python)
set(INN_TEXT_FILES)
foreach(pair ${INN_TEXT_DOCS})
    string(REPLACE ":" ";" pair ${pair})
    list(GET pair 0 pod)
    list(GET pair 1 name)
    set(out ${_inn_text_out}/${name})
    add_custom_command(OUTPUT ${out}
        COMMAND ${INN_POD2TEXT} ${INN_POD_GUESSWORK} -s -l
                ${CMAKE_CURRENT_SOURCE_DIR}/pod/${pod}.pod ${out}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/pod/${pod}.pod
        COMMENT "pod2text ${name}" VERBATIM)
    list(APPEND INN_DOC_OUTPUTS ${out})
    list(APPEND INN_TEXT_FILES ${out})
endforeach()

add_custom_target(inn_docs ALL DEPENDS ${INN_DOC_OUTPUTS})

# Install: man pages under ${prefix}/share/man (Autoconf's default
# mandir), the documents to docdir (doc/Makefile's TOPDOCS and DOCS).
foreach(section 1 3 5 8)
    set(files)
    foreach(page ${INN_MAN${section}})
        list(APPEND files ${_inn_man_out}/${page}.${section})
    endforeach()
    install(FILES ${files} ${_inn_links_${section}}
            DESTINATION ${mandir}/man${section}
            PERMISSIONS ${INN_MODE_RPUB})
endforeach()
set(files)
foreach(page ${INN_MAN3PM})
    list(APPEND files ${_inn_man_out}/${page}.3pm)
endforeach()
install(FILES ${files} DESTINATION ${mandir}/man3
        PERMISSIONS ${INN_MODE_RPUB})

install(FILES ${INN_TEXT_FILES}
        ${CMAKE_SOURCE_DIR}/CONTRIBUTORS ${CMAKE_SOURCE_DIR}/LICENSE
        ${CMAKE_SOURCE_DIR}/README.md ${CMAKE_SOURCE_DIR}/TODO
        GPL config-design config-semantics config-syntax history
        history-innfeed sample-control
        DESTINATION ${docdir} PERMISSIONS ${INN_MODE_RPUB})

# Release tarball (support/mkdist): git archive of HEAD plus the text
# documents above.  cmake --build build --target dist
add_custom_target(dist
    COMMAND sh ${CMAKE_SOURCE_DIR}/support/mkdist ${PROJECT_VERSION}
            ${CMAKE_SOURCE_DIR} ${_inn_text_out} ${CMAKE_BINARY_DIR}
    DEPENDS ${INN_TEXT_FILES}
    COMMENT "Making inn-${PROJECT_VERSION}.tar.gz" VERBATIM)
