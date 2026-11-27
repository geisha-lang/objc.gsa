/*
 * Geisha Objective-C Runtime & Apple Blocks ABI Bridge (`objc.gsa`)
 *
 * Self-contained C11 bridge for Apple platforms (macOS / iOS / iOS Simulator).
 * Forward-declares the public Geisha GC FFI symbols (`gc_foreign_ptr_new`,
 * `gc_handle_free`) so this package compiles standalone outside `geisha.gsa`.
 * On non-Apple platforms, compiles to harmless stubs for cross-platform checks.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef void (*gc_cleanup_fn)(void *arg);
extern void *gc_foreign_ptr_new(void *raw_ptr, gc_cleanup_fn fn, size_t external_bytes);
extern void gc_handle_free(int64_t handle);

#if defined(__APPLE__)

#include <objc/message.h>
#include <objc/objc.h>
#include <objc/runtime.h>
#include <pthread.h>

extern void *objc_retain(void *obj);
extern void objc_release(void *obj);
extern void *objc_autoreleasePoolPush(void);
extern void objc_autoreleasePoolPop(void *pool);

/* Official Apple Blocks runtime symbols (libSystem / libclosure) */
extern void *_NSConcreteStackBlock[32];
extern void *_Block_copy(const void *aBlock);
extern void _Block_release(const void *aBlock);

void *gs_objc_autorelease_push(void)
{
    return objc_autoreleasePoolPush();
}

void gs_objc_autorelease_pop(void *pool)
{
    objc_autoreleasePoolPop(pool);
}

void *gs_objc_retain_foreign(void *obj, int64_t external_bytes)
{
    if (obj == NULL) return gc_foreign_ptr_new(NULL, NULL, 0);
    objc_retain(obj);
    return gc_foreign_ptr_new(obj, (gc_cleanup_fn)objc_release,
                              external_bytes > 0 ? (size_t)external_bytes : 0);
}

void *gs_objc_adopt_foreign(void *obj, int64_t external_bytes)
{
    if (obj == NULL) return gc_foreign_ptr_new(NULL, NULL, 0);
    return gc_foreign_ptr_new(obj, (gc_cleanup_fn)objc_release,
                              external_bytes > 0 ? (size_t)external_bytes : 0);
}

void *gs_objc_nsstring_from_utf8(const char *utf8)
{
    if (utf8 == NULL) utf8 = "";
    Class nsStrCls = objc_getClass("NSString");
    if (nsStrCls == Nil) return NULL;
    SEL sel = sel_registerName("stringWithUTF8String:");
    typedef id (*msg_fn)(id, SEL, const char *);
    id s = ((msg_fn)objc_msgSend)((id)nsStrCls, sel, utf8);
    if (s == nil) return NULL;
    objc_retain(s);
    return (void *)s;
}

const char *gs_objc_nsstring_to_utf8(void *nsstr)
{
    if (nsstr == NULL) return "";
    SEL sel = sel_registerName("UTF8String");
    typedef const char *(*msg_fn)(id, SEL);
    const char *cstr = ((msg_fn)objc_msgSend)((id)nsstr, sel);
    return cstr != NULL ? cstr : "";
}

int32_t gs_objc_add_protocol(void *cls, const char *proto_name)
{
    if (cls == NULL || proto_name == NULL) return 0;
    Protocol *p = objc_getProtocol(proto_name);
    if (p == NULL) return 0;
    return class_addProtocol((Class)cls, p) ? 1 : 0;
}

/* ---- Auto-releasing Associated GC Handle Box ---- */

static pthread_once_t g_assoc_box_once = PTHREAD_ONCE_INIT;
static Class g_assoc_box_cls = Nil;
static Ivar g_assoc_box_ivar = NULL;

static void gs_assoc_box_dealloc(id self, SEL _cmd)
{
    if (g_assoc_box_ivar != NULL) {
        ptrdiff_t off = ivar_getOffset(g_assoc_box_ivar);
        int64_t *slot = (int64_t *)((char *)self + off);
        int64_t h = *slot;
        *slot = 0;
        if (h != 0) {
            gc_handle_free(h);
        }
    }
    struct objc_super sup;
    sup.receiver = self;
    sup.super_class = objc_getClass("NSObject");
    typedef void (*super_fn)(struct objc_super *, SEL);
    ((super_fn)objc_msgSendSuper)(&sup, _cmd);
}

static void init_assoc_box_class(void)
{
    Class nsObj = objc_getClass("NSObject");
    if (nsObj == Nil) return;
    Class cls = objc_allocateClassPair(nsObj, "GeishaAssocHandleBox", 0);
    if (cls == Nil) {
        g_assoc_box_cls = objc_getClass("GeishaAssocHandleBox");
        if (g_assoc_box_cls != Nil) {
            g_assoc_box_ivar = class_getInstanceVariable(g_assoc_box_cls, "_gs_handle");
        }
        return;
    }
    class_addIvar(cls, "_gs_handle", sizeof(int64_t), 3, "q");
    class_addMethod(cls, sel_registerName("dealloc"), (IMP)gs_assoc_box_dealloc, "v@:");
    objc_registerClassPair(cls);
    g_assoc_box_cls = cls;
    g_assoc_box_ivar = class_getInstanceVariable(cls, "_gs_handle");
}

void gs_objc_set_associated_handle(void *obj, const void *key, int64_t handle)
{
    if (obj == NULL || key == NULL) return;
    if (handle == 0) {
        objc_setAssociatedObject((id)obj, key, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return;
    }
    pthread_once(&g_assoc_box_once, init_assoc_box_class);
    if (g_assoc_box_cls == Nil || g_assoc_box_ivar == NULL) return;
    typedef id (*alloc_fn)(id, SEL);
    id box = ((alloc_fn)objc_msgSend)((id)g_assoc_box_cls, sel_registerName("alloc"));
    box = ((alloc_fn)objc_msgSend)(box, sel_registerName("init"));
    if (box == nil) return;
    ptrdiff_t off = ivar_getOffset(g_assoc_box_ivar);
    *(int64_t *)((char *)box + off) = handle;
    objc_setAssociatedObject((id)obj, key, box, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_release(box);
}

int64_t gs_objc_get_associated_handle(void *obj, const void *key)
{
    if (obj == NULL || key == NULL) return 0;
    pthread_once(&g_assoc_box_once, init_assoc_box_class);
    if (g_assoc_box_ivar == NULL) return 0;
    id box = objc_getAssociatedObject((id)obj, key);
    if (box == nil) return 0;
    ptrdiff_t off = ivar_getOffset(g_assoc_box_ivar);
    return *(int64_t *)((char *)box + off);
}

/* ---- Objective-C Block ABI Bridge (refcounted + GC-handle rooted) ---- */

#define GS_BLOCK_HAS_COPY_DISPOSE (1 << 25)

typedef struct gs_block_desc {
    unsigned long reserved;
    unsigned long size;
    void (*copy_helper)(void *dst, void *src);
    void (*dispose_helper)(void *src);
} gs_block_desc_t;

typedef struct gs_block_state {
    int64_t gc_handle;
    void *trampoline;
    int32_t refcount;
} gs_block_state_t;

typedef struct gs_block_literal {
    void *isa;
    int flags;
    int reserved;
    void *invoke;
    gs_block_desc_t *descriptor;
    gs_block_state_t *state;
} gs_block_literal_t;

static void gs_block_copy_helper(void *dst, void *src)
{
    gs_block_literal_t *d = (gs_block_literal_t *)dst;
    gs_block_literal_t *s = (gs_block_literal_t *)src;
    d->state = s->state;
    if (d->state != NULL) {
        __atomic_add_fetch(&d->state->refcount, 1, __ATOMIC_RELAXED);
    }
}

static void gs_block_dispose_helper(void *src)
{
    gs_block_literal_t *s = (gs_block_literal_t *)src;
    gs_block_state_t *st = s->state;
    if (st != NULL) {
        if (__atomic_sub_fetch(&st->refcount, 1, __ATOMIC_ACQ_REL) == 0) {
            if (st->gc_handle != 0) {
                gc_handle_free(st->gc_handle);
            }
            free(st);
        }
    }
}

static gs_block_desc_t g_gs_block_desc = {
    0,
    sizeof(gs_block_literal_t),
    gs_block_copy_helper,
    gs_block_dispose_helper
};

static void *gs_block_wrap_heap(void *invoke_fn, void *trampoline, int64_t gc_handle)
{
    gs_block_state_t *st = (gs_block_state_t *)malloc(sizeof(gs_block_state_t));
    if (st == NULL) abort();
    st->gc_handle = gc_handle;
    st->trampoline = trampoline;
    st->refcount = 1;

    gs_block_literal_t stack_blk;
    stack_blk.isa = _NSConcreteStackBlock;
    stack_blk.flags = GS_BLOCK_HAS_COPY_DISPOSE;
    stack_blk.reserved = 0;
    stack_blk.invoke = invoke_fn;
    stack_blk.descriptor = &g_gs_block_desc;
    stack_blk.state = st;

    void *heap_blk = _Block_copy(&stack_blk);
    /* Drop initial stack ref; heap_blk now holds the sole reference */
    if (__atomic_sub_fetch(&st->refcount, 1, __ATOMIC_ACQ_REL) == 0) {
        if (st->gc_handle != 0) gc_handle_free(st->gc_handle);
        free(st);
        return gc_foreign_ptr_new(NULL, NULL, 0);
    }
    return gc_foreign_ptr_new(heap_blk, (gc_cleanup_fn)_Block_release,
                              sizeof(gs_block_literal_t) + sizeof(gs_block_state_t));
}

static void gs_blk_inv_0(void *blk)
{
    gs_block_state_t *st = ((gs_block_literal_t *)blk)->state;
    if (st == NULL || st->trampoline == NULL) return;
    typedef void (*cb_t)(int64_t);
    ((cb_t)st->trampoline)(st->gc_handle);
}

static void gs_blk_inv_1p(void *blk, void *a0)
{
    gs_block_state_t *st = ((gs_block_literal_t *)blk)->state;
    if (st == NULL || st->trampoline == NULL) return;
    typedef void (*cb_t)(int64_t, void *);
    ((cb_t)st->trampoline)(st->gc_handle, a0);
}

static void gs_blk_inv_2p(void *blk, void *a0, void *a1)
{
    gs_block_state_t *st = ((gs_block_literal_t *)blk)->state;
    if (st == NULL || st->trampoline == NULL) return;
    typedef void (*cb_t)(int64_t, void *, void *);
    ((cb_t)st->trampoline)(st->gc_handle, a0, a1);
}

static void gs_blk_inv_1i64(void *blk, int64_t a0)
{
    gs_block_state_t *st = ((gs_block_literal_t *)blk)->state;
    if (st == NULL || st->trampoline == NULL) return;
    typedef void (*cb_t)(int64_t, int64_t);
    ((cb_t)st->trampoline)(st->gc_handle, a0);
}

static void gs_blk_inv_1b(void *blk, bool a0)
{
    gs_block_state_t *st = ((gs_block_literal_t *)blk)->state;
    if (st == NULL || st->trampoline == NULL) return;
    typedef void (*cb_t)(int64_t, int32_t);
    ((cb_t)st->trampoline)(st->gc_handle, a0 ? 1 : 0);
}

void *gs_objc_block_new_0(void *trampoline, int64_t gc_handle)
{
    return gs_block_wrap_heap((void *)gs_blk_inv_0, trampoline, gc_handle);
}

void *gs_objc_block_new_1p(void *trampoline, int64_t gc_handle)
{
    return gs_block_wrap_heap((void *)gs_blk_inv_1p, trampoline, gc_handle);
}

void *gs_objc_block_new_2p(void *trampoline, int64_t gc_handle)
{
    return gs_block_wrap_heap((void *)gs_blk_inv_2p, trampoline, gc_handle);
}

void *gs_objc_block_new_1i64(void *trampoline, int64_t gc_handle)
{
    return gs_block_wrap_heap((void *)gs_blk_inv_1i64, trampoline, gc_handle);
}

void *gs_objc_block_new_1b(void *trampoline, int64_t gc_handle)
{
    return gs_block_wrap_heap((void *)gs_blk_inv_1b, trampoline, gc_handle);
}

void *gs_objc_block_new_custom(void *custom_invoke, int64_t gc_handle)
{
    return gs_block_wrap_heap(custom_invoke, NULL, gc_handle);
}

int64_t gs_objc_block_get_handle(void *blk)
{
    if (blk == NULL) return 0;
    gs_block_state_t *st = ((gs_block_literal_t *)blk)->state;
    return st != NULL ? st->gc_handle : 0;
}

#else

void *gs_objc_autorelease_push(void) { return NULL; }
void gs_objc_autorelease_pop(void *pool) { (void)pool; }
void *gs_objc_retain_foreign(void *obj, int64_t external_bytes) { (void)obj; (void)external_bytes; return NULL; }
void *gs_objc_adopt_foreign(void *obj, int64_t external_bytes) { (void)obj; (void)external_bytes; return NULL; }
void *gs_objc_nsstring_from_utf8(const char *utf8) { (void)utf8; return NULL; }
const char *gs_objc_nsstring_to_utf8(void *nsstr) { (void)nsstr; return ""; }
int32_t gs_objc_add_protocol(void *cls, const char *proto_name) { (void)cls; (void)proto_name; return 0; }
void gs_objc_set_associated_handle(void *obj, const void *key, int64_t handle) { (void)obj; (void)key; (void)handle; }
int64_t gs_objc_get_associated_handle(void *obj, const void *key) { (void)obj; (void)key; return 0; }
void *gs_objc_block_new_0(void *trampoline, int64_t gc_handle) { (void)trampoline; (void)gc_handle; return NULL; }
void *gs_objc_block_new_1p(void *trampoline, int64_t gc_handle) { (void)trampoline; (void)gc_handle; return NULL; }
void *gs_objc_block_new_2p(void *trampoline, int64_t gc_handle) { (void)trampoline; (void)gc_handle; return NULL; }
void *gs_objc_block_new_1i64(void *trampoline, int64_t gc_handle) { (void)trampoline; (void)gc_handle; return NULL; }
void *gs_objc_block_new_1b(void *trampoline, int64_t gc_handle) { (void)trampoline; (void)gc_handle; return NULL; }
void *gs_objc_block_new_custom(void *custom_invoke, int64_t gc_handle) { (void)custom_invoke; (void)gc_handle; return NULL; }
int64_t gs_objc_block_get_handle(void *blk) { (void)blk; return 0; }

#endif
