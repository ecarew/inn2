PORTNAME=	inn2-cxx
# pkg's version for the tag v2.8.0-cxx.alpha.5 (GH_TAGNAME), so it sorts
# before 2.8.0 and after alpha 4.
PORTVERSION=	2.8.0.a5
CATEGORIES=	news

MAINTAINER=	noreply@users.noreply.github.com
COMMENT=	InterNetNews news server, C++20/CMake conversion (alpha)
WWW=		https://github.com/ecarew/inn2

LICENSE=	GPLv2 ISCL
LICENSE_COMB=	multi
LICENSE_FILE_ISCL=	${WRKSRC}/LICENSE

BUILD_DEPENDS=	${LOCALBASE}/include/boost/version.hpp:devel/boost-libs
LIB_DEPENDS=	libcanlock.so:news/libcanlock \
		libcrypt.so:security/libxcrypt
RUN_DEPENDS=	p5-MIME-Tools>=0:mail/p5-MIME-Tools

USES=		bison cmake perl5 pkgconfig python sqlite ssl
USE_GITHUB=	yes
GH_ACCOUNT=	ecarew
GH_PROJECT=	inn2
GH_TAGNAME=	v2.8.0-cxx.alpha.5
USE_PERL5=	build run
USE_RC_SUBR=	innd

CONFLICTS=	inn inn-current

# FreeBSD's base system has the news account (uid and gid 8).
NEWSBASE=	${PREFIX}/news
CONFDIR=	${PREFIX}/etc/news
DBDIR=		/var/db/news
SPOOLDIR=	/var/spool/news
LOGDIR=		/var/log/news
RUNDIR=		/var/run/news

CMAKE_ARGS=	-DINN_TESTS=OFF -DINN_CCACHE=OFF \
		-DINN_PREFIX=${NEWSBASE} \
		-DINN_PATH_CONFIG=${CONFDIR} \
		-DINN_DB_DIR=${DBDIR} \
		-DINN_SPOOL_DIR=${SPOOLDIR} \
		-DINN_LOG_DIR=${LOGDIR} \
		-DINN_RUN_DIR=${RUNDIR} \
		-DINN_PATH_TMP=${SPOOLDIR}/incoming/tmp \
		-DINN_HTTP_DIR=${DBDIR}/http \
		-DINN_MAN_DIR=${PREFIX}/share/man \
		-DINN_DOC_DIR=${DOCSDIR} \
		-DINN_PERL_FILTERS=ON -DINN_PYTHON_FILTERS=ON \
		-DINN_TLS=ON -DINN_SYSTEMD=OFF -DINN_BLOCKLIST=ON \
		-DINN_PATHHOST=@PATHHOST@

SUB_FILES=	pkg-install pkg-message
SUB_LIST=	NEWSBASE=${NEWSBASE} CONFDIR=${CONFDIR} DBDIR=${DBDIR} \
		DATADIR=${DATADIR}
# pkg-plist names the paths literally (a PLIST_SUB value of "news" would
# match inside every file name containing it).

# Configuration files become @sample entries (pkg copies each into place
# when absent and leaves a site's edited copy alone); inn.conf names this
# server, so pkg-install makes it from a template.  Site data (active,
# newsgroups, history) is made by pkg-install when absent, never packaged.
post-install:
	${MKDIR} ${STAGEDIR}${DATADIR}
	${MV} ${STAGEDIR}${CONFDIR}/inn.conf ${STAGEDIR}${DATADIR}/inn.conf
	${INSTALL_DATA} ${WRKSRC}/samples/active.minimal \
	    ${WRKSRC}/samples/newsgroups.minimal ${STAGEDIR}${DATADIR}/
	cd ${STAGEDIR}${CONFDIR} && for f in $$(${FIND} . -type f ! -name '*.sample'); do \
	    ${MV} $$f $$f.sample; done
	# rc.d/innd makes /var/run/news at each start (it is cleared at boot).
	${RM} -r ${STAGEDIR}${RUNDIR}
	${RM} ${STAGEDIR}${DBDIR}/active ${STAGEDIR}${DBDIR}/active.times \
	    ${STAGEDIR}${DBDIR}/newsgroups ${STAGEDIR}${DBDIR}/history
	${RM} -r ${STAGEDIR}${NEWSBASE}/include
	${RM} ${STAGEDIR}${NEWSBASE}/lib/*.a

.include <bsd.port.mk>
