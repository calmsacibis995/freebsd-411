/*-
 * Copyright (c) 1999 Michael Smith <msmith@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/kern/subr_eventhandler.c,v 1.3 1999/11/16 16:28:57 phk Exp $
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/eventhandler.h>

MALLOC_DEFINE(M_EVENTHANDLER, "eventhandler", "Event handler records");

/* List of 'slow' lists */
static TAILQ_HEAD(, eventhandler_list)	eventhandler_lists;
static int				eventhandler_lists_initted = 0;

struct eventhandler_entry_generic 
{
    struct eventhandler_entry	ee;
    void			(* func)(void);
};

/* 
 * Insertion is O(n) due to the priority scan, but optimises to O(1)
 * if all priorities are identical.
 */
eventhandler_tag
eventhandler_register(struct eventhandler_list *list, char *name, 
		      void *func, void *arg, int priority)
{
    struct eventhandler_entry_generic	*eg;
    struct eventhandler_entry		*ep;
    
    /* avoid the need for a SYSINIT just to init the list */
    if (!eventhandler_lists_initted) {
	TAILQ_INIT(&eventhandler_lists);
	eventhandler_lists_initted = 1;
    }

    /* Do we need to find/create the (slow) list? */
    if (list == NULL) {
	/* look for a matching, existing list */
	list = eventhandler_find_list(name);

	/* Do we need to create the list? */
	if (list == NULL) {
	    if ((list = malloc(sizeof(struct eventhandler_list) + strlen(name) + 1, 
			       M_EVENTHANDLER, M_NOWAIT)) == NULL)
		return(NULL);
	    list->el_flags = 0;
	    list->el_name = (char *)list + sizeof(struct eventhandler_list);
	    strcpy(list->el_name, name);
	    TAILQ_INSERT_HEAD(&eventhandler_lists, list, el_link);
	}
    }
    if (!(list->el_flags & EHE_INITTED)) {
	TAILQ_INIT(&list->el_entries);
	list->el_flags = EHE_INITTED;
    }
    
    /* allocate an entry for this handler, populate it */
    if ((eg = malloc(sizeof(struct eventhandler_entry_generic), 
		     M_EVENTHANDLER, M_NOWAIT)) == NULL)
	return(NULL);
    eg->func = func;
    eg->ee.ee_arg = arg;
    eg->ee.ee_priority = priority;
    
    /* sort it into the list */
    for (ep = TAILQ_FIRST(&list->el_entries);
	 ep != NULL; 
	 ep = TAILQ_NEXT(ep, ee_link)) {
	if (eg->ee.ee_priority < ep->ee_priority) {
	    TAILQ_INSERT_BEFORE(ep, &eg->ee, ee_link);
	    break;
	}
    }
    if (ep == NULL)
	TAILQ_INSERT_TAIL(&list->el_entries, &eg->ee, ee_link);
    return(&eg->ee);
}

void
eventhandler_deregister(struct eventhandler_list *list, eventhandler_tag tag)
{
    struct eventhandler_entry	*ep = tag;

    /* XXX insert diagnostic check here? */
    if (ep != NULL) {
	/* remove just this entry */
	TAILQ_REMOVE(&list->el_entries, ep, ee_link);
	free(ep, M_EVENTHANDLER);
    } else {
	/* remove entire list */
	while (!TAILQ_EMPTY(&list->el_entries)) {
	    ep = TAILQ_FIRST(&list->el_entries);
	    TAILQ_REMOVE(&list->el_entries, ep, ee_link);
	    free(ep, M_EVENTHANDLER);
	}
    }
}

struct eventhandler_list *
eventhandler_find_list(char *name)
{
    struct eventhandler_list	*list;

    /* scan looking for the requested list */
    for (list = TAILQ_FIRST(&eventhandler_lists); 
	 list != NULL; 
	 list = TAILQ_NEXT(list, el_link)) {
	if (!strcmp(name, list->el_name))
	    break;
    }
    return(list);
}
