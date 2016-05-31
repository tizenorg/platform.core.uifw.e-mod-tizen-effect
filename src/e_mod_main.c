#include "e_mod_main.h"
#include "e_mod_effect.h"

EAPI E_Module_Api e_modapi = { E_MODULE_API_VERSION, "Effect Module" };

EAPI void *
e_modapi_init(E_Module *m)
{
   if (!e_mod_effect_init())
     return NULL;

   return m;
}
EAPI int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
   e_mod_effect_shutdown();

   return 1;
}

EAPI int
e_modapi_save(E_Module *m EINA_UNUSED)
{
   /* Save something to be kept */
   return 1;
}
