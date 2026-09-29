#pragma once
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t lv_color_t;
typedef struct lv_font_t { const struct lv_font_t *fallback; } lv_font_t;
typedef struct { char text[256]; int x, y, w, h; unsigned flags; } lv_obj_t;
extern const lv_font_t lv_font_montserrat_14, lv_font_montserrat_20;
#define LV_FONT_DECLARE(name) extern const lv_font_t name
enum { LV_OBJ_FLAG_SCROLLABLE=1, LV_OBJ_FLAG_HIDDEN=2, LV_OPA_COVER=255,
       LV_TEXT_ALIGN_LEFT=0, LV_TEXT_ALIGN_RIGHT=1, LV_TEXT_ALIGN_CENTER=2,
       LV_LABEL_LONG_WRAP=0, LV_LABEL_LONG_DOT=1, LV_LABEL_LONG_SCROLL_CIRCULAR=2 };
static lv_obj_t test_objects[64];
static unsigned test_object_count;
static inline lv_obj_t *lv_obj_create(lv_obj_t *p) {
    (void)p; assert(test_object_count < 64);
    return &test_objects[test_object_count++];
}
static inline lv_obj_t *lv_label_create(lv_obj_t *p) { return lv_obj_create(p); }
static inline void lv_obj_delete(lv_obj_t *p) { (void)p; test_object_count=0; memset(test_objects,0,sizeof(test_objects)); }
static inline lv_color_t lv_color_hex(uint32_t c) { return c; }
static inline void lv_obj_set_pos(lv_obj_t *p,int x,int y) { p->x=x; p->y=y; }
static inline void lv_obj_set_size(lv_obj_t *p,int w,int h) { p->w=w; p->h=h; }
static inline void lv_obj_add_flag(lv_obj_t *p,unsigned f) { p->flags |= f; }
static inline void lv_obj_remove_flag(lv_obj_t *p,unsigned f) { p->flags &= ~f; }
static inline void lv_label_set_text(lv_obj_t *p,const char *s) { assert(strlen(s)<sizeof(p->text)); snprintf(p->text,sizeof(p->text),"%s",s); }
static inline const char *lv_label_get_text(lv_obj_t *p) { return p->text; }
static inline void lv_label_set_long_mode(lv_obj_t *p,int m) { (void)p; (void)m; }
static inline void lv_screen_load(lv_obj_t *p) { (void)p; }
#define STYLE_FN(name,type) static inline void name(lv_obj_t *p,type value,int selector) { (void)p; (void)value; (void)selector; }
STYLE_FN(lv_obj_set_style_bg_color,lv_color_t)
STYLE_FN(lv_obj_set_style_bg_opa,int)
STYLE_FN(lv_obj_set_style_radius,int)
STYLE_FN(lv_obj_set_style_border_width,int)
STYLE_FN(lv_obj_set_style_pad_all,int)
STYLE_FN(lv_obj_set_style_text_font,const lv_font_t *)
STYLE_FN(lv_obj_set_style_text_color,lv_color_t)
STYLE_FN(lv_obj_set_style_text_align,int)
#undef STYLE_FN
