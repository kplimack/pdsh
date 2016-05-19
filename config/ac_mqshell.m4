##*****************************************************************************
## $Id$
##*****************************************************************************
#  AUTHOR:
#    Albert Chu <chu11@llnl.gov>
#
#  SYNOPSIS:
#    AC_MQSHELL
#
#  DESCRIPTION:
#    Checks for mqshell option and checks for things mqshd requires
#
#  WARNINGS:
#    This macro must be called after AC_QSHELL and AC_MRSH
#    This macro must be placed after AC_PROG_CC or equivalent.
##*****************************************************************************

# Found online, original author not known
AC_DEFUN([AC_STRUCT_SA_LEN],
[
  AC_CACHE_CHECK([for sa_len in struct sockaddr], ac_cv_struct_sa_len,
        AC_TRY_COMPILE([#include <sys/types.h> #include <sys/socket.h>], 
                        [struct sockaddr s; s.sa_len;],
                        ac_cv_struct_sa_len=yes, 
                        ac_cv_struct_sa_len=no))

  if test $ac_cv_struct_sa_len = yes; then
     AC_DEFINE(HAVE_SA_LEN, [1], [do we have sa_len in struct sockaddr])  
  fi
])

AC_DEFUN([AC_MQSHELL],
[
  #
  # Check for whether to include mqshell module
  # Assumes this is AFTER --with-qshell and --with-mrsh checks
  #
  AC_MSG_CHECKING([for whether to build mqshell module and mqshd daemon])
  AC_ARG_WITH([mqshell],
    AC_HELP_STRING([--with-mqshell], [Build mqshell module and mqshd daemon]),
    [ case "$withval" in
        no)  ac_with_mqshell=no ;;
        yes) ac_with_mqshell=yes ;;
        *)   AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$withval" for --with-mqshell]) ;;
      esac
    ]
  )
  AC_MSG_RESULT([${ac_with_mqshell=no}])
   
  if test "$ac_with_mqshell" = "yes"; then
       
     AC_ELAN
       
     if test "$ac_have_elan" != "yes"; then 
       ac_have_mqshell=no
     else
        
       if test "$ac_with_pam" = "yes" ; then
          AC_CHECK_LIB([pam], [pam_start], [ac_mqshell_have_pam=yes])
          if test "$ac_mqshell_have_pam" != "yes" ; then
             AC_MSG_NOTICE([Cannot support mqshell without libpam])
             AC_MSG_NOTICE([Consider turning off PAM with --without-pam])
	         AC_MSG_ERROR([Error configuring mqshell.])
           else
             QSHELL_LIBS="-lpam -lpam_misc"
             AC_DEFINE_UNQUOTED(USE_PAM, [1])
             ac_mqshell_qshell=yes
           fi
       else
          ac_mqshell_have_pam=no
          ac_mqshell_qshell=yes
       fi

       AC_SUBST(QSHELL_LIBS)

       # check for munge libs if --with-mrsh was not specified
       if test "$ac_have_libmunge" = "yes"; then 
          ac_mqshell_munge=yes
       else
          AC_CHECK_LIB([munge], [munge_encode], [ac_mqshell_have_munge=yes])

          if test "$ac_mqshell_have_munge" != "yes" ; then
             AC_MSG_NOTICE([Cannot support mqshell without libmunge])
             ac_mqshell_munge=no
          else
             ac_mqshell_munge=yes
             MRSH_LIBS="-lmunge" 
             AC_SUBST(MRSH_LIBS)
          fi
       fi

       # do we have everything we want?
       if test "$ac_mqshell_qshell" = "yes" &&
          test "$ac_mqshell_munge" = "yes" ; then
          ac_have_mqshell=yes
          AC_ADD_STATIC_MODULE("mqcmd")
          AC_DEFINE_UNQUOTED([HAVE_MQSHELL], [1], [Define if you have mqshell.])
          PROG_MQSHD=in.mqshd   

          # check for IPv6, IEEE standard says it should be in sys/socket.h 
          AC_CHECK_DECL([AF_INET6], 
                        AC_DEFINE(HAVE_IPV6,1,[have IPv6]),,
                        [#include <sys/socket.h>])  

          AC_STRUCT_SA_LEN

          # compile libqsw
          ac_have_qsw=yes
       fi
     fi      
   fi

   AC_SUBST(PROG_MQSHD)
   AC_SUBST(HAVE_MQSHELL)
])
