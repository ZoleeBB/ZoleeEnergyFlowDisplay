#include <assert.h>
#include "app_display_boot.h"
/* A tájolás az NVS-ből jön; próbaidőszak és letiltás nincs többé. */
int main(void) {
    app_display_boot_state_t s={.rotation_degrees=270};
    assert(app_display_boot_select(&s)==270);
    assert(app_display_boot_sync(&s,90));
    assert(s.rotation_degrees==90);
    /* Ismételt indulás 90 fokon marad, akkor sem esik vissza, ha közben
     * újraindítás szakítja meg a bootot. */
    assert(app_display_boot_select(&s)==90);
    assert(app_display_boot_select(&s)==90);
    assert(!app_display_boot_sync(&s,90));
    assert(app_display_boot_sync(&s,270));
    assert(app_display_boot_select(&s)==270);
    /* Érvénytelen tárolt érték biztonságos 270 fokot ad. */
    s.rotation_degrees=123;
    assert(app_display_boot_select(&s)==270);
    return 0;
}
