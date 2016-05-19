##*****************************************************************************
## $Id$
##*****************************************************************************
#  AUTHOR:
#    Jim Garlick <garlick@llnl.gov>
#
#  SYNOPSIS:
#    AC_MRSH
#
#  DESCRIPTION:
#    Checks for mrsh
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC or equivalent.
##*****************************************************************************

AC_DEFUN([AC_MRSH],
[
  #
  # Check for whether to include mrsh module
  #
  AC_MSG_CHECKING([for whether to build mrsh module])
  AC_ARG_WITH([mrsh],
    AC_HELP_STRING([--with-mrsh], [Build mrsh module]),
    [ case "$withval" in
        no)  ac_with_mrsh=no ;;
        yes) ac_with_mrsh=yes ;;
        *)   AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$withval" for --with-mrsh]) ;;
      esac
    ]
  )
  AC_MSG_RESULT([${ac_with_mrsh=no}])
   
  if test "$ac_with_mrsh" = "yes"; then
    # is libmunge installed?    
    AC_CHECK_LIB([munge], [munge_encode], [ac_have_libmunge=yes], [])

    if test "$ac_have_libmunge" != "yes" ; then
       AC_MSG_NOTICE([Cannot support mrsh without libmunge])
    fi 

    if test "$ac_have_libmunge" = "yes" ; then
      ac_have_mrsh=yes
      AC_ADD_STATIC_MODULE("mcmd")
      MRSH_LIBS="-lmunge"
      AC_DEFINE([HAVE_MRSH], [1], [Define if you have mrsh.])
    fi
  fi

  AC_SUBST(HAVE_MRSH)
  AC_SUBST(MRSH_LIBS)
])
