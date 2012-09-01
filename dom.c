/*
Calf Box, an open source musical instrument.
Copyright (C) 2012 Krzysztof Foltman

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "dom.h"

#include <assert.h>
#include <glib.h>
#include <malloc.h>

static GHashTable *class_name_hash = NULL;

struct cbox_class_per_document
{
    GList *instances;
    struct cbox_objhdr *singleton;
};

struct cbox_document
{
    GHashTable *classes_per_document;
};

////////////////////////////////////////////////////////////////////////////////////////

void cbox_dom_init()
{
    class_name_hash = g_hash_table_new(g_str_hash, g_str_equal);
}

void cbox_dom_close()
{
    g_hash_table_destroy(class_name_hash);    
}

////////////////////////////////////////////////////////////////////////////////////////

struct cbox_class *cbox_class_find_by_name(const char *name)
{
    assert(class_name_hash != NULL);
    return g_hash_table_lookup(class_name_hash, name);
}

void cbox_class_register(struct cbox_class *class_ptr)
{
    assert(class_name_hash != NULL);
    g_hash_table_insert(class_name_hash, (gpointer)class_ptr->name, class_ptr);
}

static struct cbox_class_per_document *get_cpd_for_class(struct cbox_document *doc, struct cbox_class *class_ptr)
{
    struct cbox_class_per_document *p = g_hash_table_lookup(doc->classes_per_document, class_ptr);
    if (p != NULL)
        return p;
    p = malloc(sizeof(struct cbox_class_per_document));
    p->instances = NULL;
    p->singleton = NULL;
    g_hash_table_insert(doc->classes_per_document, class_ptr, p);
    return p;
}

////////////////////////////////////////////////////////////////////////////////////////

struct cbox_objhdr *cbox_object_new_by_class(struct cbox_document *doc, struct cbox_class *class_ptr)
{
    assert(class_ptr != NULL);

    struct cbox_objhdr *obj = class_ptr->newfunc(class_ptr, doc);
    if (!obj)
        return NULL;
    
    struct cbox_class_per_document *cpd = get_cpd_for_class(doc, class_ptr);
    cpd->instances = g_list_prepend(cpd->instances, obj);
    obj->owner = doc;
    obj->link_in_document = cpd->instances;
    
    return obj;
}

struct cbox_objhdr *cbox_object_new_by_class_name(struct cbox_document *doc, const char *name)
{
    struct cbox_class *class_ptr = cbox_class_find_by_name(name);
    if (class_ptr == NULL)
        return NULL;
    
    return cbox_object_new_by_class(doc, (struct cbox_class *)class_ptr);
}

struct cbox_command_target *cbox_object_get_cmd_target(struct cbox_objhdr *hdr_ptr)
{
    if (!hdr_ptr->class_ptr->getcmdtargetfunc)
        return NULL;
    return hdr_ptr->class_ptr->getcmdtargetfunc(hdr_ptr);
}

void cbox_object_set_as_singleton(struct cbox_objhdr *hdr_ptr)
{
    struct cbox_class_per_document *cpd = get_cpd_for_class(hdr_ptr->owner, hdr_ptr->class_ptr);
    cpd->singleton = hdr_ptr;
}

void cbox_object_destroy(struct cbox_objhdr *hdr_ptr)
{
    struct cbox_class_per_document *cpd = get_cpd_for_class(hdr_ptr->owner, hdr_ptr->class_ptr);
    cpd->instances = g_list_remove_link(cpd->instances, hdr_ptr->link_in_document);
    hdr_ptr->link_in_document = NULL;
    
    hdr_ptr->class_ptr->destroyfunc(hdr_ptr);
}

////////////////////////////////////////////////////////////////////////////////////////


struct cbox_document *cbox_document_new()
{
    struct cbox_document *res = malloc(sizeof(struct cbox_document));
    res->classes_per_document = g_hash_table_new(NULL, NULL);
    return res;
}

struct cbox_objhdr *cbox_document_get_singleton(struct cbox_document *document, struct cbox_class *class_ptr)
{
    struct cbox_class_per_document *cpd = get_cpd_for_class(document, class_ptr);
    return cpd->singleton;
}

static void iter_func(gpointer key, gpointer value, gpointer document)
{
    struct cbox_class *class_ptr = key;
    struct cbox_class_per_document *cpd = value;
    int first = 1;
    printf("Class %s: ", class_ptr->name);
    if (cpd->singleton)
    {
        printf("Singleton %p", cpd->singleton);
        first = 0;
    }
    GList *l = cpd->instances;
    while(l) {
        if (!first)
            printf(", ");
        printf("%p", l->data);
        l = l->next;
        first = 0;
    }
    printf("\n");
}

void cbox_document_dump(struct cbox_document *document)
{
     g_hash_table_foreach(document->classes_per_document, iter_func, document);
}

void cbox_document_destroy(struct cbox_document *document)
{
    free(document);
}


