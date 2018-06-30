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

#include <glib.h>
#include <stdint.h>
#include <uuid/uuid.h>

struct cbox_command_target;
struct cbox_osc_command;
struct cbox_objhdr;
struct cbox_document;
struct GList;

struct cbox_uuid
{
    uuid_t uuid;
};

extern guint cbox_uuid_hash(gconstpointer v);
extern void cbox_uuid_copy(struct cbox_uuid *vto, const struct cbox_uuid *vfrom);
extern gboolean cbox_uuid_equal(gconstpointer v1, gconstpointer v2);
extern gboolean cbox_uuid_report(struct cbox_uuid *uuid, struct cbox_command_target *fb, GError **error);
extern gboolean cbox_uuid_report_as(struct cbox_uuid *uuid, const char *cmd, struct cbox_command_target *fb, GError **error);
extern gboolean cbox_uuid_fromstring(struct cbox_uuid *uuid, const char *str, GError **error);
extern void cbox_uuid_tostring(struct cbox_uuid *uuid, char str[40]);
extern void cbox_uuid_generate(struct cbox_uuid *uuid);

struct cbox_class
{
    struct cbox_class *parent;
    const char *name;
    int hdr_offset;
    void (*destroyfunc)(struct cbox_objhdr *objhdr);
    struct cbox_command_target *(*getcmdtargetfunc)(struct cbox_objhdr *objhdr);
};

extern struct cbox_class *cbox_class_find_by_name(const char *name);
extern void cbox_class_register(struct cbox_class *class_ptr);

struct cbox_objhdr
{
    struct cbox_class *class_ptr;
    struct cbox_document *owner;
    void *link_in_document;
    struct cbox_uuid instance_uuid;
    uint64_t stamp;
};

static inline int cbox_class_is_a(const struct cbox_class *c1, const struct cbox_class *c2)
{
    while(c1 != c2 && c1->parent)
        c1 = c1->parent;
    return c1 == c2;
}

extern void cbox_object_register_instance(struct cbox_document *doc, struct cbox_objhdr *obj);
extern struct cbox_command_target *cbox_object_get_cmd_target(struct cbox_objhdr *hdr_ptr);
extern gboolean cbox_object_try_default_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, const char *subcmd, gboolean *result, GError **error);
extern gboolean cbox_object_default_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error);
extern gboolean cbox_object_default_status(struct cbox_objhdr *objhdr, struct cbox_command_target *fb, GError **error);

extern void cbox_object_destroy(struct cbox_objhdr *hdr_ptr);

extern struct cbox_document *cbox_document_new(void);
extern void cbox_document_dump(struct cbox_document *);
extern struct cbox_command_target *cbox_document_get_cmd_target(struct cbox_document *);
extern struct cbox_objhdr *cbox_document_get_service(struct cbox_document *doc, const char *name);
extern void cbox_document_set_service(struct cbox_document *doc, const char *name, struct cbox_objhdr *hdr_ptr);
extern struct cbox_objhdr *cbox_document_get_object_by_uuid(struct cbox_document *doc, const struct cbox_uuid *uuid);
extern struct cbox_objhdr *cbox_document_get_object_by_text_uuid(struct cbox_document *doc, const char *uuid, const struct cbox_class *class_ptr, GError **error);
extern uint64_t cbox_document_get_next_stamp(struct cbox_document *doc);
extern void cbox_document_destroy(struct cbox_document *);

extern void cbox_dom_init(void);
extern void cbox_dom_close(void);

// must be the first field in the object-compatible struct
#define CBOX_OBJECT_HEADER() \
    struct cbox_objhdr _obj_hdr;

#define CBOX_CLASS(class) CBOX_CLASS_##class

#define CBOX_EXTERN_CLASS(class) \
    extern struct cbox_class CBOX_CLASS(class);

#define CBOX_GET_DOCUMENT(obj) \
    ((obj)->_obj_hdr.owner)

#define CBOX_STAMP(obj) \
    ((obj)->_obj_hdr.stamp = cbox_document_get_next_stamp(CBOX_GET_DOCUMENT(obj)))

#define CBOX_DELETE(obj) \
    ((obj) && (cbox_object_destroy(&(obj)->_obj_hdr), 1))

#define CBOX_IS_A(obj, class) \
    ((obj) && cbox_class_is_a((obj)->_obj_hdr.class_ptr, &CBOX_CLASS(class)))

#define CBOX_OBJECT_HEADER_INIT(self, class, document) \
    do { \
        (self)->_obj_hdr.class_ptr = &CBOX_CLASS_##class; \
        (self)->_obj_hdr.owner = (document); \
        (self)->_obj_hdr.link_in_document = NULL; \
        (self)->_obj_hdr.stamp = cbox_document_get_next_stamp(document); \
        uuid_generate((self)->_obj_hdr.instance_uuid.uuid); \
    } while(0)
    
#define CBOX_OBJECT_REGISTER(self) \
    (cbox_object_register_instance((self)->_obj_hdr.owner, &(self)->_obj_hdr))

#define CBOX_OBJECT_DEFAULT_STATUS(self, fb, error) \
    (cbox_object_default_status(&(self)->_obj_hdr, (fb), (error)))

#define CBOX_CLASS_DEFINITION_ROOT(class) \
    static void class##_destroyfunc(struct cbox_objhdr *hdr_ptr); \
    static struct cbox_command_target *class##_getcmdtarget(struct cbox_objhdr *hdr) { \
        return &(((struct class *)hdr)->cmd_target);\
    }; \
    struct cbox_class CBOX_CLASS_##class = { \
        .parent = NULL, \
        .name = #class, \
        .hdr_offset = offsetof(struct class, _obj_hdr), \
        .destroyfunc = class##_destroyfunc, \
        .getcmdtargetfunc = class##_getcmdtarget \
    }; \
    
#define CBOX_RETURN_OBJECT(result) \
    return &(result)->_obj_hdr
    
// Convert header to object, regardless of the relative position of the header.
#define CBOX_H2O(hdr) \
    (void *)(((char *)(hdr)) - (hdr)->class_ptr->hdr_offset)

#define CBOX_O2H(obj) \
    (&(*(obj))._obj_hdr)

#endif
