#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.71])
AC_INIT([picsnvideos],[0.2])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([picsnvideos.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIRS([m4])

# Checks for programs.
AC_PROG_CC
AC_SEARCH_LIBS([strerror],[cposix])

AC_DISABLE_STATIC
LT_INIT
AC_PROG_INSTALL

# Checks for libraries.
AC_CHECK_LIB(gdbm, main, LIBS="$libs -lgdbm",
 AC_MSG_ERROR([*** GDBM not installed - please install first ***]))

# Checks for header files.
m4_warn([obsolete],
[The preprocessor macro `STDC_HEADERS' is obsolete.
  Except in unusual embedded environments, you can safely include all
  ISO C90 headers unconditionally.])dnl
# Autoupdate added the next two lines to ensure that your configure
# script's behavior did not change.  They are probably safe to remove.
AC_CHECK_INCLUDES_DEFAULT
AC_PROG_EGREP

AC_CHECK_HEADERS([stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([mkdir])
AC_CHECK_FUNCS([utime])

AC_CONFIG_FILES([Makefile])

dnl copyed from jpilot-backup configure.in
dnl ******************************
dnl * pilot-link libs
dnl ******************************
dnl Check for pilot-link libs
pilot_prefix=""
AC_ARG_WITH(pilot_prefix,
 AS_HELP_STRING([--with-pilot-prefix=PFX],[Prefix to top level of pilot-link files (e.g., = /usr/local if the pilot-link includes are in /usr/local/include and libs are in /usr/local/lib)]))

if test "x$with_pilot_prefix" != "x"; then
   pilot_prefix=$with_pilot_prefix
fi

dnl Make sure that the pilot-link stuff actually exists

AC_MSG_CHECKING(for pilot-link header files)
pilotinclude=${FORCE_PILOT_INCLUDES:-no}
if test $pilotinclude = no ; then
for pilot_incl in $pilot_prefix/include /usr/include /usr/local/include \
    /usr/extra/pilot/include /usr/include/libpisock; do
   if test -r "$pilot_incl/pi-version.h" ; then
      pilotinclude=yes
      PILOT_FLAGS="$PILOT_FLAGS -I$pilot_incl"
      break
   fi
done
fi

if test $pilotinclude = no ; then
   AC_MSG_RESULT(no)
   AC_MSG_ERROR(Could not find the pilot-link header files)
else
   AC_MSG_RESULT(found at $pilot_incl)
fi

dnl pilot-link maybe installed at this point

AC_MSG_CHECKING(for pilot library files)
pilotlibs=${FORCE_PILOT_LIBS:-no}
PILOT_LIBS="-lpisock"

if test $pilotlibs = no ; then
for pilot_libs in $pilot_prefix/lib /usr/lib /usr/local/lib/ \
  /usr/extra/pilot/lib ; do
   if test -r $pilot_libs/libpisock.so >/dev/null 2>&1 ; then
      pilotlibs=yes
      PILOT_LIBS="-L$pilot_libs $PILOT_LIBS"
      break
   fi
   if test -r "$pilot_libs/libpisock.a" ; then
      pilotlibs=yes
      PILOT_LIBS="-L$pilot_libs $PILOT_LIBS"
      break
   fi
   if test -r "$pilot_libs/libpisock.sl" ; then
      pilotlibs=yes
      PILOT_LIBS="-L$pilot_libs $PILOT_LIBS"
      break
   fi
done
fi

if test $pilotlibs = no ; then
   AC_MSG_RESULT(no)
   AC_MSG_ERROR(Could not find the pilot-link libraries)
else
   AC_MSG_RESULT(found at $pilot_libs)
   AC_SUBST(PILOT_FLAGS)
   AC_SUBST(PILOT_LIBS)
fi

dnl Solaris needs the socket library
AC_CHECK_FUNC(gethostent, , AC_CHECK_LIB(nsl, gethostent))
AC_CHECK_FUNC(setsockopt, , AC_CHECK_LIB(socket, setsockopt))

dnl *************************
dnl * Check pilot-link pi-version.h
dnl *************************

AC_MSG_CHECKING(pilot-link version )

dnl ** save CFLAGS
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS $PILOT_FLAGS"
save_LIBS="$LIBS"
LIBS="$LIBS $PILOT_LIBS"

dnl *** check for pi-version.h
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <pi-version.h>]], [[
   exit(0);
]])],[],[AC_MSG_ERROR(pilot-link header pi-version.h not found)
])

dnl *****
dnl * Pilot-link version variations, 12.0, etc.
dnl *****

pl12=no

pl_version=`grep "define PILOT_LINK_VERSION" "$pilot_incl/pi-version.h" | \
cut -d " " -f 3`
pl_major=`grep "define PILOT_LINK_MAJOR" "$pilot_incl/pi-version.h" | \
cut -d " " -f 3`
pl_minor=`grep "define PILOT_LINK_MINOR" "$pilot_incl/pi-version.h" | \
cut -d " " -f 3`
pl_patch=`grep "define PILOT_LINK_PATCH" "$pilot_incl/pi-version.h" | \
cut -d " " -f 3 | sed -e 's/"//g'`
AC_MSG_RESULT([pi-version indicates $pl_version.$pl_major.$pl_minor])

dnl *** check for pilot-link 0.12 and up
if test $pl_version -eq 0 ; then
   if test $pl_major -ge 12 ; then
      pl12=yes;
      AC_MSG_RESULT([pilot-link has card support (>12.0)])
      AC_DEFINE(PILOT_LINK_0_12, 1, [ pl-0.12 support ])
   fi
fi

CFLAGS="$save_CFLAGS"
LIBS="$save_LIBS"

AC_SUBST(PROGNAME)

AC_OUTPUT

#dnl *************************
#dnl * GTK 2
#dnl *************************
#AC_ARG_ENABLE(gtk2,
#   AS_HELP_STRING([--disable-gtk2],[Disable GTK2 support and compile with GTK1]),
#   enable_gtk2=$enableval,
#   enable_gtk2=yes
#   )
#
#if test $enable_gtk2 = no; then
#  AC_MSG_RESULT(GTK2 support disabled by configure options)
#else
#  AC_DEFINE(ENABLE_GTK2, 1, [ GTK-2 support ])
#fi
#
#dnl Checking for GTK version
#dnl Check for GTK2
#
#have_gtk2=no;
#if test "x$enable_gtk2" = "xyes"; then
#  AM_PATH_GTK_2_0(2.0.3, have_gtk2=yes,
#  AC_MSG_ERROR([*** GTK >= 2.0.3 not found ***]))
#fi
#if test "x$have_gtk2" = "xno"; then
#  AM_PATH_GTK(1.2.0,,
#    AC_MSG_ERROR([*** GTK >= 1.2.0 not found ***]))
#fi
#
#fi
#
