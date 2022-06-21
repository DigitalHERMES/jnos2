/* AX25 link callsign monitoring. Also contains beginnings of
 * an automatic link quality monitoring scheme (incomplete)
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <ctype.h>
#include "global.h"
#include <time.h>
#ifdef AX25
#include "mbuf.h"
#include "iface.h"
#include "ax25.h"
#include "ip.h"
#include "timer.h"
#include "pktdrvr.h"
  
#define iscallsign(c) ((isupper(c)) || (isdigit(c)) || (c ==' '))
int axheard_filter_flag = AXHEARD_PASS;
 
#ifndef	KLM_HLREVAMP_2022
 
static struct lq *al_create __ARGS((struct iface *ifp,char *addr));
static struct ld *ad_lookup __ARGS((struct iface *ifp,char *addr,int sort));
static struct ld *ad_create __ARGS((struct iface *ifp,char *addr));
struct lq *Lq;
struct ld *Ld;

/*
 * 05Apr2021, Maiko (VE4KLM), interested in listing digipeated (via, not direct) calls
 * 15Apr2021, Maiko (VE4KLM), added digipeater address to lookup, we need to be able to
 * see if a callsign used multiple digipeaters ... bit of an oops ...
 */
static struct lv *av_lookup __ARGS((struct iface *ifp,char *addr, char *digiaddr, int sort));
static struct lv *av_create __ARGS((struct iface *ifp,char *addr, char *digiaddr));	/* 06Apr2021, Maiko, extra param now */
struct lv *Lv;

#else

/*
 * 04Mar2022, Maiko (VE4kLM), I must be (I am) nuts, yup, need to use pointers
 * to pointers to get this new heard list stuff working the way I want it too,
 * you would think after 30 years of C, nope still struggle with this stuff !
 *
 */
       struct lq *al_NEWlookup (struct lq**, char*, int);
static struct lq *al_NEWcreate (struct lq**, char*);

static struct ld *ad_NEWlookup (struct ld**, char*, int);
static struct ld *ad_NEWcreate (struct ld**, char*);

static struct lv *av_NEWlookup (struct lv**, char*, char*, int);
static struct lv *av_NEWcreate (struct lv**, char*, char*);

/*
 * 04Mar20222, Maiko (VE4KLM), idea - sysop can now define multiple heard lists
 * for a single port, switching to any list if they (for instance) change freq
 * on that sole port, so they don't mix calls from different freqs or bands, or
 * you can use a particular heard list on multiple ports as well in the end.
 *  (meaning any of these heard lists can be switched into any port any time)
 *
 *  some old structures for reference, originally idea, but squashed :]
 *
	typedef struct hl17m30m {
        	struct lq *lp;
		char *hlname;
	} HL17M30M;

	static HL17M30M *pseudoHL = (HL17M30M*)0;
 *
 * Using #define KLM_HLREVAMP_2022 to encapsulte the new (code) way of doing it
 *
 *
 * 13Mar2022, Maiko (VE4KLM), New function to implement this, after having
 * spent a bit of time revamping the existing code, to be able to do this.
 */

typedef struct hl17m30m {

	char *hlname;

	/*
	 * So if I already have these 3 ptrs in here, then why bother defining
	 * them in the iface structure ? Why not just index to this list ?
	 *
	 * Having to do a lookup wastes CPU time, but the bigger thing to keep
	 * in mind is this list is only used for newly defined heardlists. The
	 * default heard list for any particular iface is now directly linked
	 * to the iface structure (there is no more Lq, Lp, Lv), and probably
	 * most of the time, sysops will stick with the default.
	 *
	 * If you create (rather switch) to a newly defined list, it winds up
	 * on this list, along with a 2nd entry (backup copy) of the existing
	 * 3 ptrs inside the iface structure for that interface, then copies
	 * the newly created list pointers to that same iface structure.
	 *
	 * This allows the sysop to switch back to the default pointers, or to
	 * any other newly defined lists - by simply swapping the 3 pointers.
	 *  (looks like I had this structure already defined on 07Mar2022)
	 */
	struct lq *hrdsrc;
	struct ld *hrddst;
	struct lv *hrdvia;

	/* 13Mar2022, Maiko, after thought, need to bypass if_lookup in some cases */
	struct iface *iface;

	struct hl17m30m *next;	/* this list is a link list */

} HL17M30M;

static HL17M30M *hl17m30m = (HL17M30M*)0;

hl12m17m30m_switch (struct iface *iface, char *hlname)
{
	HL17M30M *ptr = hl17m30m;

	while (ptr)
	{
		if (!strcmp (hlname, ptr->hlname))
			break;

		ptr = ptr->next;
	}

	if (!ptr)
	{
		/* Could not find the list asked for, so create it */

		ptr = malloc (sizeof(HL17M30M));	

		ptr->hlname = j2strdup (hlname);

		ptr->hrdsrc = (struct lq*)0;
		ptr->hrddst = (struct ld*)0;
		ptr->hrdvia = (struct lv*)0;

		ptr->iface = iface;		/* cause we have to bypass if_lookup sometimes */

		ptr->next = hl17m30m;

		hl17m30m = ptr;

		/*
		 * Since we are replacing the default list, we also need to
		 * create an entry (using the interface name) to save those
	 	 * original pointers incase we want to switch back to them.
		 *
		 */

		HL17M30M *ptr2 = hl17m30m;

		while (ptr2)
		{
			if (!strcmp (iface->name, ptr2->hlname))
				break;

			ptr2 = ptr2->next;
		}

		if (!ptr2)
		{
			/* save the original pointers, but only do this once ! */

			ptr2 = malloc (sizeof(HL17M30M));	

			ptr2->hlname = j2strdup (iface->name);

			ptr2->hrdsrc = iface->hrdsrc;
			ptr2->hrddst = iface->hrddst;
			ptr2->hrdvia = iface->hrdvia;

			ptr2->iface = iface;		/* cause we have to bypass if_lookup sometimes */

			ptr2->next = hl17m30m;

			hl17m30m = ptr2;
		}
	}	

	/* now tell the interface to point to the new list */

	iface->hrdsrc = ptr->hrdsrc;
	iface->hrddst = ptr->hrddst;
	iface->hrdvia = ptr->hrdvia;

	return 0;
}

/*
 * 13Mar2022, Maiko (VE4KLM), When user wants to display heard lists, we need
 * now be able to make sure there is one in our new list, before checking the
 * validity of the actual physical interface, since heard lists are no longer
 * an interface name, they can be just a heard list, not a port anymore, yup.
 *   (at this point, I decided to add a ptr to the iface struct, since I
 *     will need it anyway for the original code, so this works out nice)
 */
struct iface *hl12m17m30m_lookup (char *hlname)
{
	HL17M30M *ptr = hl17m30m;

	while (ptr)
	{
		if (!strcmp (hlname, ptr->hlname))
			break;

		ptr = ptr->next;
	}

	if (ptr) return ptr->iface;

	return (struct iface*)0;
}

#endif	/* end of KLM_HLREVAMP_2022 */

/*
 * 06Apr2021, Maiko (VE4KLM), attempts to reduce duplicate code
 *
 *  1) simplistic callsign validation
 */

static int checkcall (char *addr)
{
	register unsigned char c;

	register int i = 0;

	while(i < AXALEN-1){
		c = *(addr+i);
		c >>= 1;
		if(!iscallsign(c))
			return 0;
		i++;
	}

	return 1;
}
  
#ifdef  notdef
/* Send link quality reports to interface */
void
genrpt(ifp)
struct iface *ifp;
{
    struct mbuf *bp;
    register char *cp;
    int i;
    struct lq *lp;
    int maxentries,nentries;
  
    maxentries = (Paclen - LQHDR) / LQENTRY;
    if((bp = alloc_mbuf(Paclen)) == NULLBUF)
        return;
    cp = bp->data;
    nentries = 0;
  
    /* Build and emit header */
    cp = putlqhdr(cp,LINKVERS,Ip_addr);
  
    /* First entry is for ourselves. Since we're examining the Axsent
     * variable before we've sent this frame, add one to it so it'll
     * match the receiver's count after he gets this frame.
     */
    cp = putlqentry(cp,ifp->hwaddr,Axsent+1);
    nentries++;
  
    /* Now add entries from table */
    for(lp = lq;lp != NULLLQ;lp = lp->next){
        cp = putlqentry(cp,&lp->addr,lp->currxcnt);
        if(++nentries >= MAXENTRIES){
            /* Flush */
            bp->cnt = nentries*LQENTRY + LQHDR;
            ax_output(ifp,Ax25multi[0],ifp->hwaddr,PID_LQ,bp);
            if((bp = alloc_mbuf(Paclen)) == NULLBUF)
                return;
            cp = bp->data;
        }
    }
    if(nentries > 0){
        bp->cnt = nentries*LQENTRY + LQHDR;
        ax_output(ifp,Ax25multi[0],ifp->hwaddr,LQPID,bp);
    } else {
        free_p(bp);
    }
}
  
/* Pull the header off a link quality packet */
void
getlqhdr(hp,bpp)
struct lqhdr *hp;
struct mbuf **bpp;
{
    hp->version = pull16(bpp);
    hp->ip_addr = pull32(bpp);
}
  
/* Put a header on a link quality packet.
 * Return pointer to buffer immediately following header
 */
char *
putlqhdr(cp,version,ip_addr)
register char *cp;
int16 version;
int32 ip_addr;
{
    cp = put16(cp,version);
    return put32(cp,ip_addr);
}
  
/* Pull an entry off a link quality packet */
void
getlqentry(ep,bpp)
struct lqentry *ep;
struct mbuf **bpp;
{
    pullup(bpp,ep->addr,AXALEN);
    ep->count = pull32(bpp);
}
  
/* Put an entry on a link quality packet
 * Return pointer to buffer immediately following header
 */
char *
putlqentry(cp,addr,count)
char *cp;
char *addr;
int32 count;
{
    memcpy(cp,addr,AXALEN);
    cp += AXALEN;
    return put32(cp,count);
}
#endif

  
/* Log the source address of an incoming packet */
void
logsrc(ifp,addr)
struct iface *ifp;
char *addr;
{
    register struct lq *lp;
  
    if(axheard_filter_flag & AXHEARD_NOSRC || !(ifp->flags & LOG_AXHEARD))
        return;

	/* 06Apr2021, Maiko, new function to cut code duplication */
	if (!checkcall (addr))
		return;
  
#ifndef KLM_HLREVAMP_2022
    if((lp = al_lookup(ifp,addr,1)) == NULLLQ)
        if((lp = al_create(ifp,addr)) == NULLLQ)
            return;
#else
    if ((lp = al_NEWlookup (&(ifp->hrdsrc), addr, 1)) == NULLLQ)
        if ((lp = al_NEWcreate (&(ifp->hrdsrc), addr)) == NULLLQ)
            return;
#endif

    lp->currxcnt++;
    lp->time = secclock();
}

/* Log the destination address of an incoming packet */
void
logdest(ifp,addr)
struct iface *ifp;
char *addr;
{
    register struct ld *lp;
  
    if(axheard_filter_flag & AXHEARD_NODST || !(ifp->flags & LOG_AXHEARD))
        return;

	/* 06Apr2021, Maiko, new function to cut code duplication */
	if (!checkcall (addr))
		return;
  
#ifndef KLM_HLREVAMP_2022
    if((lp = ad_lookup(ifp,addr,1)) == NULLLD)
        if((lp = ad_create(ifp,addr)) == NULLLD)
            return;
#else
    if ((lp = ad_NEWlookup (&(ifp->hrddst), addr, 1)) == NULLLD)
        if ((lp = ad_NEWcreate (&(ifp->hrddst), addr)) == NULLLD)
            return;
#endif

    lp->currxcnt++;
    lp->time = secclock();
}

/*
 * 05Apr2021, Maiko (VE4KLM), Log the source address of an digipeated incoming packet
 */
void logDigisrc (struct iface *ifp, char *addr, char *digiaddr)
{
    register struct lv *lv;

    if (axheard_filter_flag & AXHEARD_NOSRC || !(ifp->flags & LOG_AXHEARD))
        return;

	/* 06Apr2021, Maiko, new function to cut code duplication */
	if (!checkcall (addr))
		return;

	/* 06Apr2021, Maiko, should probably check the digi call as well */
	if (!checkcall (digiaddr))
		return;

#ifndef KLM_HLREVAMP_2022
    /*
     * 05Apr2021, Maiko (VE4KLM) digiaddr is not used yet, this is just playing around,
     * okay modified lv structure 06Apr2021, now using digicall so we can display it.
     *
     * 15Apr2021, Maiko (observation) - if a call exists already, and we come in via
     * another digipeater we should create another new entry, right now it doesn't,
     * it does update the count and time but from the original digipeater entry :|
     *  (meaning we need a digi parameter for av_lookup() function - done)
     *
     * Interesting this 'sort' flag, hmmm, my sort function is 'not working' ?
     *
     */

    if((lv = av_lookup(ifp,addr,digiaddr,1)) == NULLLV)
        if((lv = av_create(ifp,addr, digiaddr)) == NULLLV)
            return;
#else
    if ((lv = av_NEWlookup (&(ifp->hrdvia), addr, digiaddr, 1)) == NULLLV)
        if ((lv = av_NEWcreate (&(ifp->hrdvia), addr, digiaddr)) == NULLLV)
            return;
#endif

    lv->currxcnt++;
    lv->time = secclock();
}

extern int Maxax25heard;

extern int numal, numad;

#ifdef KLM_HLREVAMP_2022

/* new revamped heard (link) list functions */

struct lq *al_NEWlookup (struct lq **rootptr, char *addr, int sort)
{
	register struct lq *lp;

	struct lq *lplast = NULLLQ;

	for (lp = *rootptr; lp != NULLLQ; lplast = lp, lp = lp->next)
	{
		if (addreq (lp->addr, addr))
		{
			if (sort && lplast != NULLLQ)
			{
                /* Move entry to top of list */
                lplast->next = lp->next;
                lp->next = *rootptr;
                *rootptr = lp;
            }

            return lp;
        }
    }

    return NULLLQ;
}

static struct lq *al_NEWcreate (struct lq **rootptr, char *addr)
{
    register struct lq *lp;

    struct lq *lplast = NULLLQ;
  
    if (Maxax25heard && numal == Maxax25heard)
	{
        /* find and use last one in list */
        for (lp = *rootptr; lp->next != NULLLQ; lplast = lp, lp = lp->next);

        /* delete entry from end */
        if(lplast)
            lplast->next = NULLLQ;
        else    /* Only one entry, and maxax25heard = 1 ! */
            *rootptr = NULLLQ;

        lp->currxcnt = 0;

    } else {    /* create a new entry */
        numal++;
        lp = (struct lq *)callocw(1,sizeof(struct lq));
    }
    memcpy(lp->addr,addr,AXALEN);
    // lp->iface = ifp;
    lp->next = *rootptr;
    *rootptr = lp;
  
    return lp;
}

static struct ld *ad_NEWlookup (struct ld **rootptr, char *addr, int sort)
{
	register struct ld *lp;

	struct ld *lplast = NULLLD;
 
	for(lp = *rootptr; lp != NULLLD; lplast = lp, lp = lp->next)
	{
		if (addreq (lp->addr, addr))
		{
			if (sort && lplast != NULLLD)
			{
                /* Move entry to top of list */
                lplast->next = lp->next;
                lp->next = *rootptr;
                *rootptr = lp;
            }

            return lp;
        }
    }

    return NULLLD;
}

static struct ld *ad_NEWcreate (struct ld **rootptr, char* addr)
{
	register struct ld *lp;

	struct ld *lplast = NULLLD;
  
	if (Maxax25heard && numad == Maxax25heard)	/* find and use last one in list */
	{
		for (lp = *rootptr; lp->next != NULLLD; lplast = lp, lp = lp->next);

        /* delete entry from end */
        if(lplast)
            lplast->next = NULLLD;
        else
            *rootptr = NULLLD;

        lp->currxcnt = 0;

    } else {    /* create a new entry */
        numad++;
        lp = (struct ld *)callocw(1,sizeof(struct ld));
    }
    memcpy(lp->addr,addr,AXALEN);
    // lp->iface = ifp;
    lp->next = *rootptr;
    *rootptr = lp;
  
    return lp;
}

static struct lv * av_NEWlookup (struct lv **rootptr, char *addr, char *digiaddr, int sort)
{
	register struct lv *lv;

	struct lv *lvlast = NULLLV;
  
	for (lv = *rootptr; lv != NULLLV; lvlast = lv, lv = lv->next)
	{
		if (addreq (lv->addr, addr) && addreq (lv->digi, digiaddr))
		{
			if (sort && lvlast != NULLLV)
			{
                /* Move entry to top of list */
                lvlast->next = lv->next;
                lv->next = *rootptr;
                *rootptr = lv;
            }

            return lv;
        }
    }

    return NULLLV;
}

static struct lv * av_NEWcreate (struct lv **rootptr, char *addr, char *digiaddr)
{
    register struct lv *lv;

    struct lv *lvlast = NULLLV;
  
	if (Maxax25heard && numad == Maxax25heard)	/* find and use last one in list */
	{
		for (lv = *rootptr; lv->next != NULLLV; lvlast = lv, lv = lv->next);

        /* delete entry from end */
        if(lvlast)
            lvlast->next = NULLLV;
        else
            *rootptr = NULLLV;

        lv->currxcnt = 0;

    } else {    /* create a new entry */
        numad++;
        lv = (struct lv *)callocw(1,sizeof(struct lv));
    }
    memcpy(lv->addr,addr,AXALEN);
    memcpy(lv->digi,digiaddr,AXALEN);	/* 06Apr2021, Maiko */
    // lv->iface = ifp;
    lv->next = *rootptr;
    *rootptr = lv;
  
    return lv;
}

#else

/* Look up an entry in the source data base */
struct lq *
al_lookup(ifp,addr,sort)
struct iface *ifp;
char *addr;
int sort;
{
    register struct lq *lp;
    struct lq *lplast = NULLLQ;
  
    for(lp = Lq;lp != NULLLQ;lplast = lp,lp = lp->next){
        if((lp->iface == ifp) && addreq(lp->addr,addr)){
            if(sort && lplast != NULLLQ){
                /* Move entry to top of list */
                lplast->next = lp->next;
                lp->next = Lq;
                Lq = lp;
            }
            return lp;
        }
    }
    return NULLLQ;
}

/* Create a new entry in the source database */
/* If there are too many entries, override the oldest one - WG7J */
static struct lq *
al_create(ifp,addr)
struct iface *ifp;
char *addr;
{
    register struct lq *lp;
    struct lq *lplast = NULLLQ;
  
    if(Maxax25heard && numal == Maxax25heard) {
        /* find and use last one in list */
        for(lp = Lq;lp->next != NULLLQ;lplast = lp,lp = lp->next);
        /* delete entry from end */
        if(lplast)
            lplast->next = NULLLQ;
        else    /* Only one entry, and maxax25heard = 1 ! */
            Lq = NULLLQ;
        lp->currxcnt = 0;
    } else {    /* create a new entry */
        numal++;
        lp = (struct lq *)callocw(1,sizeof(struct lq));
    }
    memcpy(lp->addr,addr,AXALEN);
    lp->iface = ifp;
    lp->next = Lq;
    Lq = lp;
  
    return lp;
}
  
/* Look up an entry in the destination database */
static struct ld *
ad_lookup(ifp,addr,sort)
struct iface *ifp;
char *addr;
int sort;
{
    register struct ld *lp;
    struct ld *lplast = NULLLD;
  
    for(lp = Ld;lp != NULLLD;lplast = lp,lp = lp->next){
        if((lp->iface == ifp) && addreq(lp->addr,addr)){
            if(sort && lplast != NULLLD){
                /* Move entry to top of list */
                lplast->next = lp->next;
                lp->next = Ld;
                Ld = lp;
            }
            return lp;
        }
    }
    return NULLLD;
}
/* Create a new entry in the destination database */
static struct ld *
ad_create(ifp,addr)
struct iface *ifp;
char *addr;
{
    register struct ld *lp;
    struct ld *lplast = NULLLD;
  
    if(Maxax25heard && numad == Maxax25heard) { /* find and use last one in list */
        for(lp = Ld;lp->next != NULLLD;lplast = lp,lp = lp->next);
        /* delete entry from end */
        if(lplast)
            lplast->next = NULLLD;
        else
            Ld = NULLLD;
        lp->currxcnt = 0;
    } else {    /* create a new entry */
        numad++;
        lp = (struct ld *)callocw(1,sizeof(struct ld));
    }
    memcpy(lp->addr,addr,AXALEN);
    lp->iface = ifp;
    lp->next = Ld;
    Ld = lp;
  
    return lp;
}

/*
 * 05Apr2021, Maiko (VE4KLM), Look up an entry in the digipeated source database
 * 15Apr2021, Maiko, should be using callsign / digipeater combo to do lookup !
 */
static struct lv * av_lookup (struct iface *ifp, char *addr, char *digiaddr, int sort)
{
    register struct lv *lv;
    struct lv *lvlast = NULLLV;
  
    for(lv = Lv;lv != NULLLV;lvlast = lv,lv = lv->next){
        if((lv->iface == ifp) && addreq(lv->addr,addr) && addreq(lv->digi,digiaddr)){
            if(sort && lvlast != NULLLV){
                /* Move entry to top of list */
                lvlast->next = lv->next;
                lv->next = Lv;
                Lv = lv;
            }
            return lv;
        }
    }
    return NULLLV;
}

/*
 * 05Apr2021, Maiko (VE4KLM), Create a new entry in the digipeated source database
 */
static struct lv * av_create (struct iface *ifp, char *addr, char *digiaddr)
{
    register struct lv *lv;
    struct lv *lvlast = NULLLV;
  
    if(Maxax25heard && numad == Maxax25heard) { /* find and use last one in list */
        for(lv = Lv;lv->next != NULLLV;lvlast = lv,lv = lv->next);
        /* delete entry from end */
        if(lvlast)
            lvlast->next = NULLLV;
        else
            Lv = NULLLV;
        lv->currxcnt = 0;
    } else {    /* create a new entry */
        numad++;
        lv = (struct lv *)callocw(1,sizeof(struct lv));
    }
    memcpy(lv->addr,addr,AXALEN);
    memcpy(lv->digi,digiaddr,AXALEN);	/* 06Apr2021, Maiko */
    lv->iface = ifp;
    lv->next = Lv;
    Lv = lv;
  
    return lv;
}

#endif	/* end of KLM_HLREVAMP_2022 */
  

#ifdef	BACKUP_AXHEARD

/*
 * 21Jan2020, Maiko (VE4KLM), New functions to save and restore axheard lists
 *  (called from ax25cmd.c - but in here because I need al_create() function)
 *
 * Requested by Martijn (PD2NLX) in the Netherlands :)
 *
 * 25Jan2020, Maiko, completed save function
 *
 * 27Jan2020, Maiko, completed load function
 * 
 * 28Jan2020, Maiko, merging doax commands in ax25cmd.c, so we
 * no longer need to have full argument style function calls.
 *
int doaxhsave (int argc, char **argv, void *p)
int doaxhload (int argc, char **argv, void *p)
 *
 * 12Apr2021, Maiko (VE4KLM), besides giving the sysop an option to
 * now specify the file to save/load, I need to fix the fact loaded
 * heard list is ordered backwards, so what was the earliest heard,
 * is actually loaded as the most recent heard, time stamps actually
 * correct I think, it's more the order they appear messed up ?
 *
 * Okay, I see it now, al_create() puts new entries at the root (top)
 * of the link list, almost like if you first sorted the AxHeardFile
 * by time last heard, then run the load, would solve the problem.
 *
 * 13Apr2021, Or better, just load the file, then do a sort of some
 * type on the link list afterwards, so only oddity remaining might
 * be a 'mild' time gap between saving and loading, not sure ...
 */

static char *def_axheard_backup_file = "AxHeardFile";	/* 12Apr2021, and now providing default option */

/* 12Apr2021, Maiko (VE4KLM), sysop can now specify a filename, used to be void */
int doaxhsave (char *filename)
{
	char tmp[AXBUF];
	struct lq *lp;
	struct ld *lp2;
	struct lv *lp3;
	time_t now;
	FILE *fp;

#ifdef KLM_HLREVAMP_2022
	struct iface *ifp;
#endif

	if (filename == NULL)
		filename = def_axheard_backup_file;

	/* if ((fp = fopen ("AxHeardFile", "w+")) == NULLFILE) */

	if ((fp = fopen (filename, "w+")) == NULLFILE)
	{
		tprintf ("failed to open [%s]\n", filename);
		log (-1, "failed to open [%s]", filename);
		return 1;
	}

	/*
	 * 27Jan2020, Maiko, Forgot to save a timestamp of when this file gets
	 * created, we will need this to make sure the time stamps on a future
	 * load are 'accurate' - timestamps are based on the JNOS 'Starttime',
	 * not the epoc or whatever they call it, going back to the 1970 era.
	 *
	 * 15Apr2021, Okay, I think I need to also save the time JNOS has been
	 * running for, since the time stamps are based on since JNOS started.
	 */

	time (&now); fprintf (fp, "%ld %d\n", (long)now, secclock());
 
#ifndef	KLM_HLREVAMP_2022
 
	for (lp = Lq; lp != NULLLQ; lp = lp->next)
	{
		// SAVE these : something to identify iface, lp->addr, lp->time, lp->currxcnt

		fprintf (fp, "%s %s %d %d\n", lp->iface->name, pax25 (tmp, lp->addr), lp->time, lp->currxcnt);
	}

#else

	/*
	 * 05Mar2022, Maiko (VE4KLM), Now have to go through all AX25 interfaces,
	 * note we have to do this in 3 iterations, since the save file is ordered
	 * by heard list type (source, dest, and digipeaged - preserves original.
	 */

	// SAVE these : something to identify iface, lp->addr, lp->time, lp->currxcnt

	for (ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
	{
		if (ifp->type == CL_AX25)
		{
			for (lp = ifp->hrdsrc; lp != NULLLQ; lp = lp->next)
				fprintf (fp, "%s %s %d %d\n", ifp->name, pax25 (tmp, lp->addr), lp->time, lp->currxcnt);
		}
	}
#endif

	/*
	 * 21May2021, Maiko (VE4KLM), added destinations and digipeated stations heard.
	 *  (it is important to put a separator between the different heard lists)
	 */

	fprintf (fp, "---\n");

#ifndef KLM_HLREVAMP_2022
	for (lp2 = Ld; lp2 != NULLLD; lp2 = lp2->next)
		fprintf (fp, "%s %s %d %d\n", lp2->iface->name, pax25 (tmp, lp2->addr), lp2->time, lp2->currxcnt);
#else
	for (ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
	{
		if (ifp->type == CL_AX25)
		{
			for (lp2 = ifp->hrddst; lp2 != NULLLD; lp2 = lp2->next)
				fprintf (fp, "%s %s %d %d\n", ifp->name, pax25 (tmp, lp2->addr), lp2->time, lp2->currxcnt);
		}
	}
#endif

	fprintf (fp, "---\n");

#ifndef KLM_HLREVAMP_2022

	for (lp3 = Lv; lp3 != NULLLV; lp3 = lp3->next)
	{
		/*
		 * Arrgggg, Maiko (VE4KLM), I keep forgetting that pax25() is
		 * not 'thread safe', meaning you can't have them on the same
		 * 'line', the first call will cancel out subsequent one(s) !
		 *
	fprintf (fp, "%s %s %s %d %d\n", lp3->iface->name, pax25 (tmp, lp3->addr), pax25 (tmp, lp3->digi), lp3->time, lp3->currxcnt);
		 *
		 */

		fprintf (fp, "%s %s ", lp3->iface->name, pax25 (tmp, lp3->addr));

 		fprintf (fp, "%s %d %d\n", pax25 (tmp, lp3->digi), lp3->time, lp3->currxcnt);
	}

#else

	for (ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
	{
		if (ifp->type == CL_AX25)
		{
			for (lp3 = ifp->hrdvia; lp3 != NULLLV; lp3 = lp3->next)
			{
				/*
				 * Arrgggg, Maiko (VE4KLM), I keep forgetting that pax25() is
				 * not 'thread safe', meaning you can't have them on the same
				 * 'line', the first call will cancel out subsequent one(s) !
				 *
					fprintf (fp, "%s %s %s %d %d\n",
						lp3->iface->name, pax25 (tmp, lp3->addr),
							pax25 (tmp, lp3->digi), lp3->time, lp3->currxcnt);
				 *
				 */

				fprintf (fp, "%s %s ", ifp->name, pax25 (tmp, lp3->addr));

 				fprintf (fp, "%s %d %d\n", pax25 (tmp, lp3->digi), lp3->time, lp3->currxcnt);
			}
		}
	}

#endif

	fclose (fp);

	return 0;
}

/* 12Apr2021, Maiko (VE4KLM), sysop can now specify a filename, used to be void */
int doaxhload (char *filename)
{
	char iobuffer[80];
	char ifacename[30], callsign[12];
	char thecall[AXALEN];
	int32 axetime, count;
	struct iface *ifp;
	struct lq *lp;
	long atatsave;	/* 17Apr2021, Maiko - actual time at file save */
	time_t now;
	int32 rtatsave;	/* 15Apr2021, Maiko - runtime at file save */
	FILE *fp;

	/* 21May2021, Maiko (VE4KLM), added these for remaining 2 heard lists */
	char thedigi[AXALEN], digipeater[12];
	struct ld *lp2;
	struct lv *lp3;

	extern struct lq *sort_ax_heard();	/* 13Apr2021, Maiko */

	/* 21May2021, Maiko (VE4KLM), two brand new sort functions */
	extern struct ld *sort_ad_heard();
	extern struct lv *sort_av_heard();

	if (filename == NULL)
		filename = def_axheard_backup_file;

	/* if ((fp = fopen ("AxHeardFile", "r")) == NULLFILE) */

	if ((fp = fopen (filename, "r")) == NULLFILE)
	{
		tprintf ("failed to open [%s]\n", filename);
		log (-1, "failed to open [%s]", filename);
		return 1;
	}

	/*
	 * 27Jan2020, Maiko, Make sure to read the timestamp at the
	 * top or else we get gaps in the time stamps.
	 */
	if (fgets (iobuffer, sizeof(iobuffer) - 2, fp) == NULLCHAR)
	{
		tprintf ("failed to read timestamp from [%s]\n", filename);
		log (-1, "failed to read timestamp from [%s]", filename);
		fclose (fp);
		return 1;
	}	

	time (&now);

	/*
	 * 15Apr2021, Maiko, We need to know the 'secclock()' of JNOS when
	 * the heard list was saved, so we can compensate what is displayed
	 * for the loaded values - prior to this, values displayed were not
	 * reasonable (way out) and even negative at times - I think it is
	 * now accurate - did a few quick tests on my /tmp/tmp/rte area.
	 *
	 * 16Apr2021, Good grief, still something not right, finally though
	 * seems I am quite close, this has been tough to figure out for some
	 * odd reason, my mindset is not good with these things. Just have to
	 * figure out the sort now ...
	 *
	 * Terrible of me though - more then a year to get to fixing it ?
	 */

	/*
	 * 21Apr2021, Maiko (VE4KLM), Be carefull here, do NOT allow loading
	 * of the heard file from earlier version of JNOS - this can cause a
	 * loop and/or a crash - thanks Ron (VE3CGR) for catching this !
	 */
	if (sscanf (iobuffer, "%ld %d", &atatsave, &rtatsave) != 2)
	{
		tprintf ("unable to use older version heard file [%s]\n", filename);
		log (-1, "unable to use older version heard file [%s]", filename);
		fclose (fp);
		return 1;
	}

	while (fgets (iobuffer, sizeof(iobuffer) - 2, fp) != NULLCHAR)
	{
		/* 21May2021, Maiko, Check for separator, break out for next list */
		if (strstr (iobuffer, "---"))
			break;

		sscanf (iobuffer, "%s %s %d %d", ifacename, callsign, &axetime, &count);

 		log (-1, "%s %s %d %d", ifacename, callsign, axetime, count); 

		if ((ifp = if_lookup (ifacename)) == NULLIF)
			log (-1, "unable to lookup iface [%s]", ifacename);

		else if (setcall (thecall, callsign) == -1)
			log (-1, "unable to set call [%s]", callsign);

		/* if the call is already there, then don't overwrite it of course */
   		else if
#ifndef KLM_HLREVAMP_2022
 ((lp = al_lookup (ifp, thecall, 1)) == NULLLQ)
#else
 ((lp = al_NEWlookup (&(ifp->hrdsrc), thecall, 1)) == NULLLQ)
#endif
		{
			if
#ifndef KLM_HLREVAMP_2022
			((lp = al_create (ifp, thecall)) == NULLLQ)
#else
			((lp = al_NEWcreate (&(ifp->hrdsrc), thecall)) == NULLLQ)
#endif
				log (-1, "unable to create Lq entry");
			else
			{
				lp->currxcnt = count;
		/*
		 * 27Jan2020, Maiko, This won't be accurate, need to find a way to offset
		 * the last saved time of axheard file with 'now', so that is why I have
		 * the time_gap value (called atatsave now) added to each time read in.
		 *
		 * NOW - what will be interesting is that since JNOS logs these times
		 * based on secclock(), since JNOS started, if the corrected times are
		 * actually greater then the time JNOS has been running, we run into a
		 * problem where axheard can show negative time values, and they will
		 * actually run the wrong way each time we do a 'ax25 heard', so this
		 * requires some additional code work in ax25cmd.c, working nicely.
		 *
	 	 * 17Apr2021, Maiko (VE4KLM), probably one of the most frustrating
		 * things to deal with, I spent an entire week off and on trying to
		 * wrap my head around how to preserve last heard between save and
		 * load, I still don't quite get it, but I believe I got it now.
		 */
				lp->time = (int32)(now - atatsave) + rtatsave - axetime + secclock();

			/*
			 * this part drove me mental, doing this also screwed up the
		 	 * new sort function, so easiest way to fix is use abs() in
		 	 * the sort routine, why am I making this so difficult ?
		 	 */
				lp->time *= -1;
			}
		}
	}

	/*
	 * 21May2021, Maiko (VE4KLM), Load the Destinations Heard List
	 *
	 * NOTE : The separator --- between the different heard lists
	 *        is very important, allows us to stick to one file.
	 */

	while (fgets (iobuffer, sizeof(iobuffer) - 2, fp) != NULLCHAR)
	{
		/* Check for separator, break out for the last list */
		if (strstr (iobuffer, "---"))
			break;

		sscanf (iobuffer, "%s %s %d %d", ifacename, callsign, &axetime, &count);

 		log (-1, "%s %s %d %d", ifacename, callsign, axetime, count); 

		if ((ifp = if_lookup (ifacename)) == NULLIF)
			log (-1, "unable to lookup iface [%s]", ifacename);

		else if (setcall (thecall, callsign) == -1)
			log (-1, "unable to set call [%s]", callsign);

		/* if the call is already there, then don't overwrite it of course */
   		else if
#ifndef KLM_HLREVAMP_2022
			((lp2 = ad_lookup (ifp, thecall, 1)) == NULLLD)
#else
 			((lp2 = ad_NEWlookup (&(ifp->hrddst), thecall, 1)) == NULLLD)
#endif
		{
			if
#ifndef KLM_HLREVAMP_2022
			((lp2 = ad_create (ifp, thecall)) == NULLLD)
#else
			((lp2 = ad_NEWcreate (&(ifp->hrddst), thecall)) == NULLLD)
#endif
				log (-1, "unable to create Ld entry");
			else
			{
				lp2->currxcnt = count;
				lp2->time = (int32)(now - atatsave) + rtatsave - axetime + secclock();
				lp2->time *= -1;
			}
		}
	}

	/*
	 * 21May2021, Maiko (VE4KLM), Load the Digipeated Stations Heard List
	 *
	 * NOTE : The separator --- between the different heard lists
	 *        is very important, allows us to stick to one file.
 	 */

	while (fgets (iobuffer, sizeof(iobuffer) - 2, fp) != NULLCHAR)
	{
		/* there is no separator after this list right now, it's the last one */

		sscanf (iobuffer, "%s %s %s %d %d", ifacename, callsign, digipeater, &axetime, &count);

 		log (-1, "%s %s %s %d %d", ifacename, callsign, digipeater, axetime, count); 

		if ((ifp = if_lookup (ifacename)) == NULLIF)
			log (-1, "unable to lookup iface [%s]", ifacename);

		else if (setcall (thecall, callsign) == -1)
			log (-1, "unable to set call [%s]", callsign);

		else if (setcall (thedigi, digipeater) == -1)
			log (-1, "unable to set call [%s]", callsign);

		/* if the call is already there, then don't overwrite it of course */
   		else if
#ifndef KLM_HLREVAMP_2022
		((lp3 = av_lookup (ifp, thecall, thedigi, 1)) == NULLLV)
#else
		((lp3 = av_NEWlookup (&(ifp->hrdvia), thecall, thedigi, 1)) == NULLLV)
#endif
		{
			if
#ifndef KLM_HLREVAMP_2022
			((lp3 = av_create (ifp, thecall, thedigi)) == NULLLV)
#else
			((lp3 = av_NEWcreate (&(ifp->hrdvia), thecall, thedigi)) == NULLLV)
#endif
				log (-1, "unable to create Lv entry");
			else
			{
				lp3->currxcnt = count;
				lp3->time = (int32)(now - atatsave) + rtatsave - axetime + secclock();
				lp3->time *= -1;
			}
		}
	}

	/* End of loading the 3 heard lists - 11:30 am, just have to write 2 new sort functions */

	fclose (fp);

/*
 * 05Mar2022 - Maiko (VE4KLM) - just need to deal with this original sort code :(
 *
 * AND after playing around, there is actually no need to sort, since we are now
 * doing interface by interface - they get auto sorted when logged (ie, logsrc),
 * so how nice is that ? The only catch is IF you load a save file from previous
 * version before this REVAMP it will not be sorted, BUT just save it on the new
 * version, and reload it again. It will be instantly sorted for you - nice !!!
 *
 *   'ax25 heard save crap', immediately followed by 'ax25 heard load crap'
 *
 * Okay, not quite, even with new save file, so the order of save file needs
 * a taking look at, but right now I'm not too concerned about it, working.
 */
#ifndef	KLM_HLREVAMP_2022

	/* new function required (Maiko) - need to sort by time, since file order does not match Lq order */

	Lq = sort_ax_heard ();	/* confirmed working 7:40 pm 13Apr2021 !!! */

	/* 21May201, Maiko (VE4KLM), two more brand new sort functions (see axhsort.c) */

	Ld = sort_ad_heard ();
	Lv = sort_av_heard ();

#endif

	return 0;
}

#endif /* BACKUP_AXHEARD */

#endif /* AX25 */

