/* Postaláda-jelzés: Loxone módban egy párosított „Posta érkezett” állapot
 * 1 / true értékénél postaláda-jelvény jelenik meg a téma fejlécében.
 * A firmware-be ágyazott, natív méretű PNG-t (tools/prepare_mail_icon.py)
 * a meglévő képcache egyszer dekódolja; nincs SD-olvasás és skálázás. Az elem
 * egyszer jön létre, a 200 ms-os körben csak a láthatósága és helye változik,
 * azok is kizárólag tényleges változáskor. */
#define MAIL_BADGE_SIZE 52

static lv_obj_t *mail_badge;

static void mail_reset_widgets(void)
{
    mail_badge = NULL;
}

/* A fejlécben szabadon maradó, a téma óráját, időjárását, értékeit és az
 * energiaasszisztens területét nem takaró hely. Külső témánál a szerző
 * `mail_area` mezője dönt, ennek hiányában a felső középső sáv. */
static void mail_position(int *x, int *y)
{
    *x = (W - MAIL_BADGE_SIZE) / 2;
    *y = 6;
    if (!template_is_active())
    {
        /* Rendszer nézet: a dátum és az időjárásikon közötti rés. */
        *x = 808;
        *y = 15;
        return;
    }
    const cJSON *scene = cJSON_GetObjectItemCaseSensitive(template_view.document,
                                                        s_last_night ? "dark" : "light");
    const cJSON *area = cJSON_GetObjectItemCaseSensitive(scene, "mail_area");
    if (cJSON_IsObject(area))
    {
        const cJSON *ax = cJSON_GetObjectItemCaseSensitive(area, "x");
        const cJSON *ay = cJSON_GetObjectItemCaseSensitive(area, "y");
        if (cJSON_IsNumber(ax) && cJSON_IsNumber(ay) && isfinite(ax->valuedouble) &&
            isfinite(ay->valuedouble) && ax->valuedouble >= 0 && ay->valuedouble >= 0 &&
            ax->valuedouble + MAIL_BADGE_SIZE <= W && ay->valuedouble + MAIL_BADGE_SIZE <= H)
        {
            *x = (int)ax->valuedouble;
            *y = (int)ay->valuedouble;
        }
        return;
    }
    static const struct
    {
        const char *id;
        int x, y;
    } factory[] = {
        {"01-live-home", 826, 12},      {"02-energy-paths", 258, 22},
        {"03-instruments", 930, 22},    {"04-daily-balance", 930, 22},
        {"05-calm-home", 896, 34},      {"06-energy-orbit", 958, 13},
        {"07-energy-ledger", 258, 22},  {"08-statistics", 258, 26},
        {"09-solar-journey", 388, 20},  {"10-battery-diagnostics", 614, 28},
    };
    for (unsigned i = 0; i < sizeof(factory) / sizeof(factory[0]); ++i)
        if (!strcmp(template_view.id, factory[i].id))
        {
            *x = factory[i].x;
            *y = factory[i].y;
            return;
        }
}

static void mail_render(void)
{
    bool visible = s_cfg.data_recept_mode == APP_DATA_LOXONE && s_loxone_signals.mail.valid &&
                   s_loxone_signals.mail.active && s_view == VIEW_DASHBOARD &&
                   !home_is_open() && !s_pair_pad && !s_diagnostics_requested;
    if (!visible)
    {
        if (mail_badge && !lv_obj_has_flag(mail_badge, LV_OBJ_FLAG_HIDDEN))
            lv_obj_add_flag(mail_badge, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (!mail_badge)
    {
        mail_badge = lv_image_create(s_screen);
        lv_image_set_src(mail_badge, &app_img_mail_box);
        lv_obj_set_size(mail_badge, MAIL_BADGE_SIZE, MAIL_BADGE_SIZE);
        lv_obj_remove_flag(mail_badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(mail_badge, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_IGNORE_LAYOUT);
    }
    int x, y;
    mail_position(&x, &y);
    lv_obj_set_pos(mail_badge, x, y);
    /* Új témaépítés után is legfelül legyen, de csak ha valóban nem az. */
    if (lv_obj_get_index(mail_badge) != (int32_t)lv_obj_get_child_count(s_screen) - 1)
        lv_obj_move_foreground(mail_badge);
    if (lv_obj_has_flag(mail_badge, LV_OBJ_FLAG_HIDDEN))
        lv_obj_remove_flag(mail_badge, LV_OBJ_FLAG_HIDDEN);
}
