#ifndef E_MOD_EFFECT_H
# define E_MOD_EFFECT_H

# include <e.h>
# include <tizen-extension-server-protocol.h>

#define EFFINF(f, cp, ec, x...) ELOGF("EFFECT", f, cp, ec, ##x)
#define EFFDBG(f, cp, ec, x...)                            \
   do                                                      \
     {                                                     \
        if ((!cp) && (!ec))                                \
          DBG("EWL|%20.20s|             |             |"f, \
              "EFFECT", ##x);                                   \
        else                                               \
          DBG("EWL|%20.20s|cp:0x%08x|ec:0x%08x|"f,         \
              "EFFECT",                                         \
              (unsigned int)(cp),                          \
              (unsigned int)(ec),                          \
              ##x);                                        \
     }                                                     \
   while (0)

typedef struct _E_Effect E_Effect;
typedef enum _E_Effect_Type E_Effect_Type;
typedef enum _E_Effect_Group E_Effect_Group;

enum _E_Effect_Type
{
   E_EFFECT_TYPE_SHOW,
   E_EFFECT_TYPE_HIDE,
   E_EFFECT_TYPE_ICONIFY,
   E_EFFECT_TYPE_UNICONIFY,
   E_EFFECT_TYPE_RESTACK_SHOW,
   E_EFFECT_TYPE_RESTACK_HIDE,
   E_EFFECT_TYPE_LAUNCH,
   E_EFFECT_TYPE_NONE,
};

enum _E_Effect_Group
{
   E_EFFECT_GROUP_NORMAL,
   E_EFFECT_GROUP_HOME,
   E_EFFECT_GROUP_LOCKSCREEN,
   E_EFFECT_GROUP_KEYBOARD,
   E_EFFECT_GROUP_NONE,
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
        Evas_Object *obj;
   }layers[E_CLIENT_LAYER_COUNT];

   struct {
      Eina_List *old;
      Eina_List *cur;
   } stack;

   struct {
      Edje_Signal_Cb cb;
      E_Client *ec;
      void *data;
      E_Effect_Type type;
   } next_done;

};

EAPI Eina_Bool e_mod_effect_init(void);
EAPI void e_mod_effect_shutdown(void);

#endif
