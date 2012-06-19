/*
 */

#ifndef _COMMON_H
#define _COMMON_H

/* prototypes */
/* log.c */
void		 log_init(int);
void		 vlog(int, const char *, va_list);
void		 log_warn(const char *, ...);
void		 log_warnx(const char *, ...);
void		 log_info(const char *, ...);
void		 log_debug(const char *, ...);
void		 fatal(const char *);
void		 fatalx(const char *);
const char	*log_sockaddr(struct sockaddr *);

/* strtonum.c */
long long	 strtonum(const char *, long long, long long, const char **);

#endif
