
/*
 * November, 4, 2020 - Moving from GET to POST - putting the credentials in
 * the URL is just unacceptable, but you have to realize ; when I wrote this
 * stuff back in 2009, my knowledge of HTTP protocols was quite limited, and
 * still is in some ways. The protocol has evolved and it's alot to read.
 *
 * Web Based BBS Access for JNOS 2.0g and beyond ...
 *
 *   June 8, 2009 - started working in a play area on a work machine.
 *   July 3, 2009 - put into my official development source (prototype).
 *  July 21, 2009 - prototype pretty much ready, just need to add some form
 *                  of session control, so that multiple web clients can use
 *                  this all at the same time, without interfering with each
 *                  other's data and data flow.
 * February, 2010 - refining session control for multiple web clients at once.
 * April 24, 2010 - seems to be working nicely over last while - done.
 *
 * Designed and written by VE4KLM, Maiko Langelaar
 *
 * For non-commercial use (Ham Radio) only !!!
 *
 */

#include "global.h"

#ifdef	HTTPVNC

#ifndef OPTIONAL
#define OPTIONAL
#endif

#include "ctype.h"
#include "cmdparse.h"
#include "commands.h"

#ifndef MSDOS
#include <time.h>
#endif

#include <sys/stat.h>
#include "mbuf.h"

#include "socket.h"

/* 01Feb2002, Maiko, DOS compile for JNOS will complain without this */
#ifdef  MSDOS
#include "internet.h"
#endif

#include "netuser.h"
#include "ip.h"
#include "files.h"
#include "session.h"
#include "iface.h"
#include "udp.h"
#include "mailbox.h"
#include "usock.h"
#include "mailutil.h"
#include "smtp.h"

#include "proc.h"

#include "bbs10000.h"	/* 03Jul2009, Maiko, New header file */

#define	MAXTPRINTF	(SOBUF - 5)

#define ISCMDLEN 256

/*
 * 08Jul2009, Maiko (VE4KLM), Now support multiple web clients coming
 * in at the same time. Need to write a few support functions for it.
 */

#define	MAXHVSSESS	20

static HTTPVNCSESS hvs[MAXHVSSESS];

static int maxhvs = 0;	/* start with zero clients */

static int hvsdebug = 0;

static int showhvs ()
{
	HTTPVNCSESS *ptr;

	int cnt;

	tprintf ("web based mailbox/vnc session list\n");
	tprintf ("I V R  Ipaddr         Callsign\n");

	for (cnt = 0; cnt < maxhvs; cnt++)
	{
		ptr = &hvs[cnt];

		tprintf ("%d %d %d  %s  ", cnt, ptr->valid,
			ptr->reused, inet_ntoa (ptr->ipaddr));

		if (ptr->currcall)
			tprintf ("%s\n", ptr->currcall);
		else
			tprintf ("-\n");
	}

	return 0;
}

static HTTPVNCSESS *getmyhvs (int s)
{
	struct sockaddr_in fsocket;

	int cnt, len_i = sizeof(fsocket);

	/* need to get some information of the client connecting */
	if (j2getpeername (s, (char*)&fsocket, &len_i) == -1)
	{
		log (s, "unable to get client information");
		return (HTTPVNCSESS*)0;
	}

	for (cnt = 0; cnt < maxhvs; cnt++)
	{
		if (hvs[cnt].ipaddr == (int32)fsocket.sin_addr.s_addr)
		{
			if (hvsdebug)
				log (s, "found client (%d) in session list", cnt);

			break;
		}
	}

	/* if we don't find a session, then Add and Initialize a new one */

	if (cnt == maxhvs)
	{
		int reclaim = 0;

		/* 23Feb2010, try to reclaim any sessions marked as not valid */
		while (reclaim < maxhvs)
		{
			if (!hvs[reclaim].valid)	/* 06Mar2010, Maiko, was == 2 */
			{
				if (hvsdebug)
					log (s, "reclaim (%d) in session list", reclaim);

				hvs[reclaim].reused++;	

				break;
			}

			reclaim++;
		}

		if (reclaim == maxhvs && maxhvs == MAXHVSSESS)
		{
			log (s, "max (%d) web clients reached", MAXHVSSESS);
			return (HTTPVNCSESS*)0;
		}

		if (reclaim < maxhvs)
			cnt = reclaim;
		else
		{
			cnt = maxhvs;
			hvs[cnt].reused = 0;	/* 06Mar2010, Maiko, Statistics */
			maxhvs++;
		}

		if (hvsdebug)
			log (s, "add and initialize new client (%d) to session list", cnt);

		hvs[cnt].ipaddr = fsocket.sin_addr.s_addr;
		hvs[cnt].mbxs[0] = hvs[cnt].mbxs[1] = -1;
		hvs[cnt].escape_char = hvs[cnt].clear_char = 0;
		hvs[cnt].abort_char = 0;	/* 05Nov2020, Maiko, New for aborting send message */
		hvs[cnt].currcall = (char*)0;
		hvs[cnt].mbxl = hvs[cnt].mbxr = 0;

		hvs[cnt].valid = 0;	/* 23Feb2010, for DOS attacks filling us up */

	}

	return &hvs[cnt];
}

/* 25Jun2009, Brand new private httpget and parse functions */
/* 08Jul2009, Maiko, New hextochar function for URI decoding */

char hextochar (char *ptr)
{
	char nib, val;
	int cnt = 2;

	while (cnt)
	{
		if (*ptr >= '0' && *ptr <= '9')
			nib = (*ptr - 0x30);
		else if (*ptr >= 'A' && *ptr <= 'F')
			nib = (*ptr - 0x41) + 10;
		else if (*ptr >= 'a' && *ptr <= 'f')
			nib = (*ptr - 0x61) + 10;
		else
		{
			log (-1, "invalid hex digit");
			nib = 0;
		}

		if (cnt == 2)
			val = nib * 16;
		else
			val += nib;
		cnt--;
		ptr++;
	}
/*
 * 04Nov2020, Maiko, Agggg, this locks up my xterm, I should know better
 *
	if (hvsdebug)
		log (-1, "hex value (%x) (%c)", (int)val, val);
 */
	return val;
}

static char* parse (char *hptr)
{
	char *ptr, *tptr;

	/* make a working copy of hptr, leave the original one intact */
	tptr = malloc (strlen(hptr));

	ptr = tptr;

	if (hvsdebug)
		log (-1, "parse [%s]", hptr);

	while (*hptr && *hptr != '&' && *hptr != ' ')
	{
		/* Map '+' characters to spaces */

		if (*hptr == '+')
			*ptr = ' ';

		/* Map special (encodings) characters to real characters */

		else if (*hptr == '%')
		{
			hptr++;
			*ptr = hextochar (hptr);	/* 08Jul09, Maiko, New function now */
			hptr++;
		}

		else
			*ptr = *hptr;

		hptr++;
		ptr++;
	}

	*ptr = 0;

	if (hvsdebug)
		log (-1, "parse [%s]", tptr);

	return tptr;
}

#ifdef	DONT_COMPILE	/* malloc errors, I wonder if content len is not correct */

/*
 * 05Nov2020, Maiko (VE4KLM), User can now customize the beginning
 * section of the html presentation. Maybe they wish to add a logo
 * or some type of background image, or extra instructions ?
 *
 */

static char *starting_html_file = "httpvncS.html";

static char *load_starting_html (char *ptr)
{
	FILE *fp;

	char buf[100];

	sprintf (buf, "%s/%s", Spoolqdir, starting_html_file);

	if ((fp = fopen (buf, READ_TEXT)) == NULLFILE)
		ptr += sprintf (ptr, "<html>");
	else
	{    
		while (fgets (buf, 98, fp))
			ptr += sprintf (ptr, "%s", buf);

		fclose (fp);
	}

	return ptr;
}

#endif

#ifdef MBX_CALLCHECK
extern int callcheck (char*); /* 03Sep2010, Maiko, Need callsign validation*/
#endif

extern char *strcasestr (const char*, const char*);	/* 05Nov2020, Maiko */

static int httpget (int s, HTTPVNCSESS *hvsptr, char **calldata, char **cmddata, char **passdata, char **hostdata)
{
	int retval = 0, err, len;
	char important_stuff[ISCMDLEN+1];
	char *ptr;

	/* 04Nov2020, Maiko (VE4KLM), content length and flag telling us it's a form POST submission */
	int content_length = 0;
	int inpostmode = 0;

	*calldata = *passdata = *cmddata = (char*)0;	/* very important */

	*hostdata = (char*)0;	/* 05Jul09, Maiko, Host from HTTP header */

	while (1)
	{
		j2alarm (10000);
   		err = recvline (s, important_stuff, ISCMDLEN);
		j2alarm (0);

		if (err == -1)
		{
			log (s, "hvs recvline errno %d", errno);
			retval = -1;
			break;
		}

		rip (important_stuff);

		len = strlen (important_stuff);

		if (hvsdebug == 2)
			log (s, "http (%d) [%s]", len, important_stuff);

		if (!len)
		{
			if (inpostmode)
			{
				/* log (s, "content length %d", content_length); */

				j2alarm (10000);
				ptr = important_stuff;
   				while (content_length--)
					*ptr++ = recvchar (s);
				*ptr = 0;
				j2alarm (0);

				/* log (s, "message body [%s]", important_stuff); */

				if (strstr (important_stuff, "escape=on"))
				{
					if (hvsdebug)
						log (s, "escape toggled");

					hvsptr->escape_char = 1;
				}
				else hvsptr->escape_char = 0;

				/* 06Jul09, Maiko, Ability to clear tmp file */
				if (strstr (important_stuff, "clear=on"))
				{
					if (hvsdebug)
						log (s, "clear toggled");

					hvsptr->clear_char = 1;
				}
				else hvsptr->clear_char = 0;

				/* 05Nov2020, Maiko (VE4KLM), Added CTRL-A to abort sending of messages */
				if (strstr (important_stuff, "abort=on"))
				{
					if (hvsdebug)
						log (s, "abort toggled");

					hvsptr->abort_char = 1;
				}
				else hvsptr->abort_char = 0;


				if ((ptr = strstr (important_stuff, "call=")) != NULL)
					*calldata = parse (ptr + 5);
#ifdef MBX_CALLCHECK
				/*
				 * 03Sep2010, Maiko (VE4KLM), Callsign validation should have
				 * been done here long time ago. The potential for JUNK logins
				 * is very high on any web based system, I should know better.
				 *
				 * The callcheck functionality courtesy of K2MF (Barry)
				 */
				if (*calldata && !callcheck (*calldata))
				{
					logmbox (s, *calldata, "bad login");

					free (*calldata);	/* no point keeping it, free it up */

                	*calldata = (char*)0;	/* very important !!! */
				}
#endif
				if ((ptr = strstr (important_stuff, "pass=")) != NULL)
					*passdata = parse (ptr + 5);

				if ((ptr = strstr (important_stuff, "cmd=")) != NULL)
					*cmddata = parse (ptr + 4);

				/* if either is present, then treat it as a valid request */
				if (*calldata || *cmddata)
					retval = 1;

				inpostmode = 0;
			}

			break;
		}

		if ((ptr = strstr (important_stuff, "Host: ")) != NULL)
			*hostdata = parse (ptr + 6);

		/*
		 * 05Nov2020, Maiko (VE4KLM), Are you kidding me ???
		 *
		 * Lynx uses 'Content-length'
		 * Mozilla uses 'Content-Length'
		 *
		 * notice the subtle difference, damn it !
		 *  (use strcasestr, not strstr as I had in the original code)
		 */
		if ((ptr = strcasestr (important_stuff, "Content-length: ")) != NULL)
			content_length = atoi (parse (ptr + 16));

		/*
		 * 04Nov2020, Maiko (VE4KLM), 11 years (wow) later ! Switchto POST
		 * for FORM submissions, I should never have used GET for this.
		 */
		if (!memcmp (important_stuff, "POST / ", 7))
		{
			/* log (s, "got a POST, set flag to loop for body content, read in values"); */

			inpostmode = 1;
		}
		/*
		 * 05Nov2020, Maiko (VE4KLM), the favicon requests are intense, and
		 * use up precision bandwidth in my opinion, why does mozilla insist
		 * on sending 6 of them one after the other, unreal, they're ANNOYING
		 * as hell so deal with it inside this function, this is the quickest
		 * way according to 'https://tools.ietf.org/html/rfc2616#section-6'.
		 */
		else if (!memcmp (important_stuff, "GET /favicon", 12))
		{
			/* confirmed working, not seeing ANY more of these requests */

			tprintf ("HTTP/1.1 404 \r\n\r\n");

			/* just stay in the loop and wait for another legit request :) */
		}

		else if (!memcmp (important_stuff, "GET / ", 6))
			retval = 1;
	}

	return retval;
}

static void socketpairsend (int usock, char *buffer, int len)
{
	char *tptr = buffer;

	int cnt = len;

	while (cnt > 0)
	{
		usputc (usock, *tptr);
		tptr++;
		cnt--;
	}

	usflush (usock);
}

static void launch_mbox (HTTPVNCSESS *hvsptr, char *callsign, char *passwd)
{

	if (hvsptr->mbxl)
	{
		if (hvsdebug)
			log (-1, "mailbox already launched");

		return;
	}

	if (j2socketpair (AF_LOCAL, SOCK_STREAM, 0, hvsptr->mbxs) == -1)
	{
		log (-1, "socketpair failed, errno %d", errno);
		return;
	}

	seteol (hvsptr->mbxs[0], "\n");
	seteol (hvsptr->mbxs[1], "\n");

	sockmode (hvsptr->mbxs[1], SOCK_ASCII);

	strlwr (callsign);	/* 15Jul2009, Maiko, Keep it lower case please ! */

	hvsptr->passinfo.name = j2strdup (callsign);
	hvsptr->passinfo.pass = j2strdup (passwd);

	hvsptr->currcall = hvsptr->passinfo.name;

	newproc ("WEB2MBX", 8192, mbx_incom, hvsptr->mbxs[1],
		(void*)WEB_LINK, (void*)&hvsptr->passinfo, 0);

	hvsptr->mbxl = 1;
}

/*
 * 15Jul2009, Maiko, Put this code into it's own function, which is used
 * to 'time stamp/welcome' a user when they first start a session, as well
 * as provide a 'time stamp/notice' after they clear their session file.
 */
void markermsg (int required, HTTPVNCSESS *hvsptr)
{
	char intro[80], *cp, *ptr = intro;
	time_t t;
	time(&t);
	cp = ctime(&t);
#ifdef UNIX
	if (*(cp+8) == ' ')
		*(cp+8) = '0';  /* 04 Feb, not b4 Feb */
#endif

	ptr += sprintf (ptr, "\n*** %2.2s%3.3s%2.2s %8.8s",
			cp+8, cp+4, cp+22, cp+11);

	switch (required)
	{
		case 0:
			ptr += sprintf (ptr, " New session, welcome [%s]",
						hvsptr->currcall);
			break;

		case 1:
			ptr += sprintf (ptr, " Session file has been cleared");
			break;

		default:
			break;
	}

	ptr += sprintf (ptr, " ***\n\n");

	fputs (intro, hvsptr->fpsf);

	fflush (hvsptr->fpsf);	/* 15Jul2009, Maiko, Flush it ! */
}

/*
 * 09Jul2009, Maiko, process stuff from mailbox, write to session file,
 * which I hope works alot better than the KLUDGE I have been using to
 * date (ie, the sockfopen). This code taken from my HFDD stuff ...
 *
 */

void mailbox_to_file (int dev, void *n1, void *n2)
{
	char outdata[300], *ptr = outdata;
	int c, len;

	HTTPVNCSESS *hvsptr = (HTTPVNCSESS*)n1;

	if ((hvsptr->fpsf = fopen (hvsptr->tfn, APPEND_TEXT)) == NULL)
	{
		log (-1, "fopen failed, errno %d", errno);
        return;
    }

	if (hvsdebug)
		log (-1, "m2f - socket %d", hvsptr->mbxs[0]);

	sockowner (hvsptr->mbxs[0], Curproc);

	sockmode (hvsptr->mbxs[0], SOCK_ASCII);

	markermsg (0, hvsptr);

	while (1)
	{
		/*
		 * The whole idea here is that if we don't get anything from
		 * the keyboard or our forwarding mailbox after 2 seconds, I
		 * think it's safe to assume there is nothing more to come.
		 *
		 * 05Nov2020, Maiko - Trying 1 second now, this is the only
		 * part of the design that might be finicky ? I'm not sure.
		 */

		j2alarm (1000);
    	c = recvchar (hvsptr->mbxs[0]);
		j2alarm (0);

		/*
		 * An EOF tells us that we did not receive anything from
		 * the mailbox for 2 seconds (in this case) or it tells us
		 * that we need to terminate this process !
		 */
		if (c == EOF)
		{
			/*
			 * 10Feb2007, Maiko, Actually if the following happens,
			 * then very likely a mailbox or session died, in which
			 * case (now that we want this to not die), we need to
			 * do cleanup - .....
			 */
			if (errno)
			{
				if (errno != EALARM)
				{
					/* 05Nov2020, Maiko, Errno 107 is typical on mailbox shutdown, don't show it */
					if (errno != 107)
						log (-1, "errno %d reading from mbox", errno);

					break;
				}
			}
		}

 		/* Do not put the EOF into the outgoing buffer */
		else
		{
			*ptr++ = c & 0xff;
		}

		/* Calculate the length of data in the outgoing buffer */
		len = (int)(ptr - outdata);

        if (len > 250 || (len && c == EOF))
		{
			outdata[len] = 0;	/* terminate string !!! */

			fputs (outdata, hvsptr->fpsf);
			fflush (hvsptr->fpsf);

			ptr = outdata;	/* make sure we start from beginning again */

			continue;
		}
	}

	/* 05Nov2020, Maiko, Errno 107 is typical on mailbox shutdown, don't show it */
	if (errno != 107)
		log (-1, "left mailbox_to_file, errno %d", errno);
}

void serv10000 (int s, void *unused OPTIONAL, void *p OPTIONAL)
{
	char *body, *ptr, *cmddata, *calldata, *passdata, *hostdata;
	int needed, len, resp;
	struct mbx *curmbx;
	HTTPVNCSESS *hvsptr;
	FILE *fpco;

	log (s, "hvs connect");

	close_s (Curproc->output);

	Curproc->output = s;

	/* 08Jul2009, Maiko, Get session state and data for this web client */
	if ((hvsptr = getmyhvs (s)) == (HTTPVNCSESS*)0)
	{
		log (s, "hvs disconnect");

		close_s (s);
		return;
	}

	while (1)
	{
		char cmd[80];

		resp = httpget (s, hvsptr, &calldata, &cmddata, &passdata, &hostdata);

		if (resp < 1)
			break;

		if (hvsdebug)
			log (-1, "hostdata [%s]", hostdata);

		if (calldata && *calldata && passdata && *passdata)
		{
			/*
			 * 23Feb2010, Maiko, If we get to this point, then I think it is
			 * safe to say we have a valid session with an actual user call
			 * present, so make sure this session is kept in the getmyhvs()
			 * cache. Prior to this, I cached ALL connects whether a valid
			 * callsign appeared or not, so any port attacks on this server
			 * would just fill up getmyhvs() cache, MAXHVSSESS would be hit,
			 * and no one would be able to use it, unless I restarted NOS.
			 */
			hvsptr->valid = 1;

			if (hvsdebug)
				log (-1, "call [%s] pass [%s]", calldata, passdata);

			launch_mbox (hvsptr, calldata, passdata);

			j2pause (1000);	/* give it time to come up */
		}

		if (cmddata && *cmddata)
		{
			if (hvsdebug)
				log (s, "command [%s]", cmddata);

			sprintf (cmd, "%s\n", cmddata);
		}
		/* 05Nov2020, Maiko, New button to abort sending of messages */
		else if (hvsptr->abort_char)
		{
			if (hvsdebug)
				log (s, "abort character");

			sprintf (cmd, "%c\n",'');
		}
		else if (hvsptr->escape_char)
		{
			if (hvsdebug)
				log (s, "escape character");

			sprintf (cmd, "%c\n", '');
		}
		else if (hvsptr->clear_char)
		{
			if (hvsdebug)
				log (s, "clear session file");

			*cmd = 0;
		}
		else *cmd = 0;

		/* let's see if the mailbox is really active */
		/*
		 * 09Jul2009, Maiko, Look at the session value actually, since
		 * if the web client terminates with an active mailbox, multiple
		 * mailboxs are created under the same user, which is not good.
		 *
		if (calldata && *calldata)
		 */

		if (hvsptr->currcall)
		{
			if (hvsdebug)
				log (s, "looking for [%s]", hvsptr->currcall);

			for (curmbx=Mbox; curmbx; curmbx=curmbx->next)
				if (!stricmp (curmbx->name, hvsptr->currcall))
					break;

			if (!curmbx && hvsdebug)
				log (-1, "not found !!!");
		}
		else curmbx = (struct mbx*)0;

		if (!curmbx)
			hvsptr->mbxl = hvsptr->mbxr = 0;
		else
		{
			if (!hvsptr->mbxr)
			{
				sprintf (hvsptr->tfn, "/tmp/%s.www", curmbx->name);

				if (hvsdebug)
					log (-1, "session file [%s] - start m2f process", hvsptr->tfn);

   				newproc ("m2f", 1024, mailbox_to_file, 0, (void*)hvsptr, (void*)0, 0);
				j2pause (1000);	/* give file a chance to finish */

				hvsptr->mbxr = 1;
			}

			/* 06Jul09, Maiko, This should work to clear file */
			else if (hvsptr->clear_char)
			{
				if (ftruncate (fileno (hvsptr->fpsf), 0))
					log (-1, "truncate session page, errno %d", errno);

				markermsg (1, hvsptr);	/* 15Jul2009, Maiko, Note the clear */
			}
		}

		if (*cmd)
		{
			if (hvsptr->fpsf)
			{
				fputs (cmd, hvsptr->fpsf);	/* should do this before send */

				fflush (hvsptr->fpsf);	/* 15Jul2009, Maiko, Flush it ! */
			}

			if (curmbx)
				socketpairsend (hvsptr->mbxs[0], cmd, strlen (cmd));

			j2pause (1000);	/* give mailbox a chance to display */
		}

	len = 0;

	if (hvsptr->mbxr)
	{
		/* Now read in the generated text file, find length */
		
   		if ((fpco = fopen (hvsptr->tfn, READ_TEXT)))
		{
   			fseek (fpco, 0, SEEK_END);
			len = ftell (fpco);
			rewind (fpco);

			/* leave file open for later - allocate memory first */

			if (hvsdebug)
				log (s, "ftell says %d bytes", len);
		}
	}

	needed = len + 1300;		/* cmd response + standard form */

		if (hvsdebug)
			log (s, "Body needed %d bytes", needed);

	if ((body = malloc (needed)) == (char*)0)
	{
		log (s, "No memory available");
		break;
	}

	ptr = body;

#ifdef	DONT_COMPILE	/* malloc errors, I wonder if content len is not correct */

	/*
	 * 05Nov2020, Maiko, Attempt at customization, so all of the now
	 * commented out code below will go into a supplied default file
	 * which the user is more then welcome to customize.
	 */

	ptr = load_starting_html (ptr);

#else

	ptr += sprintf (ptr, "<html>");

	ptr += sprintf (ptr, "<head><script type=\"text/javascript\">\nfunction scrollElementToEnd (element) {\nif (typeof element.scrollTop != 'undefined' &&\ntypeof element.scrollHeight != 'undefined') {\nelement.scrollTop = element.scrollHeight;\n}\n}\n</script></head>");

	ptr += sprintf (ptr, "<body bgcolor=\"beige\"><br>");

#endif

	/*
	 * 05Jul09, Maiko, Now get hostname from HTTP header as we should
	 * 04Nov20, Maiko (VE4KLM), 11 years later, holy cow, switching to POST, should never have used GET
	 */
	
	ptr += sprintf (ptr, "<form name=\"mainName\" action=\"http://%s\" method=\"post\">", hostdata);

	ptr += sprintf (ptr, "<table bgcolor=\"#aaffee\" style=\"border: 1px solid black;\" cellspacing=\"0\" cellpadding=\"10\"><tr>");

	/*
	 * 08Jul2009, Maiko (VE4KLM), Use different screens for login
	 * and active sessions already logged in - looks better :)
	 */
	if (hvsptr->mbxr)
	{
		ptr += sprintf (ptr, "<td><table bgcolor=\"lightblue\" style=\"border: 1px solid black;\" cellpadding=\"5\"><tr><td>%s</td></tr></table><input type=\"hidden\" name=\"call\" size=\"6\" value=\"%s\"></td>", hvsptr->currcall, hvsptr->currcall);

		/* 08Jul2009, Maiko, Make sure to put focus on CMD field !!! */

		ptr += sprintf (ptr, "<td>command <input type=\"text\" name=\"cmd\" value=\"\" size=\"20\" maxlength=\"80\"></td><td><input type=\"submit\" value=\"Enter / Refresh\">&nbsp;<input type=\"checkbox\" name=\"abort\"><font size=1>CTRL-A</font>&nbsp;<input type=\"checkbox\" name=\"escape\"><font size=1>CTRL-T</font>&nbsp;<input type=\"checkbox\" name=\"clear\"><font size=1>Clear</font></td></tr></table><script>document.mainName.cmd.focus();</script></form><p>");

		ptr += sprintf (ptr, "<form name=\"formName\"><textarea style=\"border: 1px solid black; padding: 10px;\" name=\"textAreaName\" readonly=\"readonly\" rows=24 cols=80>");

		if (fpco)
		{
			char inbuf[82];

			while (fgets (inbuf, 80, fpco))
				ptr += sprintf (ptr, "%s", inbuf);

			fclose (fpco);
		}

		ptr += sprintf (ptr, "</textarea><script>scrollElementToEnd(document.formName.textAreaName);</script></p></form>");
	}
	else
	{
		ptr += sprintf (ptr, "<td>call <input type=\"text\" style=\"text-align: center;\" name=\"call\" size=\"6\" value=\"\">&nbsp;pass <input type=\"password\" style=\"text-align: center;\" name=\"pass\" size=\"6\" value=\"\"></td>");

		ptr += sprintf (ptr, "<td><input type=\"hidden\" name=\"cmd\" value=\"\"></td><td><input type=\"submit\" value=\"Enter\"></td></tr></table></form><p>");

		ptr += sprintf (ptr, "<h4>no active sessions - please login</h4>");
	}

	ptr += sprintf (ptr, "</body></html>\r\n");
	
#ifdef	DONT_COMPILE
	/*
	 * 05Nov2020, Maiko (VE4KLM), not sure we need this.
	 * does it slow it down, do other JNOS procs suffer
	 * if this is commented out ? does not seem so ...
	 */
	pwait (NULL);	/* give other processes a chance */
#endif
	len = ptr - body;

	if (hvsdebug)
 		log (s, "Body length %d", len);

	/* write the HEADER record */
	tprintf ("HTTP/1.1 200 OK\r\n");
	tprintf ("Content-Length: %d\r\n", len);
	tprintf ("Content-Type: text/html\r\n");

	/* 05Nov2020, Maiko (VE4KLM), Let people know it's 'recent' */
	tprintf ("Server: JNOS 2.0m HTTP VNC 1.0\r\n");

	/* use a BLANK line to separate the BODY from the HEADER */
	tprintf ("\r\n");

	/* 01Aug2001, VE4KLM, Arrggg !!! - the tprintf call uses vsprintf
	 * which has a maximum buf size of SOBUF, which itself can vary
	 * depending on whether CONVERSE or POPSERVERS are defined. If
	 * we exceed SOBUF, TNOS is forcefully restarted !!! Sooooo...,
	 * we will have to tprintf the body in chunks of SOBUF or less.
	 *
	tprintf ("%s", body);
	 */

	ptr = body;

	while (len > 0)
	{
#ifdef	DONT_COMPILE
		/*
		 * 05Nov2020, Maiko (VE4KLM), not sure we need this.
		 * does it slow it down, do other JNOS procs suffer
		 * if this is commented out ? does not seem so ...
		 */
		pwait (NULL);	/* give other processes a chance */
#endif

		if (len < MAXTPRINTF)
		{
			tprintf ("%s", ptr);
			len = 0;
		}
		else
		{
			tprintf ("%.*s", MAXTPRINTF, ptr);
			len -= MAXTPRINTF;
			ptr += MAXTPRINTF;
		}
	}

	free (body);

		break;	/* break out !!! */
	}

	log (s, "hvs disconnect");

	close_s (s);
}

/* Start up HTTP VNC server */
int httpvnc1 (int argc, char *argv[], void *p)
{
    int16 port;

    if(argc < 2)
       port = 10000;
    else
       port = atoi(argv[1]);

    return start_tcp (port, "HTTP VNC Server", serv10000, 8192 /* 4096 */);
}

/* Stop the HTTP VNC server */
int httpvnc0 (int argc, char *argv[], void *p)
{
    int16 port;

    if(argc < 2)
       port = 10000;
    else
       port = atoi(argv[1]);

    return stop_tcp(port);
}

/* 24Feb2010, Maiko, New function to show/manipulate the getmyhvs() table */

int httpvncS (int argc, char *arg[], void *p)
{
	return showhvs ();
}

#endif	/* end of HTTPVNC */
