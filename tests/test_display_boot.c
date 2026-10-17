#include <assert.h>
#include "app_display_boot.h"
int main(void) {
    app_display_boot_state_t s={.rotation_degrees=270};
    assert(app_display_boot_select(&s)==270);
    assert(app_display_boot_sync(&s,90));
    assert(s.rotation_degrees==90 && !s.trial && !s.blocked);
    assert(app_display_boot_select(&s)==90 && s.trial);
    assert(app_display_boot_select(&s)==270 && s.blocked && !s.trial);
    assert(app_display_boot_select(&s)==270 && s.blocked);
    assert(!app_display_boot_sync(&s,90));
    assert(app_display_boot_sync(&s,270));
    assert(!s.blocked && !s.trial);
    assert(app_display_boot_sync(&s,90));
    assert(app_display_boot_select(&s)==90);
    s.trial=0;
    assert(app_display_boot_select(&s)==90);
    return 0;
}
