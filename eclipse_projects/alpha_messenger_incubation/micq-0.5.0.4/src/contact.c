/*
 * This file implements the contact list and basic operations on it.
 *
 * mICQ Copyright (C) © 2001-2005 Rüdiger Kuhlmann
 *
 * mICQ is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 dated June, 1991.
 *
 * mICQ is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
 * License for more details.
 *
 * In addition, as a special exception permission is granted to link the
 * code of this release of mICQ with the OpenSSL project's "OpenSSL"
 * library, and distribute the linked executables.  You must obey the GNU
 * General Public License in all respects for all of the code used other
 * than "OpenSSL".  If you modify this file, you may extend this exception
 * to your version of the file, but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your version
 * of this file.
 *
 * You should have received a copy of the GNU General Public License
 * along with this package; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * $Id: contact.c,v 1.121 2005/05/03 22:06:39 kuhlmann Exp $
 */

#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>

#include "micq.h"
#include "contact.h"
#include "connection.h"
#include "util_ui.h"     /* for Debug() and DEB_CONTACT */
#include "conv.h"        /* for meta data file recoding */
#include "util_io.h"     /* for UtilIOReadline() */
#include "packet.h"      /* for capabilities */
#include "buildmark.h"   /* for versioning */
#include "preferences.h" /* for BASEDIR */
#include "util_parse.h"

static ContactGroup **cnt_groups = NULL;
static int            cnt_count = 0;

#define MAX_ENTRIES 32

#define CONTACTGROUP_GLOBAL      (cnt_groups[0])
#define CONTACTGROUP_NONCONTACTS (cnt_groups[1])

#define BUILD_MIRANDA  0xffffffffUL
#define BUILD_MOBICQ   0xfffffffeUL
#define BUILD_STRICQ   0xffffff8fUL
#define BUILD_YSM      0xffffffabUL
#define BUILD_ARQ      0xffffff7fUL
#define BUILD_VICQ     0x04031980UL
#define BUILD_ALICQ    0xffffffbeUL

#define BUILD_LICQ     0x7d000000UL
#define BUILD_SSL      0x00800000UL

#define BUILD_TRILLIAN_ID1  0x3b75ac09UL
#define BUILD_TRILLIAN_ID2  0x3bae70b6UL
#define BUILD_TRILLIAN_ID3  0x3b744adbUL

#define BUILD_LIBICQ2K_ID1  0x3aa773eeUL
#define BUILD_LIBICQ2K_ID2  0x3aa66380UL
#define BUILD_LIBICQ2K_ID3  0x3a877a42UL

#define BUILD_KXICQ_ID1     0x3b4c4c0cUL
#define BUILD_KXICQ_ID2     0UL
#define BUILD_KXICQ_ID3     0x3b7248edUL

#define BUILD_KXICQ2_ID1    0x3aa773eeUL
#define BUILD_KXICQ2_ID2    0x3aa66380UL
#define BUILD_KXICQ2_ID3    0x3a877a42UL

/*
 * Initializes the contact group table.
 */
static void ContactGroupInit (void)
{
    if (!cnt_groups)
    {
        cnt_groups = calloc (cnt_count = 32, sizeof (ContactGroup *));
        cnt_groups[0] = calloc (1, sizeof (ContactGroup));
        cnt_groups[1] = calloc (1, sizeof (ContactGroup));
    }
}

#undef ContactGroupC
ContactGroup *ContactGroupC (Connection *conn, UWORD id, const char *name DEBUGPARAM)
{
    ContactGroup *cg;
    int i;
    
    if (!cnt_groups)
        ContactGroupInit ();

    for (i = 1; i < cnt_count; i++)
        if (!cnt_groups[i])
            break;

    if (i >= cnt_count - 1)
    {
        ContactGroup **ncgp = realloc (cnt_groups, sizeof (ContactGroup *) * (2 * cnt_count));
        if (!ncgp)
            return NULL;
        cnt_groups = ncgp;
        cnt_groups[i] = NULL;
        cnt_count *= 2;
    }

    if (!(cg = calloc (1, sizeof (ContactGroup))))
        return NULL;

    cnt_groups[i + 1] = NULL;
    cnt_groups[i] = cg;
    cg->serv = conn;
    cg->id = id;
    s_repl (&cg->name, name);
    Debug (DEB_CONTACT, "grpadd #%d %p %p '%s'", i, cg, conn, cg->name);
    return cg;
}

/*
 * Iterates through the contact groups.
 */
ContactGroup *ContactGroupIndex (int i)
{
    if (!cnt_groups)
        ContactGroupInit ();
    if (i >= 0 && i + 1 < cnt_count && cnt_groups[i + 1])
        return cnt_groups[i + 1];
    return NULL;
}

/*
 * Finds and/or creates a contact group.
 */
ContactGroup *ContactGroupFind (Connection *serv, UWORD id, const char *name)
{
    ContactGroup *cg;
    int i;
    
    if (!cnt_groups)
        ContactGroupInit ();
    for (i = 1; i < cnt_count; i++)
        if (    (cg = cnt_groups[i])
            && (!id   || cg->id   == id)
            && (!serv || cg->serv == serv)
            && (!name || !strcmp (cg->name, name)))
        {
            return cg;
        }
    return NULL;
}

/*
 * Returns the group id, if necessary create one
 */
UWORD ContactGroupID (ContactGroup *group)
{
    while (!group->id)
    {
        ContactGroup *cg;
        int i;

        group->id = 16 + rand() % 0x7fef;
        for (i = 0; (cg = ContactGroupIndex (i)); i++)
            if (cg->id == group->id && cg != group)
                group->id = 0;
    }
    return group->id;
}

/*
 * Count the number of contacts in this list.
 */
UDWORD ContactGroupCount (ContactGroup *group)
{
    UDWORD c = 0;
    while (group)
    {
        c += group->used;
        group = group->more;
    }
    return c;
}

/*
 * Remove a contact group.
 */
#undef ContactGroupD
void ContactGroupD (ContactGroup *group DEBUGPARAM)
{
    ContactGroup *tmp;
    int i;
    
    if (!cnt_groups)
        ContactGroupInit ();
    for (i = 1; i < cnt_count && cnt_groups[i]; i++)
        if (cnt_groups[i] == group)
        {
            Debug (DEB_CONTACT, "grprem #%d %p", i, group);
            memmove (cnt_groups + i, cnt_groups + i + 1, (cnt_count - i - 1) * sizeof (ContactGroup *));
            s_repl (&group->name, NULL);
            while (group)
            {
                tmp = group->more;
                free (group);
                group = tmp;
            }
            break;
        }
}

/*
 * Iterate through contacts on a contact group
 */
Contact *ContactIndex (ContactGroup *group, int i)
{
    static ContactGroup *old = NULL, *oldpos = NULL;
    static int oldoff = 0;
    ContactGroup *orig;
    int j = 0;
    
    if (!group)
    {
        if (!cnt_groups)
            ContactGroupInit ();
        group = CONTACTGROUP_GLOBAL;
    }
    orig = group;
    if (old == group && oldoff <= i)
    {
        group = oldpos;
        i -= oldoff;
        j = oldoff;
    }
    while (group && group->used <= i)
    {
        i -= group->used;
        j += group->used;
        group = group->more;
    }
    if (!group)
        return NULL;
    old = orig;
    oldpos = group;
    oldoff = j;
    return group->contacts[i];
}

static Contact *ContactC (UWORD id, UDWORD uin, const char *nick DEBUGPARAM)
{
    Contact *cont;

    if (!(cont = calloc (1, sizeof (Contact))))
        return NULL;

    cont->uin = uin;
    cont->ids = NULL;
    cont->status = STATUS_OFFLINE;
    
    s_repl (&cont->nick, nick ? nick : s_sprintf ("%ld", uin));

    Debug (DEB_CONTACT, "new   #%d %ld '%s' %p", id, uin, nick, cont);
    ContactAdd (CONTACTGROUP_GLOBAL, cont);
    return cont;
}

/*
 * Finds a contact for a connection
 */
#undef ContactScreen
Contact *ContactScreen (Connection *conn, const char *screen DEBUGPARAM)
{
    Contact *cont;
    UDWORD uin = atoi (screen);
    
    if (!conn || !conn->contacts || !uin)
        return NULL;

    if (!cnt_groups)
        ContactGroupInit ();

    if (uin && (cont = ContactFind (conn->contacts, 0, uin, NULL)))
        return cont;
    
    if ((cont = ContactFind (conn->contacts, 0, uin, screen)))
        return cont;
    
    if (uin && (cont = ContactFind (CONTACTGROUP_NONCONTACTS, 0, uin, NULL)))
        return cont;

    if ((cont = ContactFind (CONTACTGROUP_NONCONTACTS, 0, uin, screen)))
        return cont;

    if (!(cont = ContactC (0, uin, screen DEBUGFOR)))
        return NULL;

    ContactAdd (CONTACTGROUP_NONCONTACTS, cont);
    return cont;
}

/*
 * Finds a contact for a connection
 */
#undef ContactUIN
Contact *ContactUIN (Connection *conn, UDWORD uin DEBUGPARAM)
{
    Contact *cont;
    
    if (!conn || !conn->contacts || !uin)
        return NULL;

    if (!cnt_groups)
        ContactGroupInit ();

    if ((cont = ContactFind (conn->contacts, 0, uin, NULL)))
        return cont;
    
    if ((cont = ContactFind (CONTACTGROUP_NONCONTACTS, 0, uin, NULL)))
        return cont;

    if (!(cont = ContactC (0, uin, NULL DEBUGFOR)))
        return NULL;

    ContactAdd (CONTACTGROUP_NONCONTACTS, cont);
    return cont;
}

/*
 * Finds a contact on a contact group
 */
Contact *ContactFind (ContactGroup *group, UWORD id, UDWORD uin, const char *nick)
{
    ContactGroup *tmp;
    ContactAlias *ca;
    Contact *cont;
    int i;
    
    if (!group && !cnt_groups)
        ContactGroupInit ();

    for (tmp = group ? group : CONTACTGROUP_GLOBAL; tmp; tmp = tmp->more)
    {
        for (i = 0; i < tmp->used; i++)
        {
            cont = tmp->contacts[i];
            if (uin && uin != cont->uin)
                continue;
            if (id)
            {
                ContactIDs *ids;
                for (ids = cont->ids; ids; ids = ids->next)
                    if (ids->id == id)
                        break;
                if (!ids)
                    continue;
            }
            if (!nick || !strcmp (nick, cont->nick))
                return cont;
            for (ca = cont->alias; ca; ca = ca->more)
                if (!strcmp (nick, ca->alias))
                    return cont;
        }
    }
    return NULL;
}

#undef ContactCreate
void ContactCreate (Connection *serv, Contact *cont DEBUGPARAM)
{
    if (!serv || !cont || cont->group)
        return;

    ContactRem (CONTACTGROUP_NONCONTACTS, cont);
    ContactAdd (serv->contacts, cont);
    cont->group = serv->contacts;
    Debug (DEB_CONTACT, "accc  #%d %ld '%s' %p in %p", 0, cont->uin, cont->nick, cont, serv->contacts);
}


/*
 * Finds a contact on a contact group, possibly creating it
 */
#undef ContactFindCreate
Contact *ContactFindCreate (ContactGroup *group, UWORD id, UDWORD uin, const char *nick DEBUGPARAM)
{
    Contact *cont;
    
    if (!group && !cnt_groups)
        ContactGroupInit ();

    assert (id || uin);
    assert (group);

    if ((cont = ContactFind (group, id, uin, NULL)))
    {
        if (nick)
            ContactAddAlias (cont, nick);
        return cont;
    }
    
    if ((cont = ContactFind (CONTACTGROUP_NONCONTACTS, id, uin, NULL)))
    {
        if (!nick)
            return cont;
        ContactRem (CONTACTGROUP_NONCONTACTS, cont);
        ContactAdd (group, cont);
        cont->group = group;
        s_repl (&cont->nick, nick);
        Debug (DEB_CONTACT, "act   #%d %ld '%s' %p in %p", id, uin, nick, cont, group);
        return cont;
    }
    
    if (!(cont = ContactC (id, uin, nick DEBUGFOR)))
        return NULL;

    if (nick)
    {
        ContactAdd (group, cont);
        cont->group = group;
        Debug (DEB_CONTACT, "act   #%d %ld '%s' %p", id, uin, nick, cont);
    }
    else
    {
        ContactAdd (CONTACTGROUP_NONCONTACTS, cont);
        Debug (DEB_CONTACT, "temp  #%d %ld %p", id, uin, cont);
    }

    return cont;
}

/*
 * Delete a contact by hiding it as good as possible.
 */
#undef ContactD
void ContactD (Contact *cont DEBUGPARAM)
{
    ContactAlias *ca, *cao;
    ContactGroup *cg;
    ContactIDs *ids, *tids;
    int i;

    for (ca = cont->alias; ca; ca = cao)
    {
        cao = ca->more;
        free (ca->alias);
        free (ca);
    }
    
    for (ids = cont->ids; ids; ids = tids)
    {
        tids = ids->next;
        free (ids);
    }
    cont->ids = NULL;
    cont->alias = NULL;
    s_repl (&cont->nick, s_sprintf ("%ld", cont->uin));
    
    for (i = 0; (cg = ContactGroupIndex (i)); i++)
        ContactRem (cg, cont);
    ContactAdd (CONTACTGROUP_NONCONTACTS, cont);
    cont->group = NULL;
    Debug (DEB_CONTACT, "del   #%d %ld %p", 0, cont->uin, cont);
}


/*
 * Adds a contact to a contact group.
 */
#undef ContactAdd
BOOL ContactAdd (ContactGroup *group, Contact *cont DEBUGPARAM)
{
    ContactGroup *orig = group;
    if (!group)
    {
        if (!cnt_groups)
            ContactGroupInit ();
        group = CONTACTGROUP_GLOBAL;
    }
    if (!group || !cont)
        return FALSE;
    while (group->used == MAX_ENTRIES && group->more)
        group = group->more;
    if (group->used == MAX_ENTRIES)
    {
        group->more = calloc (1, sizeof (ContactGroup));
        if (!group->more)
            return FALSE;
        group = group->more;
    }
    group->contacts[group->used++] = cont;
    Debug (DEB_CONTACT, "add   #%d %ld '%s' %p to %p", 0, cont->uin, cont->nick, cont, orig);
    return TRUE;
}

/*
 * Removes a contact from a contact group.
 */
BOOL ContactHas (ContactGroup *group, Contact *cont)
{
    int i;

    if (!group)
    {
        if (!cnt_groups)
            ContactGroupInit ();
        group = CONTACTGROUP_GLOBAL;
    }
    if (!group || !cont)
        return FALSE;
    while (group)
    {
        for (i = 0; i < group->used; i++)
            if (group->contacts[i] == cont)
                return TRUE;
        group = group->more;
    }
    return FALSE;
}

/*
 * Removes a contact from a contact group.
 */
#undef ContactRem
BOOL ContactRem (ContactGroup *group, Contact *cont DEBUGPARAM)
{
    ContactGroup *orig = group;
    int i;

    if (!group)
    {
        if (!cnt_groups)
            ContactGroupInit ();
        group = CONTACTGROUP_GLOBAL;
    }
    if (!group || !cont)
        return FALSE;
    while (group)
    {
        for (i = 0; i < group->used; i++)
            if (group->contacts[i] == cont)
            {
                if (orig->temp)
                    group->contacts[i] = NULL;
                else
                {
                    group->contacts[i] = group->contacts[--group->used];
                    group->contacts[group->used] = NULL;
                }
                Debug (DEB_CONTACT, "rem   #%d %ld '%s' %p to %p", 0, cont->uin, cont->nick, cont, orig);
                return TRUE;
            }
        group = group->more;
    }
    return FALSE;
}

/*
 * Adds an alias to a contact.
 */
#undef ContactAddAlias
BOOL ContactAddAlias (Contact *cont, const char *nick DEBUGPARAM)
{
    ContactAlias **caref;
    ContactAlias *ca;
    
    if (!nick)
        return FALSE;

    if (!strcmp (cont->nick, nick))
        return TRUE;
    
    for (caref = &cont->alias; *caref; caref = &(*caref)->more)
        if (!strcmp ((*caref)->alias, nick))
            return TRUE;
    
    if (!strcmp (cont->nick, s_sprintf ("%ld", cont->uin)))
    {
        s_repl (&cont->nick, nick);
        return TRUE;
    }
    
    if (!strcmp (nick, s_sprintf ("%ld", cont->uin)))
        return TRUE;
    
    ca = calloc (1, sizeof (ContactAlias));
    if (!ca)
        return FALSE;
    
    ca->alias = strdup (nick);
    if (!ca->alias)
    {
        free (ca);
        return FALSE;
    }
    *caref = ca;
    Debug (DEB_CONTACT, "addal #%d %ld '%s' A'%s'", 0, cont->uin, cont->nick, nick);
    return TRUE;
}


/*
 * Removes an alias from a contact.
 */
#undef ContactRemAlias
BOOL ContactRemAlias (Contact *cont, const char *nick DEBUGPARAM)
{
    ContactAlias **caref;
    ContactAlias *ca;

    if (!strcmp (cont->nick, nick))
    {
        char *nn;
        
        if (cont->alias)
        {
            ca = cont->alias;
            nn = cont->alias->alias;
            cont->alias = cont->alias->more;
        }
        else
            nn = strdup ("");
        free (cont->nick);
        cont->nick = nn;
        Debug (DEB_CONTACT, "remal #%d %ld N'%s' '%s'", 0, cont->uin, cont->nick, nick);
        return TRUE;
    }

    for (caref = &cont->alias; *caref; caref = &(*caref)->more)
        if (!strcmp ((*caref)->alias, nick))
        {
            free ((*caref)->alias);
            ca = *caref;
            *caref = (*caref)->more;
            free (ca);
            Debug (DEB_CONTACT, "remal #%d %ld '%s' X'%s'", 0, cont->uin, cont->nick, nick);
            return TRUE;
        }

    return FALSE;
}

/*
 * Returns the contact id for flag, if necessary create one
 */
ContactIDs *ContactID (Contact *cont, UWORD type)
{
    ContactIDs **ids;
    ContactIDs *id;
    
    for (ids = &cont->ids; *ids; ids = &((*ids)->next))
        if ((*ids)->type == type)
            break;
    
    if (!(id = *ids))
    {
        id = *ids = calloc (1, sizeof (ContactIDs));
        id->type = type;
    }
    return id;
}

/*
 * Returns the contact id for type, if necessary create one
 */
UWORD ContactIDGet (Contact *cont, UWORD type)
{
    ContactIDs *id, *idt;

    id = ContactID (cont, type);    
    while (!id->id)
    {
        Contact *c;
        UWORD newid;
        int i;

        newid = 16 + rand() % 0x7fef;
        for (i = 0; (c = ContactIndex (NULL, i)); i++)
            for (idt = c->ids; idt; idt = idt->next)
                if (idt->id == id->id)
                    id->id = 0;
        id->id = newid;
    }
    id->tag = 0;
    id->issbl = 0;
    return id->id;
}

/*
 * Set the contact id for type, if necessary create one
 */
void ContactIDSet (Contact *cont, UWORD type, UWORD id, UWORD tag)
{
    ContactIDs *idp, *idt;
    Contact *c;
    int i;
    
    idp = ContactID (cont, type);
    for (i = 0; (c = ContactIndex (NULL, i)); i++)
        for (idt = c->ids; idt; idt = idt->next)
            if (idt->id == id)
            {
                idt->id = 0;
                idt->tag = 0;
                idt->issbl = 0;
            }
    idp->id = id;
    idp->tag = tag;
    idp->issbl = 1;
}

/*
 * Save the contact's meta data to disc.
 */
BOOL ContactMetaSave (Contact *cont)
{
    ContactMeta *m;
    FILE *f;
    
    if (!(f = fopen (s_sprintf ("%scontacts" _OS_PATHSEPSTR "icq-%ld", PrefUserDir (prG), cont->uin), "w")))
    {
        mkdir (s_sprintf ("%scontacts", PrefUserDir (prG)), 0700);
        if (!(f = fopen (s_sprintf ("%scontacts" _OS_PATHSEPSTR "icq-%ld", PrefUserDir (prG), cont->uin), "w")))
            return FALSE;
    }
    fprintf (f, "#\n# Meta data for contact %ld.\n#\n\n", cont->uin);
    fprintf (f, "encoding UTF-8\n"); 
    fprintf (f, "format 1\n\n");
    fprintf (f, "b_uin      %ld\n", cont->uin);
    fprintf (f, "b_nick     %s\n", s_quote (cont->nick));
    if (cont->meta_about)
        fprintf (f, "b_about    %s\n", s_quote (cont->meta_about));
    if (cont->meta_general)
    {
        MetaGeneral *mg = cont->meta_general;
        fprintf (f, "g_nick     %s\n", s_quote (mg->nick));
        fprintf (f, "g_first    %s\n", s_quote (mg->first));
        fprintf (f, "g_last     %s\n", s_quote (mg->last));
        fprintf (f, "g_email    %s\n", s_quote (mg->email));
        fprintf (f, "g_city     %s\n", s_quote (mg->city));
        fprintf (f, "g_state    %s\n", s_quote (mg->state));
        fprintf (f, "g_phone    %s\n", s_quote (mg->phone));
        fprintf (f, "g_fax      %s\n", s_quote (mg->fax));
        fprintf (f, "g_zip      %s\n", s_quote (mg->zip));
        fprintf (f, "g_street   %s\n", s_quote (mg->street));
        fprintf (f, "g_cell     %s\n", s_quote (mg->cellular));
        fprintf (f, "g_country  %u\n", mg->country);
        fprintf (f, "g_tz       %d\n", mg->tz);
        fprintf (f, "g_flags    %u\n", (mg->auth ? 1 : 0)
                                    + (mg->webaware ? 2 : 0)
                                    + (mg->hideip ? 4 : 0));
    }
    if (cont->meta_work)
    {
        MetaWork *mw = cont->meta_work;
        fprintf (f, "w_city     %s\n", s_quote (mw->wcity));
        fprintf (f, "w_state    %s\n", s_quote (mw->wstate));
        fprintf (f, "w_phone    %s\n", s_quote (mw->wphone));
        fprintf (f, "w_fax      %s\n", s_quote (mw->wfax));
        fprintf (f, "w_address  %s\n", s_quote (mw->waddress));
        fprintf (f, "w_zip      %s\n", s_quote (mw->wzip));
        fprintf (f, "w_company  %s\n", s_quote (mw->wcompany));
        fprintf (f, "w_depart   %s\n", s_quote (mw->wdepart));
        fprintf (f, "w_position %s\n", s_quote (mw->wposition));
        fprintf (f, "w_homepage %s\n", s_quote (mw->whomepage));
        fprintf (f, "w_country  %u\n", mw->wcountry);
        fprintf (f, "w_occup    %u\n", mw->woccupation);
    }
    if (cont->meta_more)
    {
        MetaMore *mm = cont->meta_more;
        fprintf (f, "m_homepage %s\n", s_quote (mm->homepage));
        fprintf (f, "m_age      %u\n", mm->age);
        fprintf (f, "m_year     %u\n", mm->year);
        fprintf (f, "m_unknown  %u\n", mm->unknown);
        fprintf (f, "m_sex      %u\n", mm->sex);
        fprintf (f, "m_month    %u\n", mm->month);
        fprintf (f, "m_day      %u\n", mm->day);
        fprintf (f, "m_lang1    %u\n", mm->lang1);
        fprintf (f, "m_lang2    %u\n", mm->lang2);
        fprintf (f, "m_lang3    %u\n", mm->lang3);
    }
    if (cont->meta_obsolete)
        fprintf (f, "obsolete %u %u %u %s\n", cont->meta_obsolete->given,
                 cont->meta_obsolete->empty, cont->meta_obsolete->unknown,
                 s_quote (cont->meta_obsolete->description));
    for (m = cont->meta_email; m; m = m->next)
        fprintf (f, "email %u %s\n", m->data, s_quote (m->text));
    for (m = cont->meta_interest; m; m = m->next)
        fprintf (f, "interest %u %s\n", m->data, s_quote (m->text));
    for (m = cont->meta_background; m; m = m->next)
        fprintf (f, "background %u %s\n", m->data, s_quote (m->text));
    for (m = cont->meta_affiliation; m; m = m->next)
        fprintf (f, "affiliation %u %s\n", m->data, s_quote (m->text));
    if (fclose (f))
        return FALSE;
    cont->updated |= UPF_DISC;
    return TRUE;
}

/*
 * Destruct a contact meta list
 */
void ContactMetaD (ContactMeta *m)
{
    ContactMeta *mm;
    while (m)
    {
        if (m->text)
            free (m->text);
        mm = m->next;
        free (m);
        m = mm;
    }
}

/*
 * Add an entry to a contact meta list
 */
void ContactMetaAdd (ContactMeta **m, UWORD val, const char *text)
{
    while (*m)
        m = &(*m)->next;
    *m = calloc (4 + sizeof (ContactMeta), 1);
    (*m)->data = val;
    (*m)->text = strdup (text);
}


/*
 *
 */
BOOL ContactMetaLoad (Contact *cont)
{
    UBYTE enc;
    FILE *f;
    char *cmd;
    strc_t par, lline;
    const char *line;
    UDWORD i;
    
    if (!(f = fopen (s_sprintf ("%scontacts" _OS_PATHSEPSTR "icq-%ld", PrefUserDir (prG), cont->uin), "r")))
        return FALSE;

    cont->updated = 0;
    ContactMetaD (cont->meta_email);
    ContactMetaD (cont->meta_interest);
    ContactMetaD (cont->meta_background);
    ContactMetaD (cont->meta_affiliation);
    cont->meta_email = NULL;
    cont->meta_interest = NULL;
    cont->meta_background = NULL;
    cont->meta_affiliation = NULL;
    
    enc = ENC_UTF8;
    while ((lline = UtilIOReadline (f)))
    {
        if (!lline->len || (lline->txt[0] == '#'))
            continue;
        line = ConvFrom (lline, enc)->txt;
        if (!(par = s_parse (&line)))
             continue;
        cmd = par->txt;
        if (!strcmp (cmd, "encoding"))
        {
            if (!(par = s_parse (&line)))
                return FALSE;
            enc = ConvEnc (par->txt);
            if (enc & ENC_FERR && (enc ^ prG->enc_loc) & ~ENC_FLAGS)
                return FALSE;
            enc &= ~ENC_FLAGS;
        }
        else if (!enc)
            return FALSE;
        else if (!strncmp (cmd, "b_", 2))
        {
            if      (!strcmp (cmd, "b_uin"))   { if (s_parseint (&line, &i) && i != cont->uin) return FALSE; }
            else if (!strcmp (cmd, "b_id"))    { s_parseint (&line, &i); /* deprecated */ }
            else if (!strcmp (cmd, "b_nick"))  { s_parse (&line); /* ignore for now */ }
            else if (!strcmp (cmd, "b_alias")) { s_parse (&line); /* deprecated */ }
            else if (!strcmp (cmd, "b_enc"))   { s_parse (&line); /* deprecated */ }
            else if (!strcmp (cmd, "b_flags")) { s_parseint (&line, &i); /* ignore for compatibility */ }
            else if (!strcmp (cmd, "b_about")) { if ((par = s_parse (&line)))  s_repl (&cont->meta_about, par->txt); }
            else if (!strcmp (cmd, "b_seen"))  { s_parseint (&line, &i); /* deprecated */ }
            else if (!strcmp (cmd, "b_micq"))  { s_parseint (&line, &i); /* deprecated */ }
        }
        else if (!strncmp (cmd, "g_", 2))
        {
            MetaGeneral *mg = CONTACT_GENERAL (cont);
            if      (!strcmp (cmd, "g_nick"))    { if ((par = s_parse (&line)))  s_repl (&mg->first,    par->txt); }
            else if (!strcmp (cmd, "g_first"))   { if ((par = s_parse (&line)))  s_repl (&mg->first,    par->txt); }
            else if (!strcmp (cmd, "g_last"))    { if ((par = s_parse (&line)))  s_repl (&mg->last,     par->txt); }
            else if (!strcmp (cmd, "g_email"))   { if ((par = s_parse (&line)))  s_repl (&mg->email,    par->txt); }
            else if (!strcmp (cmd, "g_city"))    { if ((par = s_parse (&line)))  s_repl (&mg->city,     par->txt); }
            else if (!strcmp (cmd, "g_state"))   { if ((par = s_parse (&line)))  s_repl (&mg->state,    par->txt); }
            else if (!strcmp (cmd, "g_phone"))   { if ((par = s_parse (&line)))  s_repl (&mg->phone,    par->txt); }
            else if (!strcmp (cmd, "g_fax"))     { if ((par = s_parse (&line)))  s_repl (&mg->fax,      par->txt); }
            else if (!strcmp (cmd, "g_zip"))     { if ((par = s_parse (&line)))  s_repl (&mg->zip,      par->txt); }
            else if (!strcmp (cmd, "g_street"))  { if ((par = s_parse (&line)))  s_repl (&mg->street,   par->txt); }
            else if (!strcmp (cmd, "g_cell"))    { if ((par = s_parse (&line)))  s_repl (&mg->cellular, par->txt); }
            else if (!strcmp (cmd, "g_country")) { if (s_parseint (&line, &i)) mg->country = i; }
            else if (!strcmp (cmd, "g_tz"))      { if (s_parseint (&line, &i)) mg->tz = i; }
            else if (!strcmp (cmd, "g_flags"))   { if (s_parseint (&line, &i))
            {
                mg->auth     = i & 1 ? 1 : 0;
                mg->webaware = i & 2 ? 1 : 0;
                mg->hideip   = i & 4 ? 1 : 0;
            }}
        }
        else if (!strncmp (cmd, "w_", 2))
        {
            MetaWork *mw = CONTACT_WORK (cont);
            if      (!strcmp (cmd, "w_city"))     { if ((par = s_parse (&line)))  s_repl (&mw->wcity,     par->txt); }
            else if (!strcmp (cmd, "w_state"))    { if ((par = s_parse (&line)))  s_repl (&mw->wstate,    par->txt); }
            else if (!strcmp (cmd, "w_phone"))    { if ((par = s_parse (&line)))  s_repl (&mw->wphone,    par->txt); }
            else if (!strcmp (cmd, "w_fax"))      { if ((par = s_parse (&line)))  s_repl (&mw->wfax,      par->txt); }
            else if (!strcmp (cmd, "w_address"))  { if ((par = s_parse (&line)))  s_repl (&mw->waddress,  par->txt); }
            else if (!strcmp (cmd, "w_zip"))      { if ((par = s_parse (&line)))  s_repl (&mw->wzip,      par->txt); }
            else if (!strcmp (cmd, "w_company"))  { if ((par = s_parse (&line)))  s_repl (&mw->wcompany,  par->txt); }
            else if (!strcmp (cmd, "w_depart"))   { if ((par = s_parse (&line)))  s_repl (&mw->wdepart,   par->txt); }
            else if (!strcmp (cmd, "w_position")) { if ((par = s_parse (&line)))  s_repl (&mw->wposition, par->txt); }
            else if (!strcmp (cmd, "w_homepage")) { if ((par = s_parse (&line)))  s_repl (&mw->whomepage, par->txt); }
            else if (!strcmp (cmd, "w_country"))  { if (s_parseint (&line, &i)) mw->wcountry = i; }
            else if (!strcmp (cmd, "w_occup"))    { if (s_parseint (&line, &i)) mw->woccupation = i; }
        }
        else if (!strncmp (cmd, "m_", 2))
        {
            MetaMore *mm = CONTACT_MORE (cont);
            if      (!strcmp (cmd, "m_homepage")) { if ((par = s_parse (&line)))  s_repl (&mm->homepage, par->txt); }
            else if (!strcmp (cmd, "m_age"))      { if (s_parseint (&line, &i)) mm->age = i; }
            else if (!strcmp (cmd, "m_year"))     { if (s_parseint (&line, &i)) mm->year = i; }
            else if (!strcmp (cmd, "m_unknown"))  { if (s_parseint (&line, &i)) mm->unknown = i; }
            else if (!strcmp (cmd, "m_sex"))      { if (s_parseint (&line, &i)) mm->sex = i; }
            else if (!strcmp (cmd, "m_month"))    { if (s_parseint (&line, &i)) mm->month = i; }
            else if (!strcmp (cmd, "m_day"))      { if (s_parseint (&line, &i)) mm->day = i; }
            else if (!strcmp (cmd, "m_lang1"))    { if (s_parseint (&line, &i)) mm->lang1 = i; }
            else if (!strcmp (cmd, "m_lang2"))    { if (s_parseint (&line, &i)) mm->lang2 = i; }
            else if (!strcmp (cmd, "m_lang3"))    { if (s_parseint (&line, &i)) mm->lang3 = i; }
        }
        else if (!strcmp (cmd, "obsolete"))
        {
            MetaObsolete *mo = CONTACT_OBSOLETE (cont);
            if (s_parseint (&line, &i)) mo->given = i;
            if (s_parseint (&line, &i)) mo->empty = i;
            if (s_parseint (&line, &i)) mo->unknown = i;
            if ((par = s_parse (&line))) s_repl (&mo->description, par->txt);
        }
        else if (!strcmp (cmd, "email"))
        {
            if (s_parseint (&line, &i) && (par = s_parse (&line)))
                ContactMetaAdd (&cont->meta_email, i, par->txt);
        }
        else if (!strcmp (cmd, "interest"))
        {
            if (s_parseint (&line, &i) && (par = s_parse (&line)))
                ContactMetaAdd (&cont->meta_interest, i, par->txt);
        }
        else if (!strcmp (cmd, "background"))
        {
            if (s_parseint (&line, &i) && (par = s_parse (&line)))
                ContactMetaAdd (&cont->meta_background, i, par->txt);
        }
        else if (!strcmp (cmd, "affiliation"))
        {
            if (s_parseint (&line, &i) && (par = s_parse (&line)))
                ContactMetaAdd (&cont->meta_affiliation, i, par->txt);
        }
        else if (!strcmp (cmd, "format"))
        {
            s_parseint (&line, &i); /* ignored for now */
        }
#ifdef WIP
        if ((par = s_parse (&line)))
            rl_printf ("FIXMEWIP: Ignored trailing stuff: '%s' from '%s'.\n", par->txt, line);
#endif
    }
    if (fclose (f))
        return FALSE;
    cont->updated |= UPF_DISC;
    return TRUE;
}

/*
 * Query an option for a contact group.
 */
val_t ContactGroupPrefVal (ContactGroup *cg, UDWORD flag)
{
    val_t res = 0;
    
    if (cg)
    {
        if (OptGetVal (&cg->copts, flag, &res))
            return res;
        if (cg->serv && OptGetVal (&cg->serv->contacts->copts, flag, &res))
            return res;
    }

    if (OptGetVal (&prG->copts, flag, &res))
        return res;
    return 0;
}

/*
 * Query an option for a contact.
 */
val_t ContactPrefVal (Contact *cont, UDWORD flag)
{
    val_t res = 0;
    
    if (cont)
    {
        if (OptGetVal (&cont->copts, flag, &res))
            return res;
        if (cont->group)
        {
            if (OptGetVal (&cont->group->copts, flag, &res))
                return res;
            if (cont->group->serv && OptGetVal (&cont->group->serv->contacts->copts, flag, &res))
                return res;
        }
    }
    if (OptGetVal (&prG->copts, flag, &res))
        return res;
    return 0;
}

/*
 * Query a string option for a contact.
 */
const char *ContactPrefStr (Contact *cont, UDWORD flag)
{
    const char *res = NULL;
    
    if (cont)
    {
        if (OptGetStr (&cont->copts, flag, &res))
            return res;
        if (cont->group)
        {
            if (OptGetStr (&cont->group->copts, flag, &res))
                return res;
            if (cont->group->serv && OptGetStr (&cont->group->serv->contacts->copts, flag, &res))
                return res;
        }
    }
    if (OptGetStr (&prG->copts, flag, &res))
        return res;
    if (~flag & COF_COLOR || flag == CO_COLORNONE)
        return "";
    if (cont)
    {
        if (OptGetStr (&cont->copts, CO_COLORNONE, &res))
            return res;
        if (cont->group)
        {
            if (OptGetStr (&cont->group->copts, CO_COLORNONE, &res))
                return res;
            if (cont->group->serv && OptGetStr (&cont->group->serv->contacts->copts, CO_COLORNONE, &res))
                return res;
        }
    }
    if (OptGetStr (&prG->copts, CO_COLORNONE, &res))
        return res;
    return "";
}

/*
 * Set a capability for the contact.
 */
void ContactSetCap (Contact *cont, Cap *cap)
{
    if (!cap->id)
        return;
    if (cap->var && cap->id == CAP_SIM)
    {
        UBYTE ver;
        
        ver = cap->var[15];
        if (ver >> 6) /* old SIM */
        {
            cont->v1 = (ver >> 6) - 1;
            cont->v2 = ver & 0x1f;
            cont->v3 = cont->v4 = 0;
            if (ver <= 0x48)
                CLR_CAP (cont->caps, CAP_UTF8);
        }
        else /* old KOPETE */
        {
            cont->v1 = 0;
            cont->v3 = ver & 0x1f;
            cont->v2 = cont->v4 = 0;
            if (ver <= 1)
                CLR_CAP (cont->caps, CAP_UTF8);
        }
    }
    else if (cap->var && (cap->id == CAP_MICQ || cap->id == CAP_SIMNEW
                       || cap->id == CAP_KOPETE || cap->id == CAP_LICQNEW))
    {
        cont->v1 = cap->var[12];
        cont->v2 = cap->var[13];
        cont->v3 = cap->var[14];
        cont->v4 = cap->var[15];
    }
    SET_CAP (cont->caps, cap->id);
}

/*
 * Guess the contacts client from time stamps.
 */
void ContactSetVersion (Contact *cont)
{
    char buf[100];
    char *new = NULL, *tail = NULL;
    unsigned int ver;
    ContactDC *dc;
    
    if (!(dc = cont->dc))
    {
        s_repl (&cont->version, NULL);
        return;
    }
    
    ver = dc->id1 & 0xffff;
    
    if (!HAS_CAP (cont->caps, CAP_SIM) && !HAS_CAP (cont->caps, CAP_MICQ)
        && !HAS_CAP (cont->caps, CAP_SIMNEW) && !HAS_CAP (cont->caps, CAP_KOPETE)
        && !HAS_CAP (cont->caps, CAP_LICQNEW))
        cont->v1 = cont->v2 = cont->v3 = cont->v4 = 0;

    if ((dc->id1 & 0xff7f0000UL) == BUILD_LICQ && ver > 1000)
    {
        new = "licq";
        if (dc->id1 & BUILD_SSL)
            tail = "/SSL";
        cont->v1 = ver / 1000;
        cont->v2 = (ver / 10) % 100;
        cont->v3 = ver % 10;
        cont->v4 = 0;
    }
#ifdef WIP
    else if ((dc->id1 & 0xff7f0000UL) == BUILD_MICQ || (dc->id1 & 0xff7f0000UL) == BUILD_LICQ)
    {
        new = "mICQ";
        cont->v1 = ver / 10000;
        cont->v2 = (ver / 100) % 100;
        cont->v3 = (ver / 10) % 10;
        cont->v4 = ver % 10;
        if (ver >= 489 && dc->id2)
            dc->id1 = BUILD_MICQ;
    }
#endif

    else if ((dc->id1 & 0xffff0000UL) == 0xffff0000UL)
    {
        cont->v1 = (dc->id2 & 0x7f000000) >> 24;
        cont->v2 = (dc->id2 &   0xff0000) >> 16;
        cont->v3 = (dc->id2 &     0xff00) >> 8;
        cont->v4 =  dc->id2 &       0xff;
        switch ((UDWORD)dc->id1)
        {
            case BUILD_MIRANDA:
                if (dc->id2 <= 0x00010202 && dc->version >= 8)
                    dc->version = 7;
                new = "Miranda";
                if (dc->id2 & 0x80000000)
                    tail = " cvs";
                break;
            case BUILD_MOBICQ:
                new = "MobICQ";
                break;
            case BUILD_STRICQ:
                new = "StrICQ";
                break;
            case BUILD_MICQ:
                OptSetVal (&cont->copts, CO_TIMEMICQ, time (NULL));
                new = "mICQ";
                if (dc->id2 & 0x80000000)
                    tail = " cvs";
                break;
            case BUILD_YSM:
                new = "YSM";
                if ((cont->v1 | cont->v2 | cont->v3 | cont->v4) & 0x80)
                    cont->v1 = cont->v2 = cont->v3 = cont->v4 = 0;
                break;
            case BUILD_ARQ:
                new = "&RQ";
                break;
            case BUILD_ALICQ:
                new = "alicq";
                break;
            default:
                snprintf (buf, sizeof (buf), "%08lx", (long)dc->id1);
                new = buf;
        }
    }
    else if (dc->id1 == BUILD_VICQ)
    {
        cont->v1 = 0;
        cont->v2 = 43;
        cont->v3 =  dc->id2 &     0xffff;
        cont->v4 = (dc->id2 & 0x7fff0000) >> 16;
        new = "vICQ";
    }
    else if (dc->id1 == BUILD_TRILLIAN_ID1 &&
             dc->id2 == BUILD_TRILLIAN_ID2 &&
             dc->id3 == BUILD_TRILLIAN_ID3)
    {
        new = "Trillian";
        /* Trillian only understands unicode in type-1 messages */
        CLR_CAP (cont->caps, CAP_UTF8);
    }
    else if (dc->id1 == BUILD_LIBICQ2K_ID1 &&
             dc->id2 == BUILD_LIBICQ2K_ID2 &&
             dc->id3 == BUILD_LIBICQ2K_ID3)
    {
        new = "libicq2000";
    }
    else if (dc->id1 == BUILD_KXICQ_ID1 &&
             dc->id2 == BUILD_KXICQ_ID2 &&
             dc->id3 == BUILD_KXICQ_ID3)
    {
        new = "KXicq2";
    }
    else if (dc->id1 == BUILD_KXICQ2_ID1 &&
             dc->id2 == BUILD_KXICQ2_ID2 &&
             dc->id3 == BUILD_KXICQ2_ID3)
    {
        new = "KXicq2 > 0.7.6";
    }
    else if (HAS_CAP (cont->caps, CAP_TRILL_CRYPT | CAP_TRILL_2))
        new = "Trillian";
    else if (HAS_CAP (cont->caps, CAP_LICQ))
        new = "licq";
    else if (HAS_CAP (cont->caps, CAP_SIM))
    {
        if (cont->v1 || cont->v2)
            new = "SIM";
        else
            new = "Kopete";
    }
    else if (HAS_CAP (cont->caps, CAP_MICQ))
    {
        new = "mICQ";
        OptSetVal (&cont->copts, CO_TIMEMICQ, time (NULL));
    }
    else if (HAS_CAP (cont->caps, CAP_SIMNEW))
        new = "SIM";
    else if (HAS_CAP (cont->caps, CAP_LICQNEW))
    {
        new = "licq";
        if (cont->v2 / 100 == cont->v1)
            cont->v2 %= 100; /* bug in licq 1.3.0 */
        if (cont->v4 == 1)
            tail = "/SSL";
    }
    else if (HAS_CAP (cont->caps, CAP_KOPETE))
        new = "Kopete";
    else if (dc->id1 == dc->id2 && dc->id2 == dc->id3 && dc->id1 == -1)
        new = "vICQ/GAIM(?)";
    else if (dc->version == 7 && HAS_CAP (cont->caps, CAP_TYPING))
        new = "ICQ2go";
    else if (dc->version == 9 && HAS_CAP (cont->caps, CAP_TYPING))
        new = "ICQ Lite";
    else if (dc->version == 10 && HAS_CAP (cont->caps, CAP_STR_2002) && HAS_CAP (cont->caps, CAP_UTF8))
        new = "ICQ 2003";
    else if (HAS_CAP (cont->caps, CAP_STR_2002) && HAS_CAP (cont->caps, CAP_UTF8))
        new = "ICQ 2002";
    else if (HAS_CAP (cont->caps, CAP_STR_2001) && HAS_CAP (cont->caps, CAP_IS_2001))
        new = "ICQ 2001";
    else if (HAS_CAP (cont->caps, CAP_MACICQ))
        new = "ICQ for Mac";
    else if (HAS_CAP (cont->caps, CAP_KXICQ))
        new = "KXicq2";
    else if (HAS_CAP (cont->caps, CAP_AIM_SFILE) && HAS_CAP(cont->caps, CAP_AIM_IMIMAGE)
          && HAS_CAP (cont->caps, CAP_AIM_BUDICON) && HAS_CAP(cont->caps, CAP_UTF8))
        new = "GAIM (ICQ) (?)";
    else if (dc->version == 8 && HAS_CAP (cont->caps, CAP_UTF8))
        new = "ICQ 2002 (?)";
    else if (dc->version == 8 && HAS_CAP (cont->caps, CAP_IS_2001))
        new = "ICQ 2001 (?)";
    else if (HAS_CAP (cont->caps, CAP_AIM_CHAT))
        new = "AIM(?)";
    else if (dc->version == 7 && !HAS_CAP (cont->caps, CAP_RTFMSGS))
        new = "ICQ 2000 (?)";
    else if (dc->version == 10 && HAS_CAP (cont->caps, CAP_UTF8))
        new = "ICQ 2003 (?)";
    else if (!dc->version && HAS_CAP (cont->caps, CAP_UTF8))
        new = "ICQ 2002 (?)";
    else if (!dc->version && HAS_CAP (cont->caps, CAP_IS_2001))
        new = "ICQ 2001 (?)";
    else if (HAS_CAP (cont->caps, CAP_AIM_CHAT))
        new = "AIM(?)";
    else if (!dc->version && !HAS_CAP (cont->caps, CAP_RTFMSGS))
        new = "ICQ 2000 (?)";
    
    if (new)
    {
        if (new != buf)
            strcpy (buf, new);
        if (cont->v4)
            snprintf (buf + strlen (buf), sizeof(buf) - strlen (buf),
                      " %d.%d.%d.%d", cont->v1, cont->v2, cont->v3, cont->v4);
        else if (cont->v3)
            snprintf (buf + strlen (buf), sizeof(buf) - strlen (buf),
                      " %d.%d.%d", cont->v1, cont->v2, cont->v3);
        else if (cont->v1 || cont->v2)
            snprintf (buf + strlen (buf), sizeof(buf) - strlen (buf),
                      " %d.%d", cont->v1, cont->v2);
        if (tail)
            snprintf (buf + strlen (buf), sizeof(buf) - strlen (buf),
                      "%s", tail);
    }
    else
        buf[0] = '\0';

    s_repl (&cont->version, strlen (buf) ? buf : NULL);
}
