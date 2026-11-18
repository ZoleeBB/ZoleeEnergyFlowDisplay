#include "app_setup_ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned scans, joins;
static char joined[33], pass[65];

bool app_setup_wifi_scan(void)
{
    scans++;
    return true;
}

bool app_setup_wifi_connect(const char *ssid, const char *password)
{
    joins++;
    snprintf(joined,sizeof(joined),"%s",ssid);
    snprintf(pass,sizeof(pass),"%s",password);
    return true;
}

static lv_obj_t *find(lv_obj_t *root, const char *text, const lv_obj_class_t *cl)
{
    if (lv_obj_check_type(root,cl) && (!text || !strcmp(lv_label_get_text(root),text))) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *result = find(lv_obj_get_child(root,(int)i),text,cl);
        if (result) return result;
    }
    return NULL;
}

static void click_label(lv_obj_t *root, const char *name)
{
    lv_obj_t *lbl = find(root,name,&lv_label_class);
    assert(lbl);
    lv_obj_send_event(lv_obj_get_parent(lbl),LV_EVENT_CLICKED,NULL);
}

static void press(lv_obj_t *kb, const char *value)
{
    for (unsigned i = 0; i < 80; i++) {
        const char *key = lv_buttonmatrix_get_button_text(kb,i);
        if (!key) break;
        if (strcmp(key,value)) continue;
        lv_buttonmatrix_set_selected_button(kb,i);
        lv_obj_send_event(kb,LV_EVENT_VALUE_CHANGED,NULL);
        return;
    }
    assert(0 && "Hiányzó billentyű");
}

static void dimensions(int w,int h)
{
    lv_display_t *display = lv_display_create(w,h);
    lv_obj_t *parent = lv_screen_active();
    app_setup_wifi_state_t state = {.phase=APP_SETUP_SCANNING};
    assert(app_setup_ui_update(parent,w,h,&state));
    state.phase = APP_SETUP_SELECT;
    state.count = 2;
    strcpy(state.aps[0].ssid,"Vendég");
    strcpy(state.aps[1].ssid,"Otthon_ŐŰ");
    state.aps[1].secured = true;
    assert(app_setup_ui_update(parent,w,h,&state));
    click_label(parent,"Újrakeresés");
    click_label(parent,"Vendég");
    assert(scans && joins && !strcmp(joined,"Vendég") && !*pass);
    click_label(parent,"Otthon_ŐŰ");
    assert(app_setup_ui_update(parent,w,h,&state));
    lv_obj_t *kb = find(parent,NULL,&lv_buttonmatrix_class);
    assert(kb);
    press(kb,"ő");
    press(kb,"ű");
    press(kb,"Törlés");
    press(kb,"ABC");
    press(kb,"Ű");
    press(kb,"Á");
    press(kb,"123");
    press(kb,"&");
    press(kb,"'");
    press(kb,"1");
    press(kb,"Szóköz");
    assert(find(parent,"*******",&lv_label_class));
    assert(!find(parent,"őŰÁ&'1 ",&lv_label_class));
    click_label(parent,"Csatlakozás");
    assert(!strcmp(joined,"Otthon_ŐŰ") && !strcmp(pass,"őŰÁ&'1 "));
    state.phase = APP_SETUP_CONNECTING;
    assert(app_setup_ui_update(parent,w,h,&state));
    assert(!find(parent,NULL,&lv_buttonmatrix_class));
    state.phase = APP_SETUP_FAILED;
    assert(app_setup_ui_update(parent,w,h,&state));
    click_label(parent,"Otthon_ŐŰ");
    assert(app_setup_ui_update(parent,w,h,&state));
    assert(!find(parent,"*******",&lv_label_class));
    click_label(parent,"Vissza");
    state.phase = APP_SETUP_WAIT_TIME;
    assert(app_setup_ui_update(parent,w,h,&state));
    assert(find(parent,"Időszinkronizálás",&lv_label_class));
    state.phase = APP_SETUP_WEB;
    strcpy(state.ip,"192.0.2.47");
    assert(app_setup_ui_update(parent,w,h,&state));
    lv_obj_t *url = find(parent,"http://192.0.2.47",&lv_label_class);
    assert(url);
    assert(app_setup_ui_update(parent,w,h,&state));
    assert(url == find(parent,"http://192.0.2.47",&lv_label_class));
    lv_obj_clean(parent);
    assert(app_setup_ui_update(parent,w,h,&state));
    state.phase = APP_SETUP_DONE;
    assert(!app_setup_ui_update(parent,w,h,&state) && !lv_obj_get_child_count(parent));
    lv_display_delete(display);
}

int main(void)
{
    lv_init();
    dimensions(720,720);
    dimensions(1280,800);
    lv_deinit();
    puts("PASS: két panel első indítása, Wi-Fi kiválasztás, magyar/szimbólum billentyűk és valós IP");
    return 0;
}
