##*****************************************************************************
## $Id$
##*****************************************************************************
#  AUTHOR:
#    Jim Garlick <garlick@llnl.gov>
#
#  SYNOPSIS:
#    AC_KRB4
#
#  DESCRIPTION:
#    Adds support for kerberos rcmd method.  Checks for kerberos
#    libraries.  
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC or equivalent.
##*****************************************************************************

AC_DEFUN([AC_KRB4],
[

#
# Check for kerberos libraries, if they exist, automatically build
# kerberos module
#

AC_CHECK_LIB([krb], [krb_sendauth], 
             [ac_have_krb4=yes],
             [ac_have_krb4=no], [-lkrb -ldes])

if test "$ac_have_krb4" = "yes" ; then
    AC_ADD_STATIC_MODULE("k4cmd")
    KRB_LIBS="-lkrb -ldes"
    AC_DEFINE([HAVE_KRB4], [1], [Define if you have Kerberos])
fi

AM_CONDITIONAL(WITH_KRB4, test "$ac_have_krb4" = "yes")

AC_SUBST(KRB_LIBS)

])
