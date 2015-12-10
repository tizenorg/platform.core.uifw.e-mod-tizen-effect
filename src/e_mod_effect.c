#include "e.h"
#include "e_mod_effect.h"


E_Effect *_effect = NULL;

typedef struct _E_Effect_Client
{
   E_Client *ec;
   unsigned int animating;
   E_Pixmap *ep;
} E_Effect_Client;

static void
_e_mod_effect_ref(E_Client *ec)
{
   E_Effect_Client *efc;

   if (!_effect) return;

   if (e_object_is_del(E_OBJECT(ec)))
     {
        eina_hash_del_by_key(_effect->clients, &ec);
        return;
     }

   efc = eina_hash_find(_effect->clients, &ec);
   if (!efc) return;

   efc->animating ++;
   e_object_ref(E_OBJECT(ec));
   efc->ep = e_pixmap_ref(ec->pixmap);
}

static void
_e_mod_effect_unref(E_Client *ec)
{
   E_Effect_Client *efc;

   if (!_effect) return;

   efc = eina_hash_find(_effect->clients, &ec);
   if (!efc) return;

   while(efc->animating)
     {
        e_pixmap_free(efc->ep);
        if (!e_object_unref(E_OBJECT(ec)))
          {
             efc = NULL;
             eina_hash_del_by_key(_effect->clients, &ec);
             break;
          }

        efc->animating --;
     }

   if (efc)
     efc->ep = NULL;
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

        _effect->stack.cur = eina_list_remove(_effect->stack.cur, ec);
        _effect->stack.cur = eina_list_append(_effect->stack.cur, ec);
     }
}

static Eina_Bool
_e_mod_effect_visibility_check(E_Client *ec, Eina_List *stack)
{
   Eina_List *l;
   E_Client *_ec;
   Eina_Tiler *tiler;
   Eina_Rectangle r;
   Eina_Bool vis = EINA_TRUE;

   if (!stack) return EINA_FALSE;

   tiler = eina_tiler_new(ec->zone->w, ec->zone->h);
   eina_tiler_tile_size_set(tiler, 1, 1);
   EINA_RECTANGLE_SET(&r, ec->zone->x, ec->zone->y, ec->zone->w, ec->zone->h);
   eina_tiler_rect_add(tiler, &r);

   EINA_LIST_FOREACH(stack, l, _ec)
     {
        if (_ec == ec) break;
        if ((_ec->iconic) || (!_ec->visible) || (_ec->argb)) continue;

        EINA_RECTANGLE_SET(&r, _ec->x, _ec->y, _ec->w, _ec->h);
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

   if (!_effect) return NULL;
   if (!ec->visible) return NULL;
   if (ec->new_client) return NULL;

   v1 = _e_mod_effect_visibility_check(ec, _effect->stack.old);
   v2 = _e_mod_effect_visibility_check(ec, _effect->stack.cur);

   if (v1 != v2)
     {
        if (v2) emission = "e,action,restack,show";
        else emission = "e,action,restack,hide";
     }

   return emission;
}

static void
_e_mod_effect_cb_visible_done(void *data, Evas_Object *obj, const char *sig, const char *src)
{
   E_Client *ec;
   ec = (E_Client*) data;

   _e_mod_effect_unref(ec);
}

static Eina_Bool
_e_mod_effect_cb_visible(void *data, Evas_Object *obj, const char *signal)
{
   E_Client *ec;
   ec = e_comp_object_client_get(obj);

   _e_mod_effect_ref(ec);
   _e_mod_effect_object_setup(ec);
   e_comp_object_effect_params_set(ec->frame, 0, (int[]){0}, 1);
   e_comp_object_effect_start(ec->frame, _e_mod_effect_cb_visible_done, ec);

   return EINA_TRUE;
}

static void
_e_mod_effect_cb_hidden_done(void *data, Evas_Object *obj, const char *sig, const char *src)
{
   E_Client *ec;
   ec = (E_Client*) data;

   _e_mod_effect_unref(ec);

   if (ec->iconic)
     evas_object_hide(ec->frame);
}

static Eina_Bool
_e_mod_effect_cb_hidden(void *data, Evas_Object *obj, const char *signal)
{
   E_Client *ec;
   ec = e_comp_object_client_get(obj);

   _e_mod_effect_ref(ec);

   _e_mod_effect_object_setup(ec);
   e_comp_object_effect_params_set(ec->frame, 0, (int[]){1}, 1);
   e_comp_object_effect_start(ec->frame, _e_mod_effect_cb_hidden_done, ec);

   return EINA_TRUE;
}

static void
_e_mod_effect_cb_restack_show_done(void *data, Evas_Object *obj, const char *sig, const char *src)
{
   E_Client *ec;
   ec = (E_Client*)data;

   _e_mod_effect_unref(ec);
   evas_object_layer_set(ec->frame, ec->layer);
   ec->layer_block = 0;
   _e_mod_effect_stack_update();
}

static void
_e_mod_effect_cb_restack_hide_done(void *data, Evas_Object *obj, const char *sig, const char *src)
{
   E_Client *ec = data;
   ec = (E_Client*)data;

   _e_mod_effect_unref(ec);
   evas_object_layer_set(ec->frame, ec->layer);
   ec->layer_block = 0;
   _e_mod_effect_stack_update();

   e_comp_object_signal_emit(ec->frame, "e,action,restack,finish", "e");
}

static void
_e_mod_effect_cb_restack_finish_done(void *data, Evas_Object *obj, const char *sig, const char *src)
{
   E_Client *ec = data;
   ec = (E_Client*)data;

   _e_mod_effect_unref(ec);
}

static Eina_Bool
_e_mod_effect_cb_restack(void *data, Evas_Object *obj, const char *signal)
{
   E_Client *ec;
   ec = e_comp_object_client_get(obj);

   _e_mod_effect_object_setup(ec);
   if ((!e_util_strcmp(signal, "e,action,restack,show")))
     {
        _e_mod_effect_ref(ec);
        ec->layer_block = 1;
        evas_object_layer_set(ec->frame, E_LAYER_CLIENT_PRIO);
        e_comp_object_effect_params_set(ec->frame, 0, (int[]){2}, 1);
        e_comp_object_effect_start(ec->frame, _e_mod_effect_cb_restack_show_done, ec);
     }
   else if (!e_util_strcmp(signal, "e,action,restack,hide"))
     {
        _e_mod_effect_ref(ec);
        ec->layer_block = 1;
        evas_object_layer_set(ec->frame, E_LAYER_CLIENT_PRIO);
        e_comp_object_effect_params_set(ec->frame, 0, (int[]){3}, 1);
        e_comp_object_effect_start(ec->frame, _e_mod_effect_cb_restack_hide_done, ec);
     }
   else if (!e_util_strcmp(signal, "e,action,restack,finish"))
     {
        _e_mod_effect_ref(ec);
        e_comp_object_effect_params_set(ec->frame, 0, (int[]){4}, 1);
        e_comp_object_effect_start(ec->frame, _e_mod_effect_cb_restack_finish_done, ec);
     }

   return EINA_TRUE;
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

static Eina_Bool
_e_mod_effect_cb_client_add(void *data, int type, void *event)
{
   E_Client *ec;
   E_Effect_Client *efc;
   E_Event_Client *ev = event;

   if (!_effect) return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;
   efc = eina_hash_find(_effect->clients, &ec);
   if (!efc)
     {
        efc = _e_mod_effect_client_new(ec);
        eina_hash_add(_effect->clients, &ec, efc);
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_mod_effect_cb_client_remove(void *data, int type, void *event)
{
   E_Client *ec;
   E_Event_Client *ev = event;

   if (!_effect) return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;
   _effect->stack.old = eina_list_remove(_effect->stack.old, ec);
   _effect->stack.cur = eina_list_remove(_effect->stack.cur, ec);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_mod_effect_cb_client_restack(void *data, int type, void *event)
{
   E_Client *ec;
   E_Event_Client *ev = event;
   const char* emission = NULL;

   ec = ev->ec;
   _e_mod_effect_stack_update();
   if ((emission = _e_mod_effect_restack_effect_check(ec)))
     e_comp_object_signal_emit(ec->frame, emission, "e");

   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_mod_effect_cb_client_data_free(void *data)
{
   free(data);
}

EAPI Eina_Bool
e_mod_effect_init(void)
{
   E_Effect *effect;
   E_Comp_Config *config;

   if (!e_comp) return EINA_FALSE;
   if (!(effect = E_NEW(E_Effect, 1))) return EINA_FALSE;

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

   effect->event_hdlrs =
      eina_list_append(effect->event_hdlrs,
                       ecore_event_handler_add(E_EVENT_CLIENT_ADD,
                                               _e_mod_effect_cb_client_add,
                                               effect));
   effect->event_hdlrs =
      eina_list_append(effect->event_hdlrs,
                       ecore_event_handler_add(E_EVENT_CLIENT_REMOVE,
                                               _e_mod_effect_cb_client_remove,
                                               effect));
   effect->event_hdlrs =
      eina_list_append(effect->event_hdlrs,
                       ecore_event_handler_add(E_EVENT_CLIENT_STACK,
                                               _e_mod_effect_cb_client_restack,
                                               effect));
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
                                                      _e_mod_effect_cb_hidden,
                                                      effect));
   effect->providers =
      eina_list_append(effect->providers,
                       e_comp_object_effect_mover_add(100,
                                                      "e,action,uniconify",
                                                      _e_mod_effect_cb_visible,
                                                      effect));
   effect->providers =
      eina_list_append(effect->providers,
                       e_comp_object_effect_mover_add(100,
                                                      "e,action,restack*",
                                                      _e_mod_effect_cb_restack,
                                                      effect));

   _effect = effect;

   return EINA_TRUE;
}

EAPI void
e_mod_effect_shutdown()
{
   if (!_effect) return;

   E_FREE_FUNC(_effect->clients, eina_hash_free);

   E_FREE_LIST(_effect->providers,  e_comp_object_effect_mover_del);
   E_FREE_LIST(_effect->event_hdlrs, ecore_event_handler_del);

   E_FREE_FUNC(_effect->stack.old, eina_list_free);
   E_FREE_FUNC(_effect->stack.cur, eina_list_free);

   E_FREE(_effect);
}
