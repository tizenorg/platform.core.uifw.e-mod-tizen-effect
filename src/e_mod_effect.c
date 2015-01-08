#include "e.h"
#include "e_mod_effect.h"

E_Comp *_comp = NULL;
Eina_List *_providers = NULL;
Eina_List *_event_hdlrs = NULL;
Eina_List *_stack_old = NULL;
Eina_List *_stack_new = NULL;

static void
_e_mod_effect_object_setup(E_Client *ec)
{
   E_Comp_Config *config;
   config = e_comp_config_get();

   if ((config) && (config->effect_style))
     {
        e_comp_object_effect_set(ec->frame , config->effect_style);
     }
   else
     e_comp_object_effect_set(ec->frame, "no-effect");
}

static void
_e_mod_effect_stack_update()
{
   E_Client *ec;
   Evas_Object *o;

   eina_list_free(_stack_old);
   _stack_old = eina_list_clone(_stack_new);
   for (o = evas_object_top_get(_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (!e_util_strcmp(evas_object_name_get(o), "layer_obj")) continue;

        _stack_new = eina_list_remove(_stack_new, ec);
        _stack_new = eina_list_append(_stack_new, ec);
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

   if (!ec->visible) return NULL;
   if (ec->new_client) return NULL;

   v1 = _e_mod_effect_visibility_check(ec, _stack_old);
   v2 = _e_mod_effect_visibility_check(ec, _stack_new);

   if (v1 != v2)
     {
        if (v2) emission = "e,action,restack,show";
        else emission = "e,action,restack,hide";
     }

   return emission;
}

static Eina_Bool
_e_mod_effect_cb_client_remove(void *data, int type, void *event)
{
   E_Client *ec;
   E_Event_Client *ev = event;

   ec = ev->ec;
   _stack_old = eina_list_remove(_stack_saved, ec);
   _stack_new = eina_list_remove(_stack_new, ec);

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

static Eina_Bool
_e_mod_effect_cb_visible(void *data, Evas_Object *obj, const char *signal)
{
   E_Client *ec;

   ec = e_comp_object_client_get(obj);

   _e_mod_effect_object_setup(ec);
   e_comp_object_effect_params_set(ec->frame, 0, (int[]){0}, 1);
   e_comp_object_effect_start(ec->frame, NULL, NULL);

   return EINA_TRUE;
}

static void
_e_mod_effect_cb_hidden_done(void *data, Evas_Object *obj, const char *sig, const char *src)
{
   E_Client *ec;
   ec = (E_Client*) data;

   if (ec->iconic)
     evas_object_hide(ec->frame);
}

static Eina_Bool
_e_mod_effect_cb_hidden(void *data, Evas_Object *obj, const char *signal)
{
   E_Client *ec;

   ec = e_comp_object_client_get(obj);

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
   evas_object_layer_set(ec->frame, ec->layer);
   ec->layer_block = 0;
   _e_mod_effect_stack_update();
}

static void
_e_mod_effect_cb_restack_hide_done(void *data, Evas_Object *obj, const char *sig, const char *src)
{
   E_Client *ec = data;

   ec = (E_Client*)data;
   evas_object_layer_set(ec->frame, ec->layer);
   ec->layer_block = 0;
   _e_mod_effect_stack_update();

   e_comp_object_signal_emit(ec->frame, "e,action,restack,finish", "e");
}

static Eina_Bool
_e_mod_effect_cb_restack(void *data, Evas_Object *obj, const char *signal)
{
   E_Client *ec;

   ec = e_comp_object_client_get(obj);
   _e_mod_effect_object_setup(ec);

   if ((!e_util_strcmp(signal, "e,action,restack,show")))
     {
        ec->layer_block = 1;
        evas_object_layer_set(ec->frame, E_LAYER_CLIENT_PRIO);
        e_comp_object_effect_params_set(ec->frame, 0, (int[]){2}, 1);
        e_comp_object_effect_start(ec->frame, _e_mod_effect_cb_restack_show_done, ec);
     }
   else if (!e_util_strcmp(signal, "e,action,restack,hide"))
     {
        ec->layer_block = 1;
        evas_object_layer_set(ec->frame, E_LAYER_CLIENT_PRIO);
        e_comp_object_effect_params_set(ec->frame, 0, (int[]){3}, 1);
        e_comp_object_effect_start(ec->frame, _e_mod_effect_cb_restack_hide_done, ec);
     }
   else if (!e_util_strcmp(signal, "e,action,restack,finish"))
     {
        e_comp_object_effect_params_set(ec->frame, 0, (int[]){4}, 1);
        e_comp_object_effect_start(ec->frame, NULL, NULL);
     }

   return EINA_TRUE;
}

EAPI Eina_Bool
e_mod_effect_init()
{
   if (!(_comp = e_comp_get(NULL)))
     return EINA_FALSE;

   _event_hdlrs =
      eina_list_append(_event_hdlrs,
                       ecore_event_handler_add(E_EVENT_CLIENT_STACK,
                                               _e_mod_effect_cb_client_restack,
                                               _comp));
   _event_hdlrs =
      eina_list_append(_event_hdlrs,
                       ecore_event_handler_add(E_EVENT_CLIENT_REMOVE,
                                               _e_mod_effect_cb_client_remove,
                                               _comp));
   _providers =
      eina_list_append(_providers,
                       e_comp_object_effect_mover_add(100,
                                                      "e,state,visible",
                                                      _e_mod_effect_cb_visible,
                                                      _comp));
   _providers =
      eina_list_append(_providers,
                       e_comp_object_effect_mover_add(100,
                                                      "e,state,hidden",
                                                      _e_mod_effect_cb_hidden,
                                                      _comp));
   _providers =
      eina_list_append(_providers,
                       e_comp_object_effect_mover_add(100,
                                                      "e,action,iconify",
                                                      _e_mod_effect_cb_hidden,
                                                      _comp));
   _providers =
      eina_list_append(_providers,
                       e_comp_object_effect_mover_add(100,
                                                      "e,action,uniconify",
                                                      _e_mod_effect_cb_visible,
                                                      _comp));
   _providers =
      eina_list_append(_providers,
                       e_comp_object_effect_mover_add(100,
                                                      "e,action,restack*",
                                                      _e_mod_effect_cb_restack,
                                                      _comp));
   return EINA_TRUE;
}

EAPI void
e_mod_effect_shutdown()
{
   E_Comp_Object_Mover *prov = NULL;
   Ecore_Event_Handler *hdl = NULL;

   EINA_LIST_FREE(_providers, prov)
      e_comp_object_effect_mover_del(prov);

   EINA_LIST_FREE(_event_hdlrs, hdl)
      ecore_event_handler_del(hdl);

   _comp = NULL;
}
