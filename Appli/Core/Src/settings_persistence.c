#include "settings_persistence.h"

void SettingsPersistence_Init(void)
{
    /* No external EEPROM or NOR flash on NUCLEO-H753ZI.
     * All persistence handled via internal Flash (ui_flash_storage). */
}
