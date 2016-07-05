#include "e_mod_effect_rotation.h"

#include <tbm_bufmgr.h>
#include <tbm_surface.h>
#include <tbm_surface_internal.h>
#include <tdm_helper.h>
#include <wayland-tbm-server.h>

#if 1
#define e_util_size_debug_set(x, y)
#endif

typedef struct _Rotation_Effect_Object
{
   E_Client *ec;
   Evas_Object *img;

   struct wl_shm_pool *data_pool;
   void *data;
} Rotation_Effect_Object;

typedef struct _Rotation_Effect_Begin_Context
{
   Evas_Object *layout;
   Eina_List *objects;

   double src;
   double dest;
} Rotation_Effect_Begin_Context;

typedef struct _Rotation_Effect_End_Context
{
   Evas_Object *layout;
   Eina_List *objects;

   double src;
   double dest;
} Rotation_Effect_End_Context;

typedef struct _Rotation_Effect
{
   E_Zone *zone;

   Evas_Object *bg;
   Eina_List *targets;
   Eina_List *waiting_list;

   Rotation_Effect_Begin_Context *ctx_begin;
   Rotation_Effect_End_Context *ctx_end;

   Ecore_Animator *animator;

   Eina_Bool running;
   Eina_Bool wait_for_buffer;
} Rotation_Effect;

typedef struct _Rotation_Zone
{
   E_Zone *zone;
   Eina_List *event_hdlrs;

   Rotation_Effect *effect;

   int curr_angle;
   int prev_angle;
} Rotation_Zone;

static Rotation_Zone *_rotation_zone = NULL;

static Eina_Bool
_rotation_effect_available(const E_Client *ec, int ang)
{
   Eina_Bool ret = EINA_FALSE;
   unsigned int i;

   if (ang < 0) return EINA_FALSE;
   if (!ec->e.state.rot.support)
     goto no_hint;

   if (ec->e.state.rot.preferred_rot == -1)
     {
        if (ec->e.state.rot.available_rots &&
            ec->e.state.rot.count)
          {
             for (i = 0; i < ec->e.state.rot.count; i++)
               {
                  if (ec->e.state.rot.available_rots[i] == ang)
                    {
                       ret = EINA_TRUE;
                       break;
                    }
               }
          }
        else
          goto no_hint;
     }
   else if (ec->e.state.rot.preferred_rot == ang)
     ret = EINA_TRUE;

   return ret;
no_hint:
   return (ang == 0);
}

static Eina_List *
_rotation_effect_targets_get(Rotation_Effect *effect)
{
   Evas_Object *o;
   Eina_Tiler *t;
   Eina_Rectangle r;
   int x , y, w, h;
   const int edge = 1;
   Eina_List *l = NULL;
   E_Client *ec;

   if (!effect) return NULL;

   if (effect->zone->display_state == E_ZONE_DISPLAY_STATE_OFF)
     return NULL;

   x = y = w = h = 0;

   t = eina_tiler_new(effect->zone->w + edge, effect->zone->h + edge);
   eina_tiler_tile_size_set(t, 1, 1);

   EINA_RECTANGLE_SET(&r, effect->zone->x, effect->zone->y, effect->zone->w, effect->zone->h);
   eina_tiler_rect_add(t, &r);

   o = evas_object_top_get(e_comp->evas);
   for (; o; o = evas_object_below_get(o))
     {
        if (!evas_object_visible_get(o)) continue;
        if (o == effect->zone->over) continue;
        if (o == effect->bg) continue;
        if (evas_object_layer_get(o) > E_LAYER_EFFECT) continue;
        if (!e_util_strcmp(evas_object_name_get(o), "layer_obj")) continue;

        evas_object_geometry_get(o, &x, &y, &w, &h);
        ec = evas_object_data_get(o, "E_Client");
        if (ec)
          {
             if (e_object_is_del(E_OBJECT(ec))) continue;
             if (ec->visibility.obscured != E_VISIBILITY_UNOBSCURED) continue;

             if ((!ec->animatable) ||
                 (!_rotation_effect_available(ec, effect->zone->rot.curr)) ||
                 (ec->e.state.rot.ang.curr == effect->zone->rot.curr))
               {
                  if (l) eina_list_free(l);
                  eina_tiler_free(t);
                  return NULL;
               }
          }

        l = eina_list_append(l, o);

        if ((ec) && (ec->argb) && (ec->visibility.opaque <= 0))
          continue;

        EINA_RECTANGLE_SET(&r, x, y, w + edge, h + edge);
        eina_tiler_rect_del(t, &r);

        if (eina_tiler_empty(t)) break;
     }
   eina_tiler_free(t);

   return l;
}

static void
_rotation_effect_object_free(Rotation_Effect_Object *eobj)
{
   if (!eobj) return;

   if (eobj->data)
     {
        evas_object_image_data_set(eobj->img, NULL);
        free(eobj->data);
     }
   if (eobj->img) evas_object_del(eobj->img);
   if (eobj->data_pool) wl_shm_pool_unref(eobj->data_pool);

   E_FREE(eobj);
}

static Rotation_Effect_Object *
_rotation_effect_object_create(Evas_Object *o)
{
   Rotation_Effect_Object *eobj;
   E_Comp_Wl_Buffer *buffer = NULL;
   Evas_Object *img = NULL;
   int x, y, w, h;
   int i;
   E_Client *ec;
   void *data = NULL, *pix = NULL;

   if (!evas_object_visible_get(o)) return NULL;

   ec = evas_object_data_get(o, "E_Client");
   if (ec)
     {
        if (e_object_is_del(E_OBJECT(ec))) return NULL;

        eobj = E_NEW(Rotation_Effect_Object, 1);
        if (!eobj) goto fail;

        eobj->ec = ec;

        buffer = e_pixmap_resource_get(ec->pixmap);
        if (!buffer) goto fail;

        img = evas_object_image_filled_add(e_comp->evas);
        e_util_size_debug_set(img, 1);
        evas_object_image_colorspace_set(img, EVAS_COLORSPACE_ARGB8888);
        evas_object_image_smooth_scale_set(img, e_comp_config_get()->smooth_windows);

        if (buffer->type == E_COMP_WL_BUFFER_TYPE_SHM)
          {
             if (!buffer->shm_buffer) goto fail;

             w = buffer->w;
             h = buffer->h;

             pix = wl_shm_buffer_get_data(buffer->shm_buffer);
             if (!pix) goto fail;

             if (eobj->data_pool)
               wl_shm_pool_unref(eobj->data_pool);
             eobj->data_pool = wl_shm_buffer_ref_pool(buffer->shm_buffer);
          }
        else if  (buffer->type == E_COMP_WL_BUFFER_TYPE_NATIVE)
          {
             tbm_surface_info_s surface_info;
             tbm_surface_h tbm_surface = wayland_tbm_server_get_surface(NULL, buffer->resource);
             memset(&surface_info, 0, sizeof(tbm_surface_info_s));
             tbm_surface_map(tbm_surface, TBM_SURF_OPTION_READ, &surface_info);

             data = surface_info.planes[0].ptr;
             if (!data) goto fail;

             w = surface_info.width;
             h = surface_info.height;

             pix = eobj->data = malloc(w * h * 4);
             for (i = 0; i < h; i++)
               {
                  memcpy(pix, data, surface_info.width * 4);
                  pix += surface_info.width * 4;
                  data += surface_info.planes[0].stride;
               }
             pix = eobj->data;

             tbm_surface_unmap(tbm_surface);
          }
        else if (buffer->type == E_COMP_WL_BUFFER_TYPE_TBM)
          {
             tbm_surface_info_s surface_info;
             tbm_surface_h tbm_surface = buffer->tbm_surface;

             memset(&surface_info, 0, sizeof(tbm_surface_info_s));
             tbm_surface_map(tbm_surface, TBM_SURF_OPTION_READ, &surface_info);

             data = surface_info.planes[0].ptr;
             if (!data) goto fail;

             w = surface_info.width;
             h = surface_info.height;

             pix = eobj->data = malloc(w * h * 4);
             for (i = 0; i < h; i++)
               {
                  memcpy(pix, data, surface_info.width * 4);
                  pix += surface_info.width * 4;
                  data += surface_info.planes[0].stride;
               }
             pix = eobj->data;

             tbm_surface_unmap(tbm_surface);
          }
        else
          goto fail;

        if (pix)
          {
             evas_object_image_alpha_set(img, 1);
             evas_object_image_size_set(img, w, h);
             evas_object_image_data_set(img, pix);
             evas_object_image_data_update_add(img, 0, 0, w, h);

             evas_object_name_set(img, "rotation-effect-image");
             evas_object_move(img, ec->x, ec->y);
             evas_object_resize(img, ec->w, ec->h);
          }
        else
          goto fail;

        EFFINF("Rotation EFFECT Object Created E_Client:%p",
               NULL, NULL, ec);

        eobj->img = img;
        return eobj;
     }
   else
     {
        eobj = E_NEW(Rotation_Effect_Object, 1);
        if (!eobj) return NULL;

        eobj->ec = NULL;

        evas_object_geometry_get(o, &x, &y, &w, &h);

        img = evas_object_image_filled_add(e_comp->evas);
        e_util_size_debug_set(img, 1);

        evas_object_image_colorspace_set(img, EVAS_COLORSPACE_ARGB8888);
        evas_object_image_smooth_scale_set(img, e_comp_config_get()->smooth_windows);
        evas_object_image_alpha_set(img, 1);
        evas_object_image_size_set(img, w, h);
        evas_object_image_source_set(img, o);

        evas_object_name_set(img, "rotation-effect-image");
        evas_object_move(img, x, y);
        evas_object_resize(img, w, h);

        eobj->img = img;

        EFFINF("Rotation EFFECT Object Created Object:%p",
               NULL, NULL, o);

        return eobj;
     }

fail:
   if (eobj)
     {
        evas_object_image_data_set(img, NULL);
        evas_object_del(img);

        if (eobj->data)
          free(eobj->data);

        if (eobj->data_pool)
          wl_shm_pool_unref(eobj->data_pool);

        E_FREE(eobj);
     }

   return NULL;
}

static Rotation_Effect_Begin_Context *
_rotation_effect_begin_create(Rotation_Effect *effect, Eina_List *targets)
{
   Rotation_Effect_Begin_Context *ctx_begin = NULL;
   Rotation_Effect_Object *eobj = NULL;
   Evas_Object *target;
   Eina_List *l;
   int x, y, w, h;

   ctx_begin = E_NEW(Rotation_Effect_Begin_Context, 1);
   if (!ctx_begin) return NULL;

   ctx_begin->layout = e_layout_add(e_comp->evas);
   e_util_size_debug_set(ctx_begin->layout, 1);
   evas_object_name_set(ctx_begin->layout, "rotation-begin-effect-layout");
   e_layout_virtual_size_set(ctx_begin->layout, effect->zone->w, effect->zone->h);
   evas_object_move(ctx_begin->layout, effect->zone->x, effect->zone->y);
   evas_object_resize(ctx_begin->layout, effect->zone->w, effect->zone->h);
   evas_object_layer_set(ctx_begin->layout, E_LAYER_EFFECT);

   EINA_LIST_REVERSE_FOREACH(targets, l, target)
     {
        eobj = _rotation_effect_object_create(target);
        if (!eobj) continue;

        ctx_begin->objects = eina_list_append(ctx_begin->objects, eobj);
        if (eobj->ec)
          effect->waiting_list = eina_list_append(effect->waiting_list, eobj->ec);

        evas_object_geometry_get(target, &x, &y, &w, &h);

        e_layout_pack(ctx_begin->layout, eobj->img);
        e_layout_child_move(eobj->img, x, y);
        e_layout_child_resize(eobj->img, w, h);
        e_layout_child_raise(eobj->img);
        evas_object_show(eobj->img);
     }

   if (!ctx_begin->objects)
     {
        evas_object_del(ctx_begin->layout);
        E_FREE(ctx_begin);
        return NULL;
     }

   EFFINF("Rotation Begin Created", NULL, NULL);

   int diff = effect->zone->rot.prev - effect->zone->rot.curr;
   if (diff == 270) diff = - 90;
   else if (diff == -270) diff = 90;
   ctx_begin->src = 0.0;
   ctx_begin->dest = diff;

   return ctx_begin;
}


static Rotation_Effect_End_Context *
_rotation_effect_end_create(Rotation_Effect *effect, Eina_List *targets)
{
   Rotation_Effect_End_Context *ctx_end = NULL;
   Rotation_Effect_Object *eobj = NULL;
   Eina_List *l;
   Evas_Object *target;
   int x, y, w, h;

   ctx_end = E_NEW(Rotation_Effect_End_Context, 1);
   if (!ctx_end) return NULL;

   ctx_end->layout = e_layout_add(e_comp->evas);
   e_util_size_debug_set(ctx_end->layout, 1);
   evas_object_name_set(ctx_end->layout, "rotation-end-effect-layout");
   e_layout_virtual_size_set(ctx_end->layout, effect->zone->w, effect->zone->h);
   evas_object_move(ctx_end->layout, effect->zone->x, effect->zone->y);
   evas_object_resize(ctx_end->layout, effect->zone->w, effect->zone->h);
   evas_object_layer_set(ctx_end->layout, E_LAYER_EFFECT);

   EINA_LIST_REVERSE_FOREACH(targets, l, target)
     {
        eobj = _rotation_effect_object_create(target);
        if (!eobj) continue;

        evas_object_geometry_get(target, &x, &y, &w, &h);

        ctx_end->objects = eina_list_append(ctx_end->objects, eobj);

        e_layout_pack(ctx_end->layout, eobj->img);
        e_layout_child_move(eobj->img, x, y);
        e_layout_child_resize(eobj->img, w, h);
        e_layout_child_raise(eobj->img);
        evas_object_show(eobj->img);
     }

   if (!ctx_end->objects)
     {
        evas_object_del(ctx_end->layout);
        E_FREE(ctx_end);
        return NULL;
     }

   EFFINF("Rotation End Created", NULL, NULL);

   int diff = _rotation_zone->curr_angle - _rotation_zone->prev_angle;
   if (diff == 270) diff = - 90;
   else if (diff == -270) diff = 90;
   ctx_end->src = diff;
   ctx_end->dest = 0.0;

   return ctx_end;
}

static void
_rotation_effect_animator_begin_context_free(Rotation_Effect_Begin_Context *ctx_begin)
{
   Rotation_Effect_Object *eobj;

   if (!ctx_begin) return;

   EFFINF("Rotation Begin Free", NULL, NULL);

   if (ctx_begin->layout)
     evas_object_hide(ctx_begin->layout);

   EINA_LIST_FREE(ctx_begin->objects, eobj)
     {
        e_layout_unpack(eobj->img);
        _rotation_effect_object_free(eobj);
     }

   if (ctx_begin->layout)
     evas_object_del(ctx_begin->layout);

   E_FREE(ctx_begin);
}

static void
_rotation_effect_animator_end_context_free(Rotation_Effect_End_Context *ctx_end)
{
   Rotation_Effect_Object *eobj;

   if (!ctx_end) return;

   EFFINF("Rotation End Free", NULL, NULL);

   if (ctx_end->layout)
     evas_object_hide(ctx_end->layout);

   EINA_LIST_FREE(ctx_end->objects, eobj)
     {
        e_layout_unpack(eobj->img);
        _rotation_effect_object_free(eobj);
     }

   if (ctx_end->layout)
     evas_object_del(ctx_end->layout);

   E_FREE(ctx_end);
}

static void
_rotation_effect_clear(Rotation_Effect *effect)
{
   if (!effect) return;

   EFFINF("Rotation Effect Clear", NULL, NULL);

   effect->targets = eina_list_free(effect->targets);
   effect->waiting_list = eina_list_free(effect->waiting_list);

   if (effect->animator)
     ecore_animator_del(effect->animator);

   evas_object_hide(effect->bg);

   if (effect->ctx_begin)
     {
        _rotation_effect_animator_begin_context_free(effect->ctx_begin);
        if (!effect->ctx_end)
          e_comp_override_del();
     }

   if (effect->ctx_end)
     _rotation_effect_animator_end_context_free(effect->ctx_end);

   effect->running = EINA_FALSE;
   effect->wait_for_buffer = EINA_FALSE;
   effect->animator = NULL;
   effect->ctx_begin = NULL;
   effect->ctx_end = NULL;
}

static Eina_Bool
_rotation_effect_animator_cb_update(void *data, double pos)
{
   Rotation_Effect *effect;
   Rotation_Effect_Begin_Context *ctx_begin;
   Rotation_Effect_End_Context *ctx_end;

   double curr, col, progress;
   Evas_Coord x, y, w, h;

   effect = (Rotation_Effect *)data;
   ctx_begin = effect->ctx_begin;
   ctx_end = effect->ctx_end;

   if (pos == 1.0)
     {
        ecore_animator_del(effect->animator);
        effect->animator = NULL;

        _rotation_effect_animator_begin_context_free(effect->ctx_begin);
        effect->ctx_begin = NULL;

        _rotation_effect_animator_end_context_free(effect->ctx_end);
        effect->ctx_end = NULL;

        effect->wait_for_buffer = EINA_FALSE;
        effect->running = EINA_FALSE;
        evas_object_hide(effect->bg);

        e_comp_override_del();

        return ECORE_CALLBACK_CANCEL;
     }

   progress = ecore_animator_pos_map(pos, ECORE_POS_MAP_DECELERATE, 0, 0);

   if (progress < 0.0) progress = 0.0;

   /* rotation begin */
   curr = (progress * ctx_begin->dest);
   col = 255 - (255 * progress);

   evas_object_geometry_get(ctx_begin->layout, &x, &y, &w, &h);

   Evas_Map *m = evas_map_new(4);
   evas_map_util_points_populate_from_object(m, ctx_begin->layout);
   evas_map_util_rotate(m, curr, x + (w/2), y + (h/2));
   evas_map_alpha_set(m, EINA_TRUE);
   evas_map_util_points_color_set(m, col, col, col, col);
   evas_object_map_set(ctx_begin->layout, m);
   evas_object_map_enable_set(ctx_begin->layout, EINA_TRUE);
   evas_map_free(m);

   /* rotation end */
   curr = ((-1.0f * progress * ctx_end->src) + ctx_end->src);

   evas_object_geometry_get(ctx_end->layout, &x, &y, &w, &h);

   m = evas_map_new(4);
   evas_map_util_points_populate_from_object(m, ctx_end->layout);
   evas_map_util_rotate(m, curr, x + (w/2), y + (h/2));
   evas_object_map_set(ctx_end->layout, m);
   evas_object_map_enable_set(ctx_end->layout, EINA_TRUE);
   evas_map_free(m);

   return ECORE_CALLBACK_RENEW;
}

static void
_rotation_effect_start(Rotation_Effect *effect)
{
   if ((!effect->ctx_begin) || (!effect->ctx_end)) return;
   if (effect->running) return;

   EFFINF("Rotation Effect Start", NULL, NULL);

   effect->running = EINA_TRUE;

   evas_object_raise(effect->ctx_begin->layout);
   evas_object_show(effect->ctx_begin->layout);
   evas_object_show(effect->ctx_end->layout);

   evas_object_move(effect->bg, 0, 0);
   evas_object_resize(effect->bg, effect->zone->w, effect->zone->h);
   evas_object_lower(effect->bg);
   evas_object_show(effect->bg);

   effect->animator = ecore_animator_timeline_add(0.3f,
                                                  _rotation_effect_animator_cb_update,
                                                  effect);
}

static void
_rotation_effect_animator_begin_prepare(Rotation_Effect *effect)
{
   if (!effect) return;

   _rotation_effect_clear(effect);

   effect->targets = _rotation_effect_targets_get(effect);
   if (!effect->targets) return;

   effect->ctx_begin = _rotation_effect_begin_create(effect, effect->targets);
   if (!effect->ctx_begin)
     {
        _rotation_effect_clear(effect);
        return;
     }

   EFFINF("Rotation Begin Prepared", NULL, NULL);

   /* add hwc override */
   e_comp_override_add();
}

static void
_rotation_effect_animator_end_prepare(Rotation_Effect *effect)
{
   if (!effect) return;
   if (!effect->targets) return;
   if (!effect->ctx_begin) return;

   /* clear previous context */
   if (effect->ctx_end)
     _rotation_effect_animator_end_context_free(effect->ctx_end);
   effect->ctx_end = NULL;

   effect->ctx_end = _rotation_effect_end_create(effect, effect->targets);
   if (!effect->ctx_end) return;

   EFFINF("Rotation End Prepared", NULL, NULL);

   effect->targets = eina_list_free(effect->targets);
}

static Eina_Bool
_rotation_effect_cb_zone_rotation_begin(void *data, int type, void *event)
{
   Rotation_Effect *effect;
   E_Event_Zone_Rotation_Change_Begin *ev = event;
   E_Zone *zone;

   effect = (Rotation_Effect *)data;
   if (!effect) return ECORE_CALLBACK_PASS_ON;

   zone = ev->zone;
   if (!zone) return ECORE_CALLBACK_PASS_ON;

   EFFINF("Zone rotation begin zone(prev:%d cur:%d)",
          NULL, NULL,
          zone->rot.prev, zone->rot.curr);

   if (zone->rot.prev == zone->rot.curr) return ECORE_CALLBACK_PASS_ON;

   _rotation_zone->curr_angle = zone->rot.curr;
   _rotation_zone->prev_angle = zone->rot.prev;

   _rotation_effect_animator_begin_prepare(effect);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_rotation_effect_cb_zone_rotation_end(void *data, int type, void *event)
{
   Rotation_Effect *effect;
   E_Event_Zone_Rotation_Change_End *ev = event;
   E_Zone *zone;

   effect = (Rotation_Effect *)data;
   if (!effect) return ECORE_CALLBACK_PASS_ON;

   zone = ev->zone;
   if (!zone) return ECORE_CALLBACK_PASS_ON;

   EFFINF("Zone rotation end angle(prev:%d cur:%d)", NULL, NULL,
          zone->rot.prev, zone->rot.curr);

   if (effect->running) return ECORE_CALLBACK_PASS_ON;
   if (effect->waiting_list)
     {
        effect->wait_for_buffer = EINA_TRUE;
        return ECORE_CALLBACK_PASS_ON;
     }

   if (!effect->ctx_end) _rotation_effect_animator_end_prepare(effect);
   if (effect->ctx_end) _rotation_effect_start(effect);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_rotation_effect_cb_zone_rotation_cancel(void *data, int type, void *event)
{
   Rotation_Effect *effect;

   EFFINF("Zone Rotation Canceld", NULL, NULL);

   effect = (Rotation_Effect *)data;
   if (!effect) return ECORE_CALLBACK_PASS_ON;
   if (effect->running) return ECORE_CALLBACK_PASS_ON;

   _rotation_effect_clear(effect);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_rotation_effect_cb_buffer_change(void *data, int ev_type, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec;
   Rotation_Effect *effect;

   ec = ev->ec;
   if (!ec) return ECORE_CALLBACK_PASS_ON;

   effect = (Rotation_Effect *)data;
   if (!effect) return ECORE_CALLBACK_PASS_ON;
   if (!effect->ctx_begin) return ECORE_CALLBACK_PASS_ON;
   if (!effect->waiting_list) return ECORE_CALLBACK_PASS_ON;

   effect->waiting_list = eina_list_remove(effect->waiting_list, ec);
   if (effect->waiting_list) return ECORE_CALLBACK_PASS_ON;

   if (!effect->wait_for_buffer) return ECORE_CALLBACK_PASS_ON;

   if (!effect->ctx_end) _rotation_effect_animator_end_prepare(effect);
   if (effect->ctx_end) _rotation_effect_start(effect);

   return ECORE_CALLBACK_PASS_ON;
}

static void
_rotation_effect_free(Rotation_Effect *effect)
{
   if (!effect) return;

   _rotation_effect_clear(effect);
   evas_object_del(effect->bg);
   E_FREE(effect);
}

static Rotation_Effect *
_rotation_effect_create(E_Zone *zone)
{
   Rotation_Effect *rotation_effect;

   rotation_effect = E_NEW(Rotation_Effect, 1);
   if (!rotation_effect) return NULL;

   rotation_effect->zone = zone;

   rotation_effect->bg = evas_object_rectangle_add(e_comp->evas);
   e_util_size_debug_set(rotation_effect->bg, 1);
   evas_object_color_set(rotation_effect->bg, 0, 0, 0, 255);
   evas_object_layer_set(rotation_effect->bg, E_LAYER_EFFECT);
   evas_object_name_set(rotation_effect->bg, "rotation-bg");

   return rotation_effect;
}

static void
_rotation_zone_free(Rotation_Zone *rotation_zone)
{
   if (!rotation_zone) return;

   E_FREE_LIST(rotation_zone->event_hdlrs, ecore_event_handler_del);

   _rotation_effect_free(rotation_zone->effect);

   E_FREE(rotation_zone);

   return;
}

static Rotation_Zone *
_rotation_zone_create(E_Zone *zone)
{
   Rotation_Zone *rotation_zone = NULL;

   if (!zone) return NULL;
   if ((zone->w == 0) || (zone->h == 0)) return NULL;

   rotation_zone = E_NEW(Rotation_Zone, 1);
   if (!rotation_zone) return NULL;

   /* create rotation effect data */
   rotation_zone->effect = _rotation_effect_create(zone);
   if (!rotation_zone->effect)
     {
        E_FREE(rotation_zone);
        return NULL;
     }

   rotation_zone->zone = zone;
   rotation_zone->curr_angle = zone->rot.curr;
   rotation_zone->prev_angle = zone->rot.prev;

   E_LIST_HANDLER_APPEND(rotation_zone->event_hdlrs,
                         E_EVENT_ZONE_ROTATION_CHANGE_BEGIN,
                         _rotation_effect_cb_zone_rotation_begin, rotation_zone->effect);

   E_LIST_HANDLER_APPEND(rotation_zone->event_hdlrs,
                         E_EVENT_ZONE_ROTATION_CHANGE_END,
                         _rotation_effect_cb_zone_rotation_end, rotation_zone->effect);

   E_LIST_HANDLER_APPEND(rotation_zone->event_hdlrs,
                         E_EVENT_ZONE_ROTATION_CHANGE_CANCEL,
                         _rotation_effect_cb_zone_rotation_cancel, rotation_zone->effect);

   E_LIST_HANDLER_APPEND(rotation_zone->event_hdlrs,
                         E_EVENT_CLIENT_BUFFER_CHANGE,
                         _rotation_effect_cb_buffer_change, rotation_zone->effect);


   return rotation_zone;
}

EAPI Eina_Bool
e_mod_effect_rotation_init(void)
{
   _rotation_zone = _rotation_zone_create(e_zone_current_get());
   if (!_rotation_zone) return EINA_FALSE;

   return EINA_TRUE;
}

EAPI void
e_mod_effect_rotation_shutdown(void)
{
   if (_rotation_zone)
     _rotation_zone_free(_rotation_zone);

   _rotation_zone = NULL;
}
