#ifndef E_MOD_EFFECT_H
#define E_MOD_EFFECT_H

typedef struct _E_Effect E_Effect;

struct _E_Effect
{
   E_Comp *comp;

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
