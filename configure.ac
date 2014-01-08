
AC_PREREQ([2.65])
AC_INIT([spacestatus], m4_esyscmd_s(echo 1.$(git rev-list HEAD --count)),
    [lordyomin@hivegate.net], [spacestatus], [http://github.com/Yomin/spacestatus])
AM_INIT_AUTOMAKE([-Wall foreign])
AC_CONFIG_SRCDIR([src/spacestatus.c])
AC_CONFIG_HEADERS([config.h])

# command line arguments
AC_ARG_WITH(notify,
    AS_HELP_STRING([--with-notify], [send notification on status change]),
    AS_IF([test "x$withval" != "xno"],
        [AC_DEFINE([NOTIFY], [1], [Define to 1 if you want notifications])]
    )
)
AC_ARG_WITH(bubble,
    AS_HELP_STRING([--with-bubble],[send bubble request on status change]),
    AS_IF([test "x$withval" != "xno"],
        [AC_DEFINE([BUBBLE], [1], [Define to 1 if you want bubbles])]
    )
)
AC_ARG_WITH(xembed,
    AS_HELP_STRING([--with-xembed], [set xembed window property]),
    AS_IF([test "x$withval" != "xno"],
        [AC_DEFINE([XEMBED], [1], [Define to 1 if you want the xembed window property set])]
    )
)

# Checks for programs.
AC_PROG_CC
AC_CHECK_PROG([DATE], [date], [date])
if [ "x$DATE" != "xdate" ]; then
    AC_MSG_ERROR([date not found])
fi
AC_CHECK_PROG([SED], [sed], [sed])
if [ "x$SED" != "xsed" ]; then
    AC_MSG_ERROR([sed not found])
fi

# Checks for libraries.
AC_CHECK_LIB([X11], [XOpenDisplay])
AC_CHECK_LIB([Xpm], [XpmReadFileToImage])
AS_IF([test "x$with_notify" != "xno"], [PKG_CHECK_MODULES([LIBNOTIFY], [libnotify])])

# Checks for header files.
AC_PATH_X
AC_HEADER_RESOLV
AC_CHECK_HEADERS([netdb.h stdlib.h string.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([gettimeofday memset socket strchr strrchr strspn strstr])

AC_CONFIG_FILES([Makefile src/Makefile doc/Makefile])
AC_OUTPUT
