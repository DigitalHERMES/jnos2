
/*
 * 13Apr2021, Maiko (VE4KLM), trying to write my own sort routine
 * from scratch, probably not the most efficient, but it seems to
 * work, it's destructive in nature, obliterates my original array
 * which I would like to sort from lowest to highest, so a copy of
 * what you want sorted should be made first, then pass the copy.
 *
 * I need something to sort the axheard link list after it gets
 * loaded from the axheard backup file, this idea should work.
 *
 * Create new LL, destroy the old one - hopefully works
 *  (seems to, ran tests evening of 13Apr2021, Maiko)
 *
 * 14Apr2021, Maiko, Not quite right yet (hard to find) :|
 *
 * 17Apr2021, Maiko, This works, but I need to make sure to now
 * abs() the time value (see comments in ax25cmd.c) or else the
 * sort will not work at all, I suppose I could rewrite it to
 * handle the negative numbers, but using abs() is easiest.
 */

#include "ax25.h"

struct lq *sort_ax_heard ()
{
	register struct lq *lp, *lp2, *lplowest, *topnewlq, *newlp, *newlq = NULLLQ;

	int lowest;

	for (lp = Lq;lp != NULLLQ; lp = lp->next)
	{
		for (lowest = INT_MAX, lp2 = Lq; lp2 != NULLLQ; lp2 = lp2->next)
		{
			if (lp2->time == INT_MAX) /* ignore already used */
				continue;

			// log (-1, "time %d lowest %d", lp2->time, lowest);

			if ((abs)(lp2->time) < lowest)
			{
				lowest = (abs)(lp2->time);
				lplowest = lp2;
			}
		}

		/* lplowest points to entry with lowest time - most recent entry */

		newlp = callocw (1, sizeof(struct lq));
		memcpy (newlp, lplowest, sizeof(struct lq));
		newlp->next = NULLLQ;

		if (newlq == NULLLQ)
			topnewlq = newlq = newlp;
		else
			newlq = newlq->next = newlp;

	    lplowest->time = INT_MAX; /* ignore already used (destroys time) */
	}

	/* destroy the old link list, free up the memory */
	for (lp = Lq;lp != NULLLQ; lp = newlp)
	{
		newlp = lp->next;	/* reuse pointer variable */
		free (lp);
	}

	return topnewlq;
}

#ifdef	TEST_MY_ORIGINAL_CODE

#include <stdio.h>

int before[] = { 78, 4, 156, -3, -1721, 16, 99, 0 };

int after[] = { 0, 0, 0, 0, 0, 0, 0, 0 };

int main ()
{
	int aindex, bindex, lowest, lowest_bindex;

	printf ("before processing ...\n");
	for (aindex = 0; before[aindex]; aindex++)
		printf (" %d", before[aindex]);
	printf ("\n");

	for (aindex = 0; before[aindex]; aindex++)
	{
		printf ("proc %d : ", before[aindex]);

	   	for (lowest = 999999, bindex = 0; before[bindex]; bindex++)
		{
			if (before[bindex] == 999999) /* ignore already used */
			{
				printf (" X%d", bindex);
				continue;
			}

			if (before[bindex] < lowest)
			{
				lowest = before[bindex];
				lowest_bindex = bindex;

				printf (" %d", bindex);
			}
		}

		printf ("\n");

		after[aindex] = before[lowest_bindex];

	 	before[lowest_bindex] = 999999; /* indicate already used - destroy */
	}

	printf ("after processing ...\n");
	for (aindex = 0; after[aindex]; aindex++)
		printf (" %d", after[aindex]);
	printf ("\n");

	return 0;
}

#endif	/* end of TEST_MY_ORIGINAL_CODE */

