/*****************************************************************************\
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2001-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>.
 *  UCRL-CODE-2003-005.
 *  
 *  This file is part of Pdsh, a parallel remote shell program.
 *  For details, see <http://www.llnl.gov/linux/pdsh/>.
 *  
 *  Pdsh is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  Pdsh is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with Pdsh; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

/*
 * Started with BSD rcmd.c which is:
 * 
 * Copyright (c) 1983, 1993, 1994, 2003
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 *
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * 5. This is free software; you can redistribute it and/or modify it
 *    under the terms of the GNU General Public License as published
 *    by the Free Software Foundation; either version 2 of the
 *    License, or (at your option) any later version.
 *                              
 * 6. This is distributed in the hope that it will be useful, but
 *    WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *                                                           
 * 7. You should have received a copy of the GNU General Public License;
 *    if not, write to the Free Software Foundation, Inc., 59 Temple
 *    Place, Suite 330, Boston, MA  02111-1307  USA.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)mcmd.c      Based from: 8.3 (Berkeley) 3/26/94";
#endif /* LIBC_SCCS and not lint */

#if     HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/param.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/stat.h>

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#if HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <netdb.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <pwd.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <elan3/elanvp.h>
#include <munge.h>

#include "src/qsnet/qswutil.h"
#include "src/common/xmalloc.h"      /* Free() */
#include "src/common/macros.h"       /* LINEBUFSIZE && IP_ADDR_LEN */
#include "src/common/err.h"
#include "src/common/fd.h"
#include "src/common/xpoll.h"
#include "src/pdsh/mod.h"

#define MQSH_PORT       21234

extern char **environ;

static bool dist_set = false;
static bool cyclic   = false;
static int  nprocs   = 1;

static unsigned int  railmask      = 1;
static bool          railmask_set  = false;

static char cwd[MAXPATHLEN + 1];
static qsw_info_t qinfo;
static ELAN_CAPABILITY cap;

#ifdef HAVE_PTHREAD
#define SET_PTHREAD()           pthread_sigmask(SIG_BLOCK, &blockme, &oldset)
#define RESTORE_PTHREAD()       pthread_sigmask(SIG_SETMASK, &oldset, NULL)
#define EXIT_PTHREAD()          RESTORE_PTHREAD(); \
return -1
#else
#define SET_PTHREAD()
#define RESTORE_PTHREAD()
#define EXIT_PTHREAD()          return -1
#endif

#if STATIC_MODULES
#  define pdsh_module_info mqcmd_module_info
#  define pdsh_module_priority mqcmd_module_priority
#endif    

int pdsh_module_priority = DEFAULT_MODULE_PRIORITY;

static int mqcmd_postop(opt_t *opt);

static int mqcmd_opt_m(opt_t *, int, char *);
static int mqcmd_opt_n(opt_t *, int, char *);
static int mqcmd_opt_r(opt_t *, int, char *);

static int mqcmd_init(opt_t *);
static int mqcmd_signal(int, void *, int);
static int mqcmd(char *, char *, char *, char *, char *, int, int *, void **); 

/* random num for all jobs in this group */
static unsigned int randy = -1;

/* 
 * Export pdsh module operations structure
 */
struct pdsh_module_operations mqcmd_module_ops = {
    (ModInitF)       NULL, 
    (ModExitF)       NULL, 
    (ModReadWcollF)  NULL, 
    (ModPostOpF)     mqcmd_postop,
};

/*
 *  Export rcmd module operations
 */
struct pdsh_rcmd_operations mqcmd_rcmd_ops = {
    (RcmdInitF)  mqcmd_init,
    (RcmdSigF)   mqcmd_signal,
    (RcmdF)      mqcmd,
};

/* 
 * Export module options
 */
struct pdsh_module_option mqcmd_module_options[] =
{ { 'm', "block|cyclic", "(mqshell) control assignment of procs to nodes",
    DSH, (optFunc) mqcmd_opt_m },
  { 'n', "n",            "(mqshell) set number of tasks per node",
    DSH, (optFunc) mqcmd_opt_n },
  { 'r', "railmask",     "(mqshell) set rail bitmask for job on multirail system",
    DSH, (optFunc) mqcmd_opt_r }, 
  PDSH_OPT_TABLE_END
};

/* 
 * Mqcmd module info 
 */
struct pdsh_module pdsh_module_info = {
    "rcmd",
    "mqsh",
    "Jim Garlick <garlick1@llnl.gov>",
    "Run MPI jobs over QsNet with mrsh authentication",
    DSH, 

    &mqcmd_module_ops,
    &mqcmd_rcmd_ops,
    &mqcmd_module_options[0],
};

static int
mqcmd_opt_m(opt_t *pdsh_opts, int opt, char *arg)
{
    if (strcmp(arg, "block") == 0)
        cyclic = false;
    else if (strcmp(arg, "cyclic") == 0)
        cyclic = true;
    else
        return -1;

    dist_set = true;

    return 0;
}

static int
mqcmd_opt_n(opt_t *pdsh_opts, int opt, char *arg)
{
    nprocs = atoi(arg);
    return 0;
}

static int
mqcmd_opt_r(opt_t *pdsh_opts, int opt, char *arg)
{
    char *p = NULL;
    long int val = strtol (arg, &p, 0);

    if (*p != '\0')
        errx ("%p: Invalid value for railmask: \"%s\"\n", arg);

    railmask = (unsigned int) val;
    railmask_set = true;
    return (0);
}

static int mqcmd_postop(opt_t *opt)
{
    int errors = 0;

    if (strcmp(opt->rcmd_name, "mqsh") == 0) {
        if (opt->fanout != DFLT_FANOUT && opt->wcoll != NULL) {
            if  (opt->fanout != hostlist_count(opt->wcoll)) {
                err("%p: mqcmd: fanout must = target node list length " 
                    "with -R mqsh\n");
                errors++;
            }
        }
        if (nprocs <= 0) {
            err("%p: -n should be > 0\n");
            errors++;
        }
        if ((railmask == 0) || (railmask > QSW_RAILMASK_MAX)) {
            err ("%p: mqcmd: invalid value %d for -r railmask\n", railmask);
            errors++;
        }
    } else {
        if (nprocs != 1) {
            err("%p: mqcmd: -n can only be specified with -R mqsh\n");
            errors++;
        }

        if (dist_set) {
            err("%p: mqcmd: -m may only be specified with -R mqsh\n");
            errors++;
        }

        if (railmask_set) {
            err("%p: mqcmd: -r may only be specified with -R mqsh\n");
            errors++;
        }
    }

    return errors;
}

static int
_mqcmd_opt_init(opt_t *opt)
{
    if (opt->fanout == DFLT_FANOUT && opt->wcoll != NULL)
        opt->fanout = hostlist_count(opt->wcoll);
    else {
        err("%p: mqcmd: Unable to set appropriate fanout\n");
        return -1;
    }

    opt->labels       = false;
    opt->kill_on_fail = true;

    if (opt->dshpath != NULL)
        Free((void **) &opt->dshpath);

    return 0;
}

/*
 * Intialize elan capability and info structures that will be used when
 * running the job.
 *  wcoll (IN)  list of nodes
 */
static int mqcmd_init(opt_t * opt)
{
    int totprocs = nprocs * hostlist_count(opt->wcoll);
    int rv, rand_fd;

    /*
     * Drop privileges if running setuid root
     */
    if ((geteuid() == 0) && (getuid() != 0))
        setuid (getuid ());

    /*
     *  Verify constraints for running Elan jobs
     *    and initialize options.
     */
    if (_mqcmd_opt_init(opt) < 0)
        return -1;

    if (getcwd(cwd, sizeof(cwd)) == NULL) {      /* cache working directory */
        err("%p: mqcmd: getcwd failed: %m\n");
        return -1;
    }

    if (qsw_init() < 0)
        exit(1);

    /* initialize Elan capability structure. */
    if (qsw_init_capability(&cap, totprocs, opt->wcoll, cyclic, railmask) < 0) {
        err("%p: mqcmd: failed to initialize Elan capability\n");
        return -1;
    }

    qsw_fini();

    /* initialize elan info structure */
    qinfo.prgnum = qsw_get_prgnum();    /* call after qsw_init_capability */
    qinfo.nnodes = hostlist_count(opt->wcoll);
    qinfo.nprocs = totprocs;
    qinfo.nodeid = qinfo.procid = qinfo.rank = 0;

    /*
     * Generate a random number to send in our package to the 
     * server.  We will see it again and compare it when the
     * server sets up the stderr socket and sends it to us.
     * We need to loop for the tiny possibility we read 0 :P
     */
    if ((rand_fd = open ("/dev/urandom", O_RDONLY | O_NONBLOCK)) < 0 ) {
        err("%p: mqcmd: Open of /dev/urandom failed\n");
        return -1;
    }

    do {
        if ((rv = read (rand_fd, &randy, sizeof(uint32_t))) < 0) {
            close(rand_fd);
            err("%p: mqcmd: Read of /dev/urandom failed\n");
            return -1;
        }

        if (rv < (int) (sizeof(uint32_t))) {
            close(rand_fd);
            err("%p: mqcmd: Read returned too few bytes\n");
            return -1;
        }
    } while (randy == 0);

    close(rand_fd);

    return 0;
}

static int
mqcmd_signal(int fd, void *arg, int signum)
{
    char c;

    if (fd >= 0) {
        /* set non-blocking mode for write - just take our best shot */
        if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
            err("%p: fcntl: %m\n");
        c = (char) signum;
        write(fd, &c, 1);
    }
    return 0;
}

/*
 * Send extra arguments to qshell server
 *  s (IN)      socket
 *  nodeid (IN) node index for this connection
 */
static int _mqcmd_send_extra_args(int s, int nodeid, const char *ahost)
{
    char **ep;
    char tmpstr[1024];
    int count = 0;
    int i;

    /* send current working dir */
    if (fd_write_n(s, cwd, strlen(cwd) + 1) < 0) {
        err("%p: %S: error writing cwd: %m\n", ahost);
        return -1;
    }

    /* send environment (count followed by variables, each \0-term) */
    for (ep = environ; *ep != NULL; ep++)
        count++;

    snprintf(tmpstr, sizeof(tmpstr), "%d", count);
    if (fd_write_n(s, tmpstr, strlen(tmpstr) + 1) < 0) {
        err("%p: %S: error writing envcount: %m\n", ahost);
        return -1;
    }

    for (ep = environ; *ep != NULL; ep++) {
        if (fd_write_n(s, *ep, strlen(*ep) + 1) < 0) {
            err("%p: %S: error writing environemtn: %m\n", ahost);
            return -1;
        }
    }

    /* send elan capability */
    if (qsw_encode_cap(tmpstr, sizeof(tmpstr), &cap) < 0)
        return -1;

    if (fd_write_n(s, tmpstr, strlen(tmpstr) + 1) < 0) {
        err("%p: %S: error writing elan capability: %m\n", ahost);
        return -1;
    }

    for (i = 0; i < qsw_cap_bitmap_count(); i += 16) {
        if (qsw_encode_cap_bitmap(tmpstr, sizeof(tmpstr), &cap, i) < 0)
            return -1;

        if (fd_write_n(s, tmpstr, strlen(tmpstr) + 1) < 0) {
            err("%p: %S: error writing bitmap: %m\n", ahost);
            return -1;
        }
    }

    /* send elan info */
    qinfo.nodeid = qinfo.rank = qinfo.procid = nodeid;
    if (qsw_encode_info(tmpstr, sizeof(tmpstr), &qinfo) < 0)
        return -1;

    if (fd_write_n(s, tmpstr, strlen(tmpstr) + 1) < 0) {
        err("%p: %S: error writing qinfo: %m\n", ahost);
        return -1;
    }

    return 0;
}

/*
 * Derived from the rcmd() libc call, with modified interface.
 * This version is MT-safe.  Errors are displayed in pdsh-compat format.
 * Connection can time out.
 *      ahost (IN)              target hostname
 *      addr (IN)               4 byte internet address
 *      locuser (IN)            not used 
 *      remuser (IN)            remote username
 *      cmd (IN)                remote command to execute under shell
 *      int nodeid (IN)         node index for this connection
 *      fd2p (IN)               if non NULL, return stderr file descriptor here
 *      int (RETURN)            -1 on error, socket for I/O on success
 *
 * Combination of code derived from mcmd by Mike Haskell, qcmd by
 * Jim Garlick, and a variety of minor modifications.
 */
static int 
mqcmd(char *ahost, char *addr, char *locuser, char *remuser, char *cmd, 
        int nodeid, int *fd2p, void **arg)
{
    struct sockaddr m_socket;
    struct sockaddr_in *getp;
    struct sockaddr_in sin, from;
    struct sockaddr_storage ss;
    struct in_addr m_in;
    unsigned int rand, randl;
    unsigned char *hptr;
    int s, s2, rv, mcount, lport;
    char c;
    char num[6] = {0};
    char *mptr;
    char *mbuf;
    char *tmbuf;
    char *m;
    char *mpvers;
    char num_seq[12] = {0};
    socklen_t len;
    sigset_t blockme;
    sigset_t oldset;
    char haddrdot[16] = {0};
    munge_ctx_t ctx;
    struct xpollfd xpfds[2];

    sigemptyset(&blockme);
    sigaddset(&blockme, SIGURG);
    sigaddset(&blockme, SIGPIPE);
    SET_PTHREAD();

    if (( rv = strcmp(ahost,"localhost")) == 0 ) {
        errno = EACCES;
        err("%p: %S: mqcmd: Can't use localhost\n", ahost);
        EXIT_PTHREAD();
    }

    /* Convert randy to decimal string, 0 if we dont' want stderr */
    if (fd2p != NULL)
        snprintf(num_seq, sizeof(num_seq),"%d",randy);
    else
        snprintf(num_seq, sizeof(num_seq),"%d",0);

    /*
     * Start setup of the stdin/stdout socket...
     */
    lport = 0;
    len = sizeof(struct sockaddr_in);

    if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        err("%p: %S: mqcmd: socket call stdout failed: %m\n", ahost);
        EXIT_PTHREAD();
    }

    memset (&ss, '\0', sizeof(ss));
    ss.ss_family = AF_INET;

    if (bind(s, (struct sockaddr *)&ss, len) < 0) {
        err("%p: %S: mqcmd: bind failed: %m\n", ahost);
        goto bad;
    }

    sin.sin_family = AF_INET;

    memcpy(&sin.sin_addr.s_addr, addr, IP_ADDR_LEN); 

    sin.sin_port = htons(MQSH_PORT);
    if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        err("%p: %S: mqcmd: connect failed: %m\n", ahost);
        goto bad;
    }

    lport = 0;
    s2 = -1;
    if (fd2p != NULL) {
        /*
         * Start the socket setup for the stderr.
         */
        struct sockaddr_in sin2;

        if ((s2 = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            err("%p: %S: mqcmd: socket call for stderr failed: %m\n", ahost);
            goto bad;
        }

        memset (&sin2, 0, sizeof(sin2));
        sin2.sin_family = AF_INET;
        sin2.sin_addr.s_addr = htonl(INADDR_ANY);
        sin2.sin_port = 0;

        if (bind(s2, (struct sockaddr *)&sin2, sizeof(sin2)) < 0) {
            err("%p: %S: mqcmd: bind failed: %m\n", ahost);
            close(s2);
            goto bad;
        }

        len = sizeof(struct sockaddr);

        /*
         * Retrieve our port number so we can hand it to the server
         * for the return (stderr) connection...
         */

        /* getsockname is thread safe */
        if (getsockname(s2,&m_socket,&len) < 0) {
            err("%p: %S: mqcmd: getsockname failed: %m\n", ahost);
            close(s2);
            goto bad;
        }

        getp = (struct sockaddr_in *)&m_socket;
        lport = ntohs(getp->sin_port);

        if (listen(s2, 1) < 0) {
            err("%p: %S: mqcmd: listen() failed: %m\n", ahost);
            close(s2);
            goto bad;
        }
    }

    snprintf(num,sizeof(num),"%d",lport);
    memcpy(&m_in.s_addr, addr, IP_ADDR_LEN);

    /* inet_ntoa is not thread safe, so we use the following,
     * which is more or less ripped from glibc
     */
    hptr = (unsigned char *)&m_in;
    sprintf(haddrdot, "%u.%u.%u.%u", hptr[0], hptr[1], hptr[2], hptr[3]);

    /*
     * We call munge_encode which will take what we write in and return a
     * pointer to an munged buffer.  What we get back is a null terminated
     * string of encrypted characters.
     * 
     * The format of the unmunged buffer is as follows (each a string terminated 
     * with a '\0' (null):
     *
     * stderr_port_number & /dev/urandom_client_produce_number are 0
     * if user did not request stderr socket
     *
     *                                              SIZE            EXAMPLE
     *                                              ==========      =============
     * remote_user_name                             variable        "mhaskell"
     * '\0'
     * dotted_decimal_address_of_this_server        7-15 bytes      "134.9.11.155"
     * '\0'
     * stderr_port_number                           4-8 bytes       "50111"
     * '\0'
     * /dev/urandom_client_produced_number          1-8 bytes       "1f79ca0e"
     * '\0'
     * users_command                                variable        "ls -al"
     * '\0' '\0'
     *
     * (The last extra null is accounted for in the following line's last strlen() call.)
     */


    mcount = ((strlen(remuser)+1) + (strlen(haddrdot)+1) + (strlen(num)+1) + 
            (strlen(num_seq)+1) + strlen(cmd)+2);
    tmbuf = mbuf = malloc(mcount);
    if (tmbuf == NULL) {
        err("%p: %S: mqcmd: Error from malloc\n", ahost);
        close(s2);
        goto bad;
    }
    /*
     * The following memset() call takes the extra trailing null as part of its
     * count as well.
     */
    memset(mbuf,0,mcount);

    mptr = strcpy(mbuf, remuser);
    mptr += strlen(remuser)+1;
    mptr = strcpy(mptr, haddrdot);
    mptr += strlen(haddrdot)+1;
    mptr = strcpy(mptr, num);
    mptr += strlen(num)+1;
    mptr = strcpy(mptr, num_seq);
    mptr += strlen(num_seq)+1;
    mptr = strcpy(mptr, cmd);

    if ((ctx = munge_ctx_create()) == NULL)
        goto bad;

    if ((rv = munge_encode(&m,0,mbuf,mcount)) != EMUNGE_SUCCESS) {
        err("%p: %S: mqcmd: munge_encode: %S\n", ahost, munge_ctx_strerror(ctx));
        munge_ctx_destroy(ctx);
        close(s2);
        free(tmbuf);
        goto bad;
    }

    munge_ctx_destroy(ctx);
 
    mcount = (strlen(m)+1);

    /*
     * Write stderr port in the clear in case we can't decode for
     * some reason (i.e. bad credentials).  May be 0 if user
     * doesn't want stderr
     */
    if (fd2p != NULL) {
        rv = fd_write_n(s, num, strlen(num)+1);
        if (rv != (strlen(num)+1)) {
            free(m);
            free(tmbuf);
            if (errno == EPIPE)
                err("%p: %S: mqcmd: Lost connection (EPIPE): %m\n", ahost);
            else
                err("%p: %S: mqcmd: Write of stderr port failed: %m\n", ahost);
            close(s2);
            goto bad;
        }
    } else {
        write(s, "", 1);
        lport = 0;
    }

    /*
     * Write the munge_encoded blob to the socket.
     */
    rv = fd_write_n(s, m, mcount);
    if (rv != mcount) {
        free(m);
        free(tmbuf);
        if (errno == EPIPE)
            err("%p: %S: mqcmd: Lost connection: %m\n", ahost);
        else
            err("%p: %S: mqcmd: Write to socket failed: %m\n", ahost);
        close(s2);
        goto bad;
    }

    free(m);
    free(tmbuf);

    if (fd2p != NULL) {
        /*
         * Wait for stderr connection from daemon.
         */
        int s3;
      
        errno = 0;
        xpfds[0].fd = s;
        xpfds[1].fd = s2;
        xpfds[0].events = xpfds[1].events = XPOLLREAD;
        if (  ((rv = xpoll(xpfds, 2, -1)) < 0) 
            || rv != 1 
            || (xpfds[0].revents > 0)) {
            if (errno != 0)
                err("%p: %S: mqcmd: xpoll (setting up stderr): %m\n", ahost);
            else
                err("%p: %S: mqcmd: xpoll: protocol failure in circuit setup\n", ahost);
            (void) close(s2);
            goto bad;
        }

        errno = 0;
        len = sizeof(from); /* arg to accept */

        if ((s3 = accept(s2, (struct sockaddr *)&from, &len)) < 0) {
            close(s2);
            err("%p: %S: mqcmd: accept (stderr) failed: %m\n", ahost);
            goto bad;
        }

        if (from.sin_family != AF_INET) {
            err("%p: %S: mqcmd: bad family type: %d\n", ahost, from.sin_family);
            goto bad2;
        }

        close(s2);

        /*
         * The following fixes a race condition between the daemon
         * and the client.  The daemon is waiting for a null to
         * proceed.  We do this to make sure that we have our
         * socket is up prior to the daemon running the command.
         */
        if (write(s,"",1) < 0) {
            err("%p: %S: mqcmd: Could not communicate to daemon to proceed: %m\n", ahost);
            close(s3);
            goto bad;
        }

        /*
         * Read from our stderr.  The server should have placed our random number
         * we generated onto this socket.
         */
        rv = fd_read_n(s3, &rand, sizeof(rand));
        if (rv != (ssize_t) (sizeof(rand))) {
            err("%p: %S: mqcmd: Bad read of expected verification "
                    "number off of stderr socket: %m\n", ahost);
            close(s3);
            goto bad;
        }

        randl = ntohl(rand);
        if (randl != randy) {
            char tmpbuf[LINEBUFSIZE] = {0};
            char *tptr = &tmpbuf[0];

            memcpy(tptr,(char *) &rand,sizeof(rand));
            tptr += sizeof(rand);
            rv = fd_read_line (s3, tptr, LINEBUFSIZE);
            if (rv < 0)
                err("%p: %S: mqcmd: Bad read of error from stderr: %m\n", ahost);
            else
                err("%p: %S: mqcmd: %s\n", ahost, &tmpbuf[0]);
            close(s3);
            goto bad;
        }

        /*
         * Set the stderr file descriptor for the user...
         */
        *fd2p = s3;
    }

    /* send extra information */
    if (_mqcmd_send_extra_args(s, nodeid, ahost) < 0) {
        err("%p: %S: mqcmd: error sending extra args\n", ahost);
        goto bad2;
    }

    if ((rv = read(s, &c, 1)) < 0) {
        err("%p: %S: mqcmd: read: protocol failure: %m\n", ahost);
        goto bad2;
    }

    if (rv != 1) {
        err("%p: %S: mqcmd: read: protocol failure: invalid response\n", ahost);
        goto bad2;
    }

    if (c != '\0') {
        /* retrieve error string from remote server */
        char tmpbuf[LINEBUFSIZE];

        if (fd_read_line (s, &tmpbuf[0], LINEBUFSIZE) < 0)
            err("%p: %S: mqcmd: Error from remote host\n", ahost);
        else
            err("%p: %S: %s\n", ahost, tmpbuf);
        goto bad2;
    }
    RESTORE_PTHREAD();

    return (s);

bad2:
    if (lport)
        close(*fd2p);
bad:
    close(s);
    EXIT_PTHREAD();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
