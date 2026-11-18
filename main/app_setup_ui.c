#include "app_setup_ui.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LV_FONT_DECLARE(app_font_20);
LV_FONT_DECLARE(app_font_28);
#if defined(APP_INTERCOM_SLAVE)
LV_FONT_DECLARE(app_font_34);
#endif

typedef struct {
    lv_obj_t *overlay, *panel, *keyboard, *mask, *hint;
    int width, height, content_width, mode;
    bool editing, dirty;
    char ssid[APP_WIFI_SSID_MAX], password[APP_WIFI_PASSWORD_MAX];
    app_setup_wifi_state_t last;
} setup_view_t;

static setup_view_t *view;
static const uint32_t ink = 0xf1f5f9, muted = 0xa9b7c4, cyan = 0x66d9e1;
static const char *const lower[] = {
    "q","w","e","r","t","z","u","i","o","p","ő","ú","\n",
    "a","s","d","f","g","h","j","k","l","é","á","ű","\n",
    "í","y","x","c","v","b","n","m","ö","ü","ó","\n",
    "ABC","123","Szóköz","Törlés",""
};
static const char *const upper[] = {
    "Q","W","E","R","T","Z","U","I","O","P","Ő","Ú","\n",
    "A","S","D","F","G","H","J","K","L","É","Á","Ű","\n",
    "Í","Y","X","C","V","B","N","M","Ö","Ü","Ó","\n",
    "abc","123","Szóköz","Törlés",""
};
static const char *const symbols[] = {
    "1","2","3","4","5","6","7","8","9","0","\n",
    "!","@","#","$","%","^","&","*","(",")","_","+","\n",
    "-","=","[","]","{","}","<",">","/","\\","|","~","\n",
    ".",",",";",":","'","\"","?","`","\n",
    "abc","ABC","Szóköz","Törlés",""
};

static void wipe(char *password)
{
    volatile char *p = password;
    for (size_t i = 0; i < APP_WIFI_PASSWORD_MAX; i++) p[i] = 0;
}

static lv_obj_t *label(lv_obj_t *parent, const char *s, int x, int y, int w,
                       const lv_font_t *font, uint32_t color, lv_text_align_t align)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(obj, align, 0);
    lv_obj_set_width(obj, w);
    lv_obj_set_pos(obj, x, y);
    lv_label_set_text(obj, s);
    return obj;
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *button(lv_obj_t *parent, const char *s, int x, int y, int w,
                        lv_event_cb_t cb, void *arg, bool primary)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, 52);
    lv_obj_set_style_radius(obj, 9, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(primary ? 0x176474 : 0x203342), 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_t *txt = label(obj,s,0,0,w-12,&app_font_20,ink,LV_TEXT_ALIGN_CENTER);
    lv_obj_center(txt);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, arg);
    return obj;
}

static void redraw_request(void)
{
    if (view) view->dirty = true;
}

static void cancel(lv_event_t *ev)
{
    (void)ev;
    wipe(view->password);
    view->editing = false;
    redraw_request();
}

static void scan(lv_event_t *ev)
{
    (void)ev;
    app_setup_wifi_scan();
}

static void choose(lv_event_t *ev)
{
    size_t i = (uintptr_t)lv_event_get_user_data(ev);
    if (i >= view->last.count) return;
    const app_setup_ap_t *ap = &view->last.aps[i];
    if (!ap->secured) {
        app_setup_wifi_connect(ap->ssid, "");
        return;
    }
    snprintf(view->ssid, sizeof(view->ssid), "%s", ap->ssid);
    wipe(view->password);
    view->editing = true;
    view->mode = 0;
    redraw_request();
}

static void join(lv_event_t *ev)
{
    (void)ev;
    if (!view->password[0]) {
        lv_label_set_text(view->hint, "Add meg a hálózat jelszavát.");
        return;
    }
    if (app_setup_wifi_connect(view->ssid, view->password)) {
        wipe(view->password);
        view->editing = false;
        redraw_request();
    } else {
        lv_label_set_text(view->hint, "A kapcsolódás nem indult el. Próbáld újra.");
    }
}

static void update_mask(void)
{
    char masked[APP_WIFI_PASSWORD_MAX];
    size_t count = 0;
    for (const unsigned char *p = (const unsigned char *)view->password; *p; p++)
        if ((*p & 0xc0) != 0x80) masked[count++] = '*';
    masked[count] = 0;
    lv_label_set_text(view->mask, masked);
}

static void key(lv_event_t *ev)
{
    lv_obj_t *kb = lv_event_get_target_obj(ev);
    uint32_t id = lv_buttonmatrix_get_selected_button(kb);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) return;
    const char *txt = lv_buttonmatrix_get_button_text(kb, id);
    if (!txt) return;
    if (!strcmp(txt, "abc") || !strcmp(txt, "ABC") || !strcmp(txt, "123")) {
        view->mode = !strcmp(txt, "abc") ? 0 : !strcmp(txt, "ABC") ? 1 : 2;
        lv_buttonmatrix_set_map(kb, view->mode == 0 ? lower : view->mode == 1 ? upper : symbols);
        return;
    }
    char *buf = view->password;
    size_t n = strlen(buf);
    if (!strcmp(txt, "Törlés")) {
        if (n) {
            size_t start = n - 1;
            while (start > 0 && ((unsigned char)buf[start] & 0xc0) == 0x80) start--;
            memset(buf + start, 0, n - start);
        }
    } else {
        if (!strcmp(txt, "Szóköz")) txt = " ";
        size_t extra = strlen(txt);
        if (n + extra >= sizeof(view->password)) {
            lv_label_set_text(view->hint, "A jelszó legfeljebb 64 bájt lehet.");
            return;
        }
        memcpy(buf + n, txt, extra + 1);
    }
    update_mask();
    lv_label_set_text(view->hint, "");
}

static void form(setup_view_t *ui)
{
    int w = ui->content_width, h = ui->height;
    label(ui->panel,"Wi-Fi-jelszó",0,34,w,&app_font_28,ink,LV_TEXT_ALIGN_LEFT);
    label(ui->panel,ui->ssid,0,88,w,&app_font_20,cyan,LV_TEXT_ALIGN_LEFT);
    lv_obj_t *entry = box(ui->panel,0,138,w,55,0x17242f);
    lv_obj_set_style_radius(entry,8,0);
    ui->mask = label(entry,"",16,10,w-32,&app_font_20,ink,LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(ui->mask,LV_LABEL_LONG_MODE_CLIP);
    update_mask();
    ui->hint = label(ui->panel,"",0,200,w,&app_font_20,muted,LV_TEXT_ALIGN_LEFT);
    int kb_height = h > 760 ? 340 : 300, kb_top = h - kb_height - 105;
    ui->keyboard = lv_buttonmatrix_create(ui->panel);
    lv_obj_set_pos(ui->keyboard,0,kb_top);
    lv_obj_set_size(ui->keyboard,w,kb_height);
    lv_obj_set_style_bg_opa(ui->keyboard,LV_OPA_TRANSP,0);
    lv_obj_set_style_pad_all(ui->keyboard,2,0);
    lv_obj_set_style_pad_column(ui->keyboard,4,0);
    lv_obj_set_style_pad_row(ui->keyboard,5,0);
    lv_obj_set_style_border_width(ui->keyboard,0,0);
    lv_obj_set_style_text_font(ui->keyboard,&app_font_20,LV_PART_ITEMS);
    lv_obj_set_style_text_color(ui->keyboard,lv_color_hex(ink),LV_PART_ITEMS);
    lv_obj_set_style_bg_color(ui->keyboard,lv_color_hex(0x223442),LV_PART_ITEMS);
    lv_obj_set_style_bg_color(ui->keyboard,lv_color_hex(0x246477),LV_PART_ITEMS|LV_STATE_PRESSED);
    lv_obj_set_style_border_width(ui->keyboard,0,LV_PART_ITEMS);
    lv_buttonmatrix_set_map(ui->keyboard,ui->mode == 0 ? lower : ui->mode == 1 ? upper : symbols);
    lv_obj_add_event_cb(ui->keyboard,key,LV_EVENT_VALUE_CHANGED,NULL);
    button(ui->panel,"Vissza",0,h-78,136,cancel,NULL,false);
    button(ui->panel,"Csatlakozás",w-176,h-78,176,join,NULL,true);
}

static void networks(setup_view_t *ui, const app_setup_wifi_state_t *s)
{
    int w = ui->content_width;
    label(ui->panel,"Wi-Fi-hálózat kiválasztása",0,36,w,&app_font_28,ink,LV_TEXT_ALIGN_LEFT);
    const char *hint = s->phase == APP_SETUP_FAILED ?
        "Nem sikerült a hálózati művelet. Válassz hálózatot vagy keress újra." :
        s->count ? "Válaszd ki a kijelző helyi hálózatát." : "Nem látható Wi-Fi-hálózat.";
    label(ui->panel,hint,0,92,w,&app_font_20,muted,LV_TEXT_ALIGN_LEFT);
    lv_obj_t *list = box(ui->panel,0,180,w,ui->height-280,0);
    lv_obj_add_flag(list,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list,LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list,LV_SCROLLBAR_MODE_AUTO);
    for (unsigned i = 0; i < s->count && i < APP_SETUP_WIFI_MAX_APS; i++) {
        lv_obj_t *row = box(list,0,(int)i*65,w-12,57,0x17242f);
        lv_obj_set_style_radius(row,8,0);
        lv_obj_add_flag(row,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row,choose,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
        lv_obj_t *name = label(row,s->aps[i].ssid,16,13,w-166,&app_font_20,ink,LV_TEXT_ALIGN_LEFT);
        lv_label_set_long_mode(name,LV_LABEL_LONG_MODE_DOTS);
        label(row,s->aps[i].secured ? "Jelszavas" : "Nyílt",w-156,13,122,&app_font_20,muted,LV_TEXT_ALIGN_RIGHT);
    }
    button(ui->panel,"Újrakeresés",0,ui->height-78,176,scan,NULL,false);
}

/* Ugyanaz a méretfüggetlen, vonalas beállításikon mindkét panelen. */
static void gear(lv_obj_t *panel, int x, int y)
{
    static const lv_point_precise_t outline[] = {
        {32,3},{44,3},{46,11},{53,14},{61,10},{69,18},{65,26},{68,33},
        {76,35},{76,47},{68,49},{65,56},{69,64},{61,72},{53,68},{46,71},
        {44,79},{32,79},{30,71},{23,68},{15,72},{7,64},{11,56},{8,49},
        {0,47},{0,35},{8,33},{11,26},{7,18},{15,10},{23,14},{30,11},{32,3}
    };
    lv_obj_t *line = lv_line_create(panel);
    lv_line_set_points(line,outline,sizeof(outline)/sizeof(outline[0]));
    lv_obj_set_style_line_width(line,4,0);
    lv_obj_set_style_line_color(line,lv_color_hex(cyan),0);
    lv_obj_set_pos(line,x,y);
    lv_obj_t *ring = box(panel,x+24,y+27,28,28,0);
    lv_obj_set_style_radius(ring,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_border_color(ring,lv_color_hex(cyan),0);
    lv_obj_set_style_border_width(ring,4,0);
}

static void progress(setup_view_t *ui, const app_setup_wifi_state_t *s)
{
    int w = ui->content_width, center = ui->height / 2;
    const char *title = "Wi-Fi-hálózatok keresése...";
    const char *detail = "";
    if (s->phase == APP_SETUP_CONNECTING) {
        title = "Kapcsolódás a Wi-Fi-hálózathoz";
        detail = s->selected_ssid;
    } else if (s->phase == APP_SETUP_WAIT_TIME) {
        title = "Időszinkronizálás";
        detail = "A kijelző a pontos időre vár.";
    } else if (s->phase == APP_SETUP_WEB) {
        title = "A kijelző beállítása szükséges";
        detail = "Nyisd meg ezt a címet egy ugyanarra a hálózatra csatlakozó eszköz böngészőjében.";
    }
    gear(ui->panel,w/2-38,center-156);
    label(ui->panel,title,0,center-47,w,&app_font_28,ink,LV_TEXT_ALIGN_CENTER);
    label(ui->panel,detail,0,center+9,w,&app_font_20,muted,LV_TEXT_ALIGN_CENTER);
    if (s->phase == APP_SETUP_WEB) {
        char link[40];
        if (s->ip[0] && strcmp(s->ip,"0.0.0.0")) snprintf(link,sizeof(link),"http://%s",s->ip);
        else snprintf(link,sizeof(link),"IP-címre vár...");
        const lv_font_t *font = &app_font_28;
#if LV_FONT_MONTSERRAT_48
        if (s->ip[0]) font = &lv_font_montserrat_48;
#elif defined(APP_INTERCOM_SLAVE)
        font = &app_font_34;
#endif
        label(ui->panel,link,0,center+90,w,font,cyan,LV_TEXT_ALIGN_CENTER);
    }
}

static void deleted(lv_event_t *ev)
{
    setup_view_t *ui = lv_event_get_user_data(ev);
    if (lv_event_get_target_obj(ev) != ui->overlay) return;
    wipe(ui->password);
    if (view == ui) view = NULL;
    free(ui);
}

void app_setup_ui_close(void)
{
    if (view) lv_obj_delete(view->overlay);
}

bool app_setup_ui_update(lv_obj_t *parent, int width, int height, const app_setup_wifi_state_t *s)
{
    if (!parent || !s || s->phase == APP_SETUP_DONE) {
        app_setup_ui_close();
        return false;
    }
    if (view && (lv_obj_get_parent(view->overlay) != parent || view->width != width || view->height != height))
        app_setup_ui_close();
    if (!view) {
        view = calloc(1,sizeof(*view));
        if (!view) return false;
        view->width = width;
        view->height = height;
        view->content_width = width > 1000 ? 884 : width - 52;
        view->overlay = box(parent,0,0,width,height,0);
        lv_obj_add_flag(view->overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(view->overlay,deleted,LV_EVENT_DELETE,view);
        view->panel = box(view->overlay,(width-view->content_width)/2,0,view->content_width,height,0);
        view->dirty = true;
    }
    if (!view->dirty && !memcmp(&view->last,s,sizeof(*s))) return true;
    if (s->phase != APP_SETUP_SELECT && s->phase != APP_SETUP_FAILED) {
        wipe(view->password);
        view->editing = false;
    }
    view->last = *s;
    view->dirty = false;
    lv_obj_clean(view->panel);
    view->keyboard = view->mask = view->hint = NULL;
    if (view->editing) form(view);
    else if (s->phase == APP_SETUP_SELECT || s->phase == APP_SETUP_FAILED) networks(view,s);
    else progress(view,s);
    return true;
}
