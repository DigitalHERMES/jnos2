#ifndef _MAILUTIL_H
#define _MAILUTIL_H
  
#include <stdio.h>
  
/* Header types.  Must correspond with Hdrs[] definition in index.c */
#define NOHEADER    -1
#define APPROVED    0
#define FROM        1
#define TO      2
#define DATE        3
#define MSGID       4
#define SUBJECT     5
#define RECEIVED    6
#define SENDER      7
#define REPLYTO     8
#define STATUS      9
#define BBSTYPE     10
#define XFORWARD    11
#define CC      12
#define RRECEIPT    13
#define APPARTO     14
#define ERRORSTO    15
#define ORGANIZATION    16
#define NEWSGROUPS  17
#define PATH        18
#define XBBSHOLD    19
#define UNKNOWN     20
  
extern char *Hdrs[];
extern char shortversion[];
extern char *Mbfwdinfo;

/* codes to indicate which rewrite file to use in rewrite_address: */
#define REWRITE_TO 0
#define REWRITE_FROM_TO 2
#define REWRITE_FROM 1
  
/* in mailutil.c */
int recvmail __ARGS((int s, char *buf, unsigned len, FILE *fp, int trace));
int copymail __ARGS((char *filename,char *buf,unsigned len, FILE *fp, int trace));
int mlock __ARGS((char *dir,char *id));
int rmlock __ARGS((char *dir,char *id));
char *rewrite_address __ARGS((char *addr, unsigned int filecode));

/* in index.c */
char *getname __ARGS((char *cp));
char *getaddress __ARGS((char *string,int cont));
int htype __ARGS((char *s, int *prevtype));
  
/* in smtpserv.c */
int mailuser __ARGS((FILE *data,char *from,char *to));

#endif  /* _MAILUTIL_H */
