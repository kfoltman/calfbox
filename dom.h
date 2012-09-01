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

#ifndef CBOX_DOM_H
#define CBOX_DOM_H

struct cbox_objhdr;
struct cbox_document;
struct GList;

struct cbox_class
{
    struct cbox_class *parent;
    const char *name;
    struct cbox_objhdr *(*newfunc)(struct cbox_class *class_ptr, struct cbox_document *owner);
    void (*destroyfunc)(struct cbox_objhdr *objhdr);
};

extern struct cbox_class *cbox_class_find_by_name(const char *name);
extern void cbox_class_register(struct cbox_class *class_ptr);

struct cbox_objhdr
{
    struct cbox_class *class_ptr;
    struct cbox_document *owner;
    void *link_in_document;
};

inline int cbox_class_is_a(struct cbox_class *c1, struct cbox_class *c2)
{
    while(c1 != c2 && c1->parent)
        c1 = c1->parent;
    return c1 == c2;
}

extern struct cbox_objhdr *cbox_object_new_by_class(struct cbox_document *doc, struct cbox_class *class_ptr);
extern struct cbox_objhdr *cbox_object_new_by_class_name(struct cbox_document *doc, const char *name);
extern void cbox_object_destroy(struct cbox_objhdr *hdr_ptr);

extern struct cbox_document *cbox_document_new();
extern void cbox_document_destroy(struct cbox_document *);

extern void cbox_dom_init();
extern void cbox_dom_close();

#endif
