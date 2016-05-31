#ifndef E_MOD_EFFECT_H
# define E_MOD_EFFECT_H

# include "config.h"

# define E_COMP_WL
# include <tizen-extension-server-protocol.h>

# include <e.h>

typedef struct _E_Effect E_Effect;
typedef enum _E_Effect_Type E_Effect_Type;

enum _E_Effect_Type
{
   E_EFFECT_TYPE_SHOW,
   E_EFFECT_TYPE_HIDE,
   E_EFFECT_TYPE_ICONIFY,
   E_EFFECT_TYPE_UNICONIFY,
   E_EFFECT_TYPE_RESTACK_SHOW,
   E_EFFECT_TYPE_RESTACK_HIDE,
   E_EFFECT_TYPE_NONE,
};

struct _E_Effect
{
   struct wl_global *global;
   Eina_Hash *resources;
   const char *file;
   const char *style;

   Eina_List *providers;
   Eina_List *event_hdlrs;

   Eina_Hash *clients;

   struct {
      Eina_List *old;
      Eina_List *cur;
   } stack;

};

EAPI Eina_Bool e_mod_effect_init(void);
EAPI void e_mod_effect_shutdown(void);

#endif
