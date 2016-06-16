#include "e_mod_effect.h"

static E_Effect *_effect = NULL;

typedef struct _E_Effect_Client
{
   E_Client *ec;
   unsigned int animating;
   E_Comp_Wl_Buffer_Ref buffer_ref;
   E_Pixmap *ep;
} E_Effect_Client;

static void
_e_mod_effect_event_send(E_Client *ec, Eina_Bool start, E_Effect_Type type)
{
   struct wl_resource *surface_resource;
   struct wl_resource *effect_resource;
   struct wl_client *wc;
   unsigned int tizen_effect_type = TIZEN_EFFECT_TYPE_NONE;

   if (!_effect) return;
   if ((!ec) || (!ec->comp_data)) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   surface_resource = ec->comp_data->surface;
   if (!surface_resource) return;

   wc = wl_resource_get_client(surface_resource);
   if (!wc) return;

   effect_resource = eina_hash_find(_effect->resources, &wc);
   if (!effect_resource) return;

   switch(type)
     {
      case E_EFFECT_TYPE_SHOW:
         tizen_effect_type = TIZEN_EFFECT_TYPE_SHOW;
         break;
      case E_EFFECT_TYPE_HIDE:
         tizen_effect_type = TIZEN_EFFECT_TYPE_HIDE;
         break;
      case E_EFFECT_TYPE_RESTACK_SHOW:
      case E_EFFECT_TYPE_RESTACK_HIDE:
         tizen_effect_type = TIZEN_EFFECT_TYPE_RESTACK;
         break;
      default:
         ERR("Unsupported effect type: %d for %p", type, ec);
         return;
     }

   EFFINF("SEND %.5s|type:%d|win:0x%08x|tz_effect:0x%08x",
          ec->pixmap, ec,
          start? "START":"END", type,
          (unsigned int)e_client_util_win_get(ec),
          (unsigned int)effect_resource);

   if (start)
     tizen_effect_send_start(effect_resource, surface_resource, tizen_effect_type);
   else
     tizen_effect_send_end(effect_resource, surface_resource, tizen_effect_type);
}

static E_Effect_Client*
_e_mod_effect_client_new(E_Client *ec)
{
   E_Effect_Client* efc;

   efc = E_NEW(E_Effect_Client, 1);
   efc->ec = ec;
   efc->animating = 0;
   efc->ep = NULL;

   return efc;
}

static E_Effect_Client *
_e_mod_effect_client_get(E_Client *ec)
{
   E_Effect_Client *efc;

   if (!_effect) return NULL;

   efc = eina_hash_find(_effect->clients, &ec);

   return efc;
}

static E_Effect_Group
_e_mod_effect_group_get(E_Client *ec)
{
   E_Effect_Group group = E_EFFECT_GROUP_NORMAL;

   /* animatable setting by aux_hint */
   if (ec->animatable) return E_EFFECT_GROUP_NONE;

   /* client_type */
   switch (ec->client_type)
     {
      case 1: //homescreen
         group = E_EFFECT_GROUP_HOME;
         break;
      case 2: //lockscreen
         group = E_EFFECT_GROUP_LOCKSCREEN;
         break;
      default:
         break;
     }

   /* client layer */
   if (group == E_EFFECT_GROUP_NORMAL)
     {
        if (ec->layer > E_LAYER_CLIENT_NORMAL)
          group = E_EFFECT_GROUP_NONE;
     }

   /* window_role */

   /* etc */
   if (ec->vkbd.vkbd)
     group = E_EFFECT_GROUP_KEYBOARD;

   return group;
}

static Eina_Bool
_e_mod_effect_ref(E_Client *ec)
{
   E_Effect_Client *efc;

   if (!_effect) return EINA_FALSE;

   if (e_object_is_del(E_OBJECT(ec)))
     {
        ERR("Client is deleted already! ec(%p)", ec);
        eina_hash_del_by_key(_effect->clients, &ec);
        return EINA_FALSE;
     }

   efc = _e_mod_effect_client_get(ec);
   if (!efc) return EINA_FALSE;

   if (!ec->pixmap) return EINA_FALSE;
   if ((e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_EXT_OBJECT) &&
       (!e_pixmap_usable_get(ec->pixmap)))
     return EINA_FALSE;

   efc->animating ++;
   e_object_ref(E_OBJECT(ec));
   efc->ep = e_pixmap_ref(ec->pixmap);

   EFFINF("Effect ref efc(%p) animating:%d",
          efc->ep, efc->ec, efc, efc->animating);

   return EINA_TRUE;
}

static E_Client *
_e_mod_effect_unref(E_Client *ec)
{
   E_Effect_Client *efc;
   int do_unref = 1;

   if (!_effect) return NULL;

   efc = _e_mod_effect_client_get(ec);
   if (!efc) return NULL;

   if (e_object_is_del(E_OBJECT(ec)))
     do_unref = efc->animating;

   efc->animating -= do_unref;
   while (do_unref)
     {
        e_pixmap_free(efc->ep);
        if (!e_object_unref(E_OBJECT(ec)))
          {
             EFFINF("Effect unref efc(%p) Client free'd",
                    efc->ep, ec, efc);

             efc->ec = NULL;
             efc = NULL;
             eina_hash_del_by_key(_effect->clients, &ec);
             return NULL;
          }
        do_unref --;
     }

   EFFINF("Effect Unref efc(%p) animating:%d",
          ec->pixmap, ec, efc, efc->animating);

   return ec;
}

static void
_e_mod_effect_object_setup(E_Client *ec)
{
   E_Comp_Config *config;
   config = e_comp_config_get();

   if (ec->vkbd.vkbd)
     {
        e_comp_object_effect_set(ec->frame, "keyboard");
     }
   else
     {
        if ((config) && (config->effect_style))
          {
             e_comp_object_effect_set(ec->frame , config->effect_style);
          }
        else
          e_comp_object_effect_set(ec->frame, "no-effect");
     }
}

static void
_e_mod_effect_pending_effect_start()
{
   E_Client *ec;

   ec = _effect->next_done.ec;
   if (!ec) return;

   EFFINF("Pending Effect Start type(%d)",
          ec->pixmap, ec, _effect->next_done.type);

   if (_effect->next_done.cb)
     {
        _e_mod_effect_event_send(ec, EINA_TRUE, _effect->next_done.type);
        e_comp_object_effect_start(ec->frame,
                                   _effect->next_done.cb,
                                   _effect->next_done.data);
     }

   memset(&_effect->next_done, 0, sizeof(_effect->next_done));
}

static void
_e_mod_effect_pending_effect_set(E_Client *ec, void *data, E_Effect_Type type, Edje_Signal_Cb done_cb)
{
   _e_mod_effect_pending_effect_start();

   EFFINF("Pending Effect Set type(%d)",
          ec->pixmap, ec, type);

   _effect->next_done.cb = done_cb;
   _effect->next_done.ec = ec;
   _effect->next_done.data = data;
   _effect->next_done.type = type;
}

static void
_e_mod_effect_stack_update()
{
   E_Client *ec;
   Evas_Object *o;

   if (!_effect) return;

   _effect->stack.old = eina_list_free(_effect->stack.old);
   _effect->stack.old = eina_list_clone(_effect->stack.cur);

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_client_util_ignored_get(ec)) continue;
        if (e_object_is_del(E_OBJECT(ec))) continue;

        _effect->stack.cur = eina_list_remove(_effect->stack.cur, ec);
        _effect->stack.cur = eina_list_append(_effect->stack.cur, ec);
     }
}

static Eina_Bool
_e_mod_effect_visibility_stack_check(E_Client *ec, Eina_List *stack)
{
   Eina_List *l;
   E_Client *_ec;
   Eina_Tiler *tiler;
   Eina_Rectangle r;
   Eina_Bool vis = EINA_TRUE;
   int x, y, w, h;

   if (!stack) return EINA_FALSE;

   tiler = eina_tiler_new(ec->zone->w, ec->zone->h);
   eina_tiler_tile_size_set(tiler, 1, 1);
   EINA_RECTANGLE_SET(&r, ec->zone->x, ec->zone->y, ec->zone->w, ec->zone->h);
   eina_tiler_rect_add(tiler, &r);

   EINA_LIST_FOREACH(stack, l, _ec)
     {
        if (_ec == ec) break;
        if (!_ec->visible) continue;
        if (!evas_object_visible_get(_ec->frame))
          {
             if (!_ec->iconic) continue;
             if (_ec->iconic && _ec->exp_iconify.by_client) continue;
          }
        if (!e_pixmap_resource_get(_ec->pixmap)) continue;

        e_client_geometry_get(_ec, &x, &y, &w, &h);

        EINA_RECTANGLE_SET(&r, x, y, w, h);
        eina_tiler_rect_del(tiler, &r);

        if (eina_tiler_empty(tiler))
          {
             vis = EINA_FALSE;
             break;
          }
     }
   eina_tiler_free(tiler);

   return vis;
}

static const char*
_e_mod_effect_restack_effect_check(E_Client *ec)
{
   const char* emission = NULL;
   Eina_Bool v1, v2;

   if (!evas_object_visible_get(ec->frame)) return NULL;
   if (ec->new_client) return NULL;

   v1 = _e_mod_effect_visibility_stack_check(ec, _effect->stack.old);
   v2 = _e_mod_effect_visibility_stack_check(ec, _effect->stack.cur);

   if (v1 != v2)
     {
        if (v2 && ec->visibility.obscured != E_VISIBILITY_UNOBSCURED)
          emission = "e,action,restack,show";
        else if (!v2 && ec->visibility.obscured == E_VISIBILITY_FULLY_OBSCURED)
          emission = "e,action,restack,hide";
     }

   EFFINF("Restack Effect Check v1(%d) -> v2(%d) obscured:%d emission:%s",
          ec->pixmap, ec,
          v1, v2, ec->visibility.obscured, emission);

   return emission;
}

static void
_e_mod_effect_cb_visible_done(void *data, Evas_Object *obj EINA_UNUSED, const char *sig EINA_UNUSED, const char *src EINA_UNUSED)
{
   E_Client *ec = NULL;

   if ((ec = (E_Client*) data))
     {
        _e_mod_effect_event_send(ec, EINA_FALSE, E_EFFECT_TYPE_SHOW);
        if (_e_mod_effect_unref(ec))
          {
             if (_e_mod_effect_client_get(ec))
               {
                  if (!eina_list_data_find(_effect->stack.cur, ec))
                    _e_mod_effect_stack_update();

                  e_client_visibility_skip_set(ec, EINA_FALSE);
               }
          }
     }

   e_comp_override_del();
}

static Eina_Bool
_e_mod_effect_cb_visible(void *data, Evas_Object *obj, const char *signal)
{
   E_Client *ec;
   E_Effect_Group group;

   if (!_effect) return EINA_FALSE;

   ec = e_comp_object_client_get(obj);
   if (!ec) return EINA_FALSE;
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   group = _e_mod_effect_group_get(ec);
   if (group != E_EFFECT_GROUP_NORMAL) return EINA_FALSE;

   if (evas_object_visible_get(obj)) return EINA_FALSE;
   if (!_e_mod_effect_ref(ec)) return EINA_FALSE;

   e_comp_override_add();
   e_client_visibility_skip_set(ec, EINA_TRUE);

   _e_mod_effect_object_setup(ec);
   e_comp_object_effect_params_set(ec->frame, 0, (int[]){0}, 1);
   if (e_comp->nocomp)
     {
         _e_mod_effect_pending_effect_set(ec,
                                          (void*)ec,
                                          E_EFFECT_TYPE_SHOW,
                                          _e_mod_effect_cb_visible_done);
         return EINA_TRUE;
     }

   _e_mod_effect_event_send(ec, EINA_TRUE, E_EFFECT_TYPE_SHOW);
   e_comp_object_effect_start(ec->frame, _e_mod_effect_cb_visible_done, ec);

   return EINA_TRUE;
}

static void
_e_mod_effect_cb_hidden_done(void *data, Evas_Object *obj, const char *sig, const char *src)
{
   E_Client *ec = NULL;


   if ((ec = (E_Client*) data))
     {
        _e_mod_effect_event_send(ec, EINA_FALSE, E_EFFECT_TYPE_HIDE);
        if (_e_mod_effect_unref(ec))
          {
             if (_e_mod_effect_client_get(ec))
               {
                  evas_object_layer_set(ec->frame, ec->layer);
                  ec->layer_block = 0;
                  e_client_visibility_skip_set(ec, EINA_FALSE);
                  evas_object_hide(ec->frame);
               }
          }
     }

   e_comp_override_del();
}

static Eina_Bool
_e_mod_effect_cb_hidden(void *data, Evas_Object *obj, const char *signal)
{
   E_Client *ec;
   E_Effect_Group group;
   Eina_Bool lowered = 0;
   Evas_Object *below;
   int map_layer;

   if (!_effect) return EINA_FALSE;

   ec = e_comp_object_client_get(obj);
   if (!ec) return EINA_FALSE;
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   group = _e_mod_effect_group_get(ec);
   if (group != E_EFFECT_GROUP_NORMAL) return EINA_FALSE;

   if (!evas_object_visible_get(obj)) return EINA_FALSE;
   if (!_e_mod_effect_ref(ec)) return EINA_FALSE;

   e_comp_override_add();

   //check if client was lowered
   below = evas_object_below_get(obj);
   map_layer = e_comp_canvas_layer_map(evas_object_layer_get(obj));
   if ((below) &&
       (evas_object_layer_get(below) != evas_object_layer_get(obj)) &&
       (evas_object_above_get(obj) != e_comp->layers[map_layer].obj))
     lowered = 1;

   if (lowered)
     {
        ec->layer_block = 1;
        evas_object_layer_set(ec->frame, ec->layer + 1);
        e_client_visibility_skip_set(ec, EINA_TRUE);
     }

   _e_mod_effect_object_setup(ec);
   e_comp_object_effect_params_set(ec->frame, 0, (int[]){1}, 1);

   if (e_comp->nocomp)
     {
        _e_mod_effect_pending_effect_set(ec,
                                         (void*)ec,
                                         E_EFFECT_TYPE_HIDE,
                                         _e_mod_effect_cb_hidden_done);
        return EINA_TRUE;
     }

   _e_mod_effect_event_send(ec, EINA_TRUE, E_EFFECT_TYPE_HIDE);
   e_comp_object_effect_start(ec->frame, _e_mod_effect_cb_hidden_done, ec);

   return EINA_TRUE;
}

static void
_e_mod_effect_cb_uniconify_done(void *data, Evas_Object *obj, const char *sig, const char *src)
{
   E_Client *ec = NULL;

   if ((ec = (E_Client*) data))
     {
        _e_mod_effect_event_send(ec, EINA_FALSE, E_EFFECT_TYPE_SHOW);
        _e_mod_effect_unref(ec);
     }

   e_comp_override_del();
}

static Eina_Bool
_e_mod_effect_cb_uniconify(void *data, Evas_Object *obj, const char *signal)
{
   E_Client *ec;
   E_Effect_Group group;
   Eina_Bool v1, v2;

   if (!_effect) return EINA_FALSE;

   ec = e_comp_object_client_get(obj);
   if (!ec) return EINA_FALSE;
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   if (evas_object_visible_get(obj)) return EINA_FALSE;

   group = _e_mod_effect_group_get(ec);

   /* for HOME group */
   if (group == E_EFFECT_GROUP_HOME)
     {
        E_Client *below;
        below = e_client_below_get(ec);
        while (below)
          {
             if ((evas_object_visible_get(below->frame)) &&
                 (below->layer == ec->layer) &&
                 ((below->visibility.obscured == E_VISIBILITY_UNOBSCURED) ||
                  (below->visibility.changed)))
               break;

             below = e_client_below_get(below);
          }

        if (!below) return EINA_FALSE;
        group = _e_mod_effect_group_get(below);
        if (group != E_EFFECT_GROUP_NORMAL) return EINA_FALSE;

        EFFINF("for HOME group do hide effect of %p",
               ec->pixmap, ec, below);
        e_comp_object_signal_emit(below->frame, "e,action,restack,hide", "e");
        return EINA_TRUE;
     }
   /* for NORMAL group */
   else if (group == E_EFFECT_GROUP_NORMAL)
     {
        v1 = _e_mod_effect_visibility_stack_check(ec, _effect->stack.old);
        v2 = _e_mod_effect_visibility_stack_check(ec, _effect->stack.cur);

        if (v1 == v2) return EINA_FALSE;
        if ((v2) && (ec->visibility.obscured == E_VISIBILITY_UNOBSCURED)) return EINA_FALSE;
        if ((!v2) && (ec->visibility.obscured != E_VISIBILITY_UNOBSCURED)) return EINA_FALSE;

        if (!_e_mod_effect_ref(ec)) return EINA_FALSE;

        e_comp_override_add();

        _e_mod_effect_object_setup(ec);
        e_comp_object_effect_params_set(ec->frame, 0, (int[]){0}, 1);

        if (e_comp->nocomp)
          {
             _e_mod_effect_pending_effect_set(ec,
                                              (void*)ec,
                                              E_EFFECT_TYPE_SHOW,
                                              _e_mod_effect_cb_uniconify_done);
             return EINA_TRUE;
          }

        _e_mod_effect_event_send(ec, EINA_TRUE, E_EFFECT_TYPE_SHOW);
        e_comp_object_effect_start(ec->frame, _e_mod_effect_cb_uniconify_done, ec);
        return EINA_TRUE;
     }

   return EINA_FALSE;
}

static void
_e_mod_effect_cb_iconify_done(void *data, Evas_Object *obj, const char *sig, const char *src)
{
   E_Client *ec = NULL;

   if ((ec = (E_Client*) data))
     {
        _e_mod_effect_event_send(ec, EINA_FALSE, E_EFFECT_TYPE_HIDE);
        if (_e_mod_effect_unref(ec))
          {
             if (_e_mod_effect_client_get(ec))
               evas_object_hide(ec->frame);
          }
     }

   e_comp_override_del();
}

static Eina_Bool
_e_mod_effect_cb_iconify(void *data, Evas_Object *obj, const char *signal)
{
   E_Client *ec;
   E_Effect_Group group;

   if (!_effect) return EINA_FALSE;

   ec = e_comp_object_client_get(obj);
   if (!ec) return EINA_FALSE;
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   group = _e_mod_effect_group_get(ec);
   if (group != E_EFFECT_GROUP_NORMAL) return EINA_FALSE;

   if (!evas_object_visible_get(obj)) return EINA_FALSE;
   if (!_e_mod_effect_visibility_stack_check(ec, _effect->stack.cur)) return EINA_FALSE;
   if (!_e_mod_effect_ref(ec)) return EINA_FALSE;

   e_comp_override_add();

   _e_mod_effect_object_setup(ec);
   e_comp_object_effect_params_set(ec->frame, 0, (int[]){1}, 1);

   if (e_comp->nocomp)
     {
        _e_mod_effect_pending_effect_set(ec,
                                         (void*)ec,
                                         E_EFFECT_TYPE_HIDE,
                                         _e_mod_effect_cb_iconify_done);
        return EINA_TRUE;
     }

   _e_mod_effect_event_send(ec, EINA_TRUE, E_EFFECT_TYPE_HIDE);
   e_comp_object_effect_start(ec->frame, _e_mod_effect_cb_iconify_done, ec);

   return EINA_TRUE;
}
static void
_e_mod_effect_cb_restack_show_done(void *data, Evas_Object *obj, const char *sig, const char *src)
{
   E_Client *ec = NULL;

   if ((ec = (E_Client*)data))
     {
        if (_e_mod_effect_unref(ec))
          {
             if (_e_mod_effect_client_get(ec))
               {
                  evas_object_layer_set(ec->frame, ec->layer);
                  ec->layer_block = 0;
                  e_client_visibility_skip_set(ec, EINA_FALSE);
                  _e_mod_effect_stack_update();
               }
          }
     }

   e_comp_override_del();
}

static void
_e_mod_effect_cb_restack_hide_done(void *data, Evas_Object *obj, const char *sig, const char *src)
{
   E_Client *ec = NULL;

   if ((ec = (E_Client*)data))
     {
        if (_e_mod_effect_unref(ec))
          {
             if (_e_mod_effect_client_get(ec))
               {
                  evas_object_layer_set(ec->frame, ec->layer);
                  ec->layer_block = 0;
                  e_client_visibility_skip_set(ec, EINA_FALSE);
                  _e_mod_effect_stack_update();

                  e_comp_object_signal_emit(ec->frame,
                                            "e,action,restack,finish", "e");
               }
          }
     }

   e_comp_override_del();
}

static void
_e_mod_effect_cb_restack_finish_done(void *data, Evas_Object *obj, const char *sig, const char *src)
{
   E_Client *ec = NULL;

   if ((ec = (E_Client*)data))
     {
        _e_mod_effect_event_send(ec, EINA_FALSE, E_EFFECT_TYPE_RESTACK_HIDE);
        _e_mod_effect_unref(ec);
     }

   e_comp_override_del();
}

static Eina_Bool
_e_mod_effect_cb_restack(void *data, Evas_Object *obj, const char *signal)
{
   E_Client *ec;
   E_Effect_Group group;
   const char *emission;

   if (!_effect) return EINA_FALSE;

   ec = e_comp_object_client_get(obj);
   if (!ec) return EINA_FALSE;
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   group = _e_mod_effect_group_get(ec);
   emission = eina_stringshare_add(signal);

   /* for HOME group: replace effect target client */
   if (group == E_EFFECT_GROUP_HOME)
     {
        E_Client *below;

        below = e_client_below_get(ec);
        while (below)
          {
             if ((!e_object_is_del(E_OBJECT(below))) &&
                 (evas_object_visible_get(below->frame)) &&
                 (below->visibility.obscured == E_VISIBILITY_UNOBSCURED) &&
                 (below->layer == ec->layer))
               break;

             below = e_client_below_get(below);
          }

        if (!below) return EINA_FALSE;
        if (e_util_strcmp(signal, "e,action,restack,show")) return EINA_FALSE;

        ec = below;
        group = _e_mod_effect_group_get(ec);

        if (emission) eina_stringshare_del(emission);
        emission = eina_stringshare_add("e,action,restack,hide");
     }

   if (group != E_EFFECT_GROUP_NORMAL) return EINA_FALSE;
   if ((!e_util_strcmp(emission, "e,action,restack,show")))
     {
        if (!_e_mod_effect_ref(ec)) return EINA_FALSE;

        e_comp_override_add();

        ec->layer_block = 1;
        evas_object_layer_set(ec->frame, ec->layer + 1);
        e_client_visibility_skip_set(ec, EINA_TRUE);

        _e_mod_effect_object_setup(ec);
        e_comp_object_effect_params_set(ec->frame, 0, (int[]){2}, 1);

        if (e_comp->nocomp)
          {
             _e_mod_effect_pending_effect_set(ec,
                                              (void*)ec,
                                              E_EFFECT_TYPE_SHOW,
                                              _e_mod_effect_cb_restack_show_done);
             return EINA_TRUE;
          }

        _e_mod_effect_event_send(ec, EINA_TRUE, E_EFFECT_TYPE_RESTACK_SHOW);
        e_comp_object_effect_start(ec->frame, _e_mod_effect_cb_restack_show_done, ec);
     }
   else if (!e_util_strcmp(emission, "e,action,restack,hide"))
     {
        if (!_e_mod_effect_ref(ec)) return EINA_FALSE;

        e_comp_override_add();

        ec->layer_block = 1;
        evas_object_layer_set(ec->frame, ec->layer + 1);
        e_client_visibility_skip_set(ec, EINA_TRUE);

        _e_mod_effect_object_setup(ec);
        e_comp_object_effect_params_set(ec->frame, 0, (int[]){3}, 1);

        if (e_comp->nocomp)
          {
             _e_mod_effect_pending_effect_set(ec,
                                              (void*)ec,
                                              E_EFFECT_TYPE_HIDE,
                                              _e_mod_effect_cb_restack_hide_done);
             return EINA_TRUE;
          }

        _e_mod_effect_event_send(ec, EINA_TRUE, E_EFFECT_TYPE_RESTACK_HIDE);
        e_comp_object_effect_start(ec->frame, _e_mod_effect_cb_restack_hide_done, ec);
     }
   else if (!e_util_strcmp(emission, "e,action,restack,finish"))
     {
        if (!_e_mod_effect_ref(ec)) return EINA_FALSE;

        e_comp_override_add();

        _e_mod_effect_object_setup(ec);
        e_comp_object_effect_params_set(ec->frame, 0, (int[]){4}, 1);

        if (e_comp->nocomp)
          {
             _e_mod_effect_pending_effect_set(ec,
                                              (void*)ec,
                                              E_EFFECT_TYPE_HIDE,
                                              _e_mod_effect_cb_restack_finish_done);
             return EINA_TRUE;
          }

        e_comp_object_effect_start(ec->frame, _e_mod_effect_cb_restack_finish_done, ec);
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_mod_effect_cb_client_add(void *data, int type, void *event)
{
   E_Client *ec;
   E_Effect_Client *efc;
   E_Event_Client *ev = event;

   if (!_effect) return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;
   efc = _e_mod_effect_client_get(ec);
   if (!efc)
     {
        efc = _e_mod_effect_client_new(ec);
        if (efc)
          eina_hash_add(_effect->clients, &ec, efc);
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_mod_effect_cb_client_remove(void *data, int type, void *event)
{
   E_Client *ec;
   E_Effect_Client *efc = NULL;
   E_Event_Client *ev = event;

   if (!_effect) return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;

   _effect->stack.old = eina_list_remove(_effect->stack.old, ec);
   _effect->stack.cur = eina_list_remove(_effect->stack.cur, ec);

   if (_effect->next_done.ec == ec)
     memset(&_effect->next_done, 0, sizeof(_effect->next_done));

   if ((efc = _e_mod_effect_client_get(ec)))
     {
        if (!efc->animating)
          eina_hash_del_by_key(_effect->clients, &ec);
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_mod_effect_cb_client_restack(void *data, int type, void *event)
{
   E_Client *ec;
   E_Event_Client *ev = event;
   const char* emission = NULL;

   if (!_effect) return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;
   if (!ec) return ECORE_CALLBACK_PASS_ON;
   if (e_object_is_del(E_OBJECT(ec))) return ECORE_CALLBACK_PASS_ON;

   _e_mod_effect_stack_update();

   if ((emission = _e_mod_effect_restack_effect_check(ec)))
     e_comp_object_signal_emit(ec->frame, emission, "e");

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_mod_effect_cb_comp_enabled(void *data, int ev_type, void *event)
{
   if (!_effect) return ECORE_CALLBACK_PASS_ON;

   _e_mod_effect_pending_effect_start();

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_mod_effect_cb_client_buffer_change(void *data, int ev_type, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec;
   E_Effect_Client *efc;
   E_Comp_Wl_Buffer *buffer = NULL;

   if (!_effect) return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;
   if (!ec) return ECORE_CALLBACK_PASS_ON;

   efc = _e_mod_effect_client_get(ec);
   if (!efc) return ECORE_CALLBACK_PASS_ON;

   if (ec->pixmap)
     {
        buffer = e_pixmap_resource_get(ec->pixmap);

        if ((buffer) && (buffer != efc->buffer_ref.buffer))
          {
             e_comp_wl_buffer_reference(&efc->buffer_ref, buffer);
          }
     }

   return ECORE_CALLBACK_PASS_ON;
}

static void
_tz_effect_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *tizen_effect_resource)
{
   wl_resource_destroy(tizen_effect_resource);
}

static const struct tizen_effect_interface _tz_effect_interface =
{
   _tz_effect_cb_destroy,
};

static void
_tz_effect_cb_effect_destroy(struct wl_resource *tizen_effect_resource)
{
   if ((!_effect) || (!_effect->resources)) return;

   eina_hash_del_by_data(_effect->resources, tizen_effect_resource);
}

static void
_e_mod_effect_cb_bind(struct wl_client *client, void *data EINA_UNUSED, uint32_t version EINA_UNUSED, uint32_t id)
{
   struct wl_resource *res;

   if (!(res = wl_resource_create(client, &tizen_effect_interface, 1, id)))
     {
        ERR("Could not create tizen_effect interface");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res,
                                  &_tz_effect_interface,
                                  NULL,
                                  _tz_effect_cb_effect_destroy);

   eina_hash_add(_effect->resources, &client, res);
}

static void
_e_mod_effect_cb_client_data_free(void *data)
{
   E_Effect_Client *efc = data;

   if (!efc) return;

   if (efc->buffer_ref.buffer)
     e_comp_wl_buffer_reference(&efc->buffer_ref, NULL);

   free(efc);
}

EAPI Eina_Bool
e_mod_effect_init(void)
{
   E_Effect *effect;
   E_Comp_Config *config;

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp->evas, EINA_FALSE);

   effect = E_NEW(E_Effect, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(effect, EINA_FALSE);

   _effect = effect;

   if ((config = e_comp_config_get()))
     {
        effect->file = eina_stringshare_add(config->effect_file);
        effect->style = eina_stringshare_add(config->effect_style);
     }
   else
     {
        effect->file = "";
        effect->style = "no-effect";
     }

   effect->clients = eina_hash_pointer_new(_e_mod_effect_cb_client_data_free);
   EINA_SAFETY_ON_NULL_GOTO(effect->clients, err);

   effect->resources = eina_hash_pointer_new(NULL);
   EINA_SAFETY_ON_NULL_GOTO(effect->resources, err);

   effect->global = wl_global_create(e_comp_wl->wl.disp, &tizen_effect_interface, 1, effect, _e_mod_effect_cb_bind);
   if (!effect->global)
     {
        ERR("Could not add tizen_efffect wayland globals: %m");
        goto err;
     }

   E_LIST_HANDLER_APPEND(effect->event_hdlrs, E_EVENT_COMPOSITOR_ENABLE,
                         _e_mod_effect_cb_comp_enabled, effect);

   E_LIST_HANDLER_APPEND(effect->event_hdlrs, E_EVENT_CLIENT_BUFFER_CHANGE,
                         _e_mod_effect_cb_client_buffer_change, effect);

   E_LIST_HANDLER_APPEND(effect->event_hdlrs, E_EVENT_CLIENT_ADD,
                         _e_mod_effect_cb_client_add, effect);

   E_LIST_HANDLER_APPEND(effect->event_hdlrs, E_EVENT_CLIENT_REMOVE,
                         _e_mod_effect_cb_client_remove, effect);

   E_LIST_HANDLER_APPEND(effect->event_hdlrs, E_EVENT_CLIENT_STACK,
                         _e_mod_effect_cb_client_restack, effect);

   effect->providers =
      eina_list_append(effect->providers,
                       e_comp_object_effect_mover_add(100,
                                                      "e,state,visible",
                                                      _e_mod_effect_cb_visible,
                                                      effect));
   effect->providers =
      eina_list_append(effect->providers,
                       e_comp_object_effect_mover_add(100,
                                                      "e,state,hidden",
                                                      _e_mod_effect_cb_hidden,
                                                      effect));
   effect->providers =
      eina_list_append(effect->providers,
                       e_comp_object_effect_mover_add(100,
                                                      "e,action,iconify",
                                                      _e_mod_effect_cb_iconify,
                                                      effect));
   effect->providers =
      eina_list_append(effect->providers,
                       e_comp_object_effect_mover_add(100,
                                                      "e,action,uniconify",
                                                      _e_mod_effect_cb_uniconify,
                                                      effect));
   effect->providers =
      eina_list_append(effect->providers,
                       e_comp_object_effect_mover_add(100,
                                                      "e,action,restack*",
                                                      _e_mod_effect_cb_restack,
                                                      effect));

   _effect = effect;

   return EINA_TRUE;

err:
   e_mod_effect_shutdown();
   return EINA_FALSE;
}

EAPI void
e_mod_effect_shutdown()
{
   if (!_effect) return;

   E_FREE_FUNC(_effect->stack.old, eina_list_free);
   E_FREE_FUNC(_effect->stack.cur, eina_list_free);

   E_FREE_LIST(_effect->providers,  e_comp_object_effect_mover_del);
   E_FREE_LIST(_effect->event_hdlrs, ecore_event_handler_del);

   if (_effect->global)
     wl_global_destroy(_effect->global);

   E_FREE_FUNC(_effect->resources, eina_hash_free);
   E_FREE_FUNC(_effect->clients, eina_hash_free);

   E_FREE(_effect);
}
