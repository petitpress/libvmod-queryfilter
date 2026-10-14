#=============================================================================
#
# libvmod-queryfilter: src/Makefile.am
#
#=============================================================================

AM_CPPFLAGS = $(VCACHEAPI_CFLAGS)

#vmoddir = @VARNISH_VMODDIR@
#vmod_LTLIBRARIES = libvmod_queryfilter.la
#
#libvmod_queryfilter_la_LDFLAGS = \
#    @VARNISH_LIBS@ \
#    -module \
#    -export-dynamic \
#    -avoid-version \
#    -shared

vmod_LTLIBRARIES = libvmod_queryfilter.la

libvmod_queryfilter_la_SOURCES = \
	vcc_queryfilter_if.c \
	vcc_queryfilter_if.h \
	vmod_queryfilter.c

dist_man_MANS = vmod_queryfilter.3

@BUILD_VMOD_QUERYFILTER@

AM_TESTS_ENVIRONMENT = \
        PATH="$(VMOD_TEST_PATH)" \
        LD_LIBRARY_PATH="$(VCACHE_LIBRARY_PATH)"
TEST_EXTENSIONS = .vtc
VTC_LOG_COMPILER = vtest -E@VTESTEXT@
AM_VTC_LOG_FLAGS = -v -Dvmod_queryfilter="$(VMOD_QUERYFILTER)"

TESTS = @VMOD_TESTS@

EXTRA_DIST = \
	vmod_queryfilter.vcc \
	$(VMOD_TESTS)
