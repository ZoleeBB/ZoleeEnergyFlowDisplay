/* A valódi LVGL Audio-kártyát és modalt vizsgáljuk explicit helyi adatokkal.
 * Ez nem állít élő Audioserver-elérést; annak protokolltesztje külön fut. */
#include "../../simulator/audio_fixture.h"
#include "src/misc/lv_text_private.h"

static void audio_test_tick(const app_energy_sample_t *sample)
{
    assert(home_prepare(home_night));
    assert(home_render(sample, true));
    lv_obj_update_layout(s_screen);
    lv_refr_now(display);
}

static void audio_test_idle(const app_energy_sample_t *sample)
{
    audio_test_tick(sample);
    unsigned invalidations = 0;
    lv_display_add_event_cb(display, count_invalidations, LV_EVENT_INVALIDATE_AREA, &invalidations);
    for (unsigned i = 0; i < 4; ++i)
        audio_test_tick(sample);
    assert(invalidations == 0);
    lv_display_remove_event_cb_with_user_data(display, count_invalidations, &invalidations);
}

static void audio_test_configure(unsigned count)
{
    app_sim_audio_reset();
    app_sim_home_config.page_count = 1;
    app_sim_home_config.card_count = app_sim_home_config.pages[0].card_count = count;
    snprintf(app_sim_home_config.pages[0].title, sizeof(app_sim_home_config.pages[0].title), "%s", "Zene az otthonban");
    const char *names[] = {"Nappali", "Konyha", "Fürdőszoba", "Dolgozószoba"};
    for (unsigned i = 0; i < count; ++i)
    {
        lx_home_card_t *card = &app_sim_home_config.cards[i];
        card->type = LX_HOME_CARD_AUDIO;
        snprintf(card->id, sizeof(card->id), "audio-%u", i);
        snprintf(card->name, sizeof(card->name), "%s", names[i % 4]);
        /* Importált téves ikonmező mellett is a kötött Audio-ikon érvényes. */
        snprintf(card->icon, sizeof(card->icon), "%s", "does-not-exist");
        lx_audio_summary_t summary = {.available = true, .controllable = true, .native_ready = true,
            .generation = 42, .volume_max = 85, .status = LX_AUDIO_READY};
        summary.track.mode = i % 2 ? LX_AUDIO_MODE_PAUSE : LX_AUDIO_MODE_PLAY;
        summary.track.volume_known = true;
        summary.track.volume = 36;
        snprintf(summary.track.title, sizeof(summary.track.title), "%s",
            i ? "Egy hosszú zeneszámcím, amely a kártya kijelzőjén rövidítve jelenik meg" : "Esti dallamok");
        snprintf(summary.track.artist, sizeof(summary.track.artist), "%s", "Helyi tesztelőadó");
        snprintf(summary.track.album, sizeof(summary.track.album), "%s", "Bemutató album");
        assert(app_sim_audio_configure(card->id, 1, &summary));
    }
    app_sim_audio_set_favorite_count(70);
}

/* A widget tényleges betűkészletét kérdezzük le minden látható karakterhez.
 * Így az ASCII és a magyar ékezetek mellett a kapcsolódás/pending feliratok
 * és a külön szimbólumfontot használó gombok is fedettek. */
static void audio_test_glyphs(lv_obj_t *object)
{
    if (!object || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN))
        return;
    if (lv_obj_check_type(object, &lv_label_class))
    {
        const char *text = lv_label_get_text(object);
        const lv_font_t *font = lv_obj_get_style_text_font(object, 0);
        uint32_t offset = 0;
        while (text[offset])
        {
            uint32_t codepoint = lv_text_encoded_next(text, &offset);
            if (codepoint == '\n' || codepoint == '\r')
                continue;
            lv_font_glyph_dsc_t glyph;
            bool found = lv_font_get_glyph_dsc(font, &glyph, codepoint, 0);
            if (!found || glyph.is_placeholder)
                fprintf(stderr, "Audio label missing U+%04X: %s\n", (unsigned)codepoint, text);
            assert(found && !glyph.is_placeholder);
        }
    }
    for (unsigned i = 0; i < lv_obj_get_child_count(object); ++i)
        audio_test_glyphs(lv_obj_get_child(object, (int)i));
}

static void audio_test_state_labels(const app_energy_sample_t *sample)
{
    lx_audio_summary_t saved, summary;
    assert(app_loxone_audio_summary(home_config.cards[0].id, home_revision, &saved));
    char language[sizeof(s_cfg.project.language)];
    snprintf(language, sizeof(language), "%s", s_cfg.project.language);
    for (unsigned english = 0; english < 2; ++english)
    {
        audio_ui_reset();
        snprintf(s_cfg.project.language, sizeof(s_cfg.project.language), "%s", english ? "en" : "hu");
        summary = saved;
        summary.track.title[0] = summary.track.artist[0] = summary.track.album[0] = summary.track.station[0] = 0;
        summary.track.mode = LX_AUDIO_MODE_PLAY;
        summary.native_ready = false;
        summary.status = LX_AUDIO_CONNECTING;
        summary.volume_pending = false;
        assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &summary));
        audio_ui_open(0, true);
        app_sim_audio_set_favorite_state(false, false);
        audio_test_tick(sample);
        assert(!strcmp(lv_label_get_text(audio_track_title), tr("Zeneszámadat nem érhető el", "Track information unavailable")));
        assert(!strcmp(lv_label_get_text(audio_track_source), ""));
        assert(!strcmp(lv_label_get_text(audio_status), tr("Audio kapcsolódás...", "Connecting audio...")));
        assert(!strcmp(lv_label_get_text(audio_favorites_label), lv_label_get_text(audio_status)));
        assert(lv_obj_has_state(audio_favorites_next, LV_STATE_DISABLED));
        audio_test_glyphs(audio_panel);
        audio_test_glyphs(lv_obj_get_parent(audio_cards[0].play));
        audio_test_idle(sample);

        /* A natív kapcsolat állapotát a Miniserver felé függő hangerő sem fedi el. */
        summary.volume_pending = true;
        summary.desired_volume = 63;
        assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &summary));
        audio_test_tick(sample);
        assert(!strcmp(lv_label_get_text(audio_status), tr("Audio kapcsolódás...", "Connecting audio...")));
        assert(!strcmp(lv_label_get_text(audio_favorites_label), lv_label_get_text(audio_status)));
        summary.native_ready = true;
        summary.status = LX_AUDIO_READY;
        assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &summary));
        audio_test_tick(sample);
        assert(!strcmp(lv_label_get_text(audio_status), tr("Hangerő küldése...", "Setting volume...")));
        assert(!strcmp(lv_label_get_text(audio_favorites_label), tr("Kedvencek betöltése...", "Loading favorites...")));
        audio_test_glyphs(audio_panel);
        audio_test_glyphs(lv_obj_get_parent(audio_cards[0].play));
        audio_test_idle(sample);
        if (!english)
            save_frame("audio-detail-pending");

        /* A külső hangerő gombjai is a meglévő fontot használják. */
        summary.volume_pending = false;
        summary.volume_up_down = true;
        assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &summary));
        audio_test_tick(sample);
        assert(lv_obj_has_flag(audio_volume_slider, LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(audio_volume_buttons[0], LV_OBJ_FLAG_HIDDEN));
        assert(!strcmp(lv_label_get_text(lv_obj_get_child(audio_volume_buttons[0], 0)), "-"));
        audio_test_glyphs(audio_panel);
        audio_test_idle(sample);
        summary.volume_up_down = false;

        /* A rádióállomás címként használható, a forrássor nem ismétli meg. */
        snprintf(summary.track.station, sizeof(summary.track.station), "%s", "95.8 Sláger FM");
        assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &summary));
        audio_test_tick(sample);
        assert(!strcmp(lv_label_get_text(audio_track_title), "95.8 Sláger FM"));
        assert(!strcmp(lv_label_get_text(audio_track_source), ""));
        snprintf(summary.track.title, sizeof(summary.track.title), "%s", "Visszatért cím");
        assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &summary));
        audio_test_tick(sample);
        assert(!strcmp(lv_label_get_text(audio_track_title), "Visszatért cím"));
        assert(!strcmp(lv_label_get_text(audio_track_source), "95.8 Sláger FM"));

        /* Régi PLAY állapot mellett a kapcsolat és a belépés hibája látszik.
         * A Miniserver-gombok működhetnek, a natív kedvencek nem. */
        summary.native_ready = false;
        summary.status = LX_AUDIO_UNAVAILABLE;
        assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &summary));
        audio_submit_error = true;
        audio_test_tick(sample);
        assert(!strcmp(lv_label_get_text(audio_status), tr("Audio-kapcsolat nem érhető el", "Audio connection unavailable")));
        assert(!strcmp(lv_label_get_text(audio_favorites_label), lv_label_get_text(audio_status)));
        assert(!lv_obj_has_state(audio_transport_buttons[1], LV_STATE_DISABLED));
        assert(lv_obj_has_state(audio_favorites_next, LV_STATE_DISABLED));
        audio_test_glyphs(audio_panel);
        audio_test_idle(sample);
        summary.status = LX_AUDIO_AUTH_FAILED;
        summary.controllable = false;
        assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &summary));
        audio_test_tick(sample);
        assert(!strcmp(lv_label_get_text(audio_status), tr("Audio: sikertelen belépés", "Audio authentication failed")));
        assert(lv_obj_has_state(audio_transport_buttons[1], LV_STATE_DISABLED));
        audio_test_glyphs(audio_panel);
        audio_test_idle(sample);
        if (!english)
            save_frame("audio-detail-unavailable");

        summary.native_ready = summary.controllable = true;
        summary.status = LX_AUDIO_READY;
        audio_submit_error = false;
        assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &summary));
        app_sim_audio_set_favorite_state(false, true);
        audio_test_tick(sample);
        assert(!strcmp(lv_label_get_text(audio_favorites_label), tr("A kedvenclista nem érhető el.", "Favorites unavailable.")));
        audio_test_glyphs(audio_panel);
        app_sim_audio_set_favorite_state(true, false);
        audio_test_tick(sample);
        assert(!strcmp(lv_label_get_text(audio_status), tr("Lejátszás", "Playing")));
        assert(audio_favorites_ids[0] == 1000 && !lv_obj_has_state(audio_favorites_rows[0], LV_STATE_DISABLED));
        assert(!lv_obj_has_state(audio_favorites_next, LV_STATE_DISABLED));
        audio_test_glyphs(audio_panel);
        audio_test_idle(sample);
    }
    audio_ui_reset();
    snprintf(s_cfg.project.language, sizeof(s_cfg.project.language), "%s", language);
    assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &saved));
    audio_ui_open(0, true);
    puts("PASS: actual Audio label glyphs in Hungarian/English, missing track metadata, pending/offline/auth/recovery states and zero idle invalidations");
}

static void audio_test_volume_feedback(const app_energy_sample_t *sample)
{
    lx_audio_summary_t saved, summary, actual;
    assert(app_loxone_audio_summary(home_config.cards[0].id, home_revision, &saved));
    audio_ui_reset();
    summary = saved;
    summary.track.volume = 6;
    summary.track.volume_known = true;
    summary.volume_pending = summary.volume_failed = false;
    assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &summary));
    audio_ui_open(0, false);
    assert(lv_slider_get_value(audio_volume_slider) == 6);
    lv_obj_send_event(audio_volume_slider, LV_EVENT_PRESSED, NULL);
    lv_slider_set_value(audio_volume_slider, 63, LV_ANIM_OFF);
    lv_obj_send_event(audio_volume_slider, LV_EVENT_VALUE_CHANGED, NULL);
    summary.volume_pending = true;
    summary.desired_volume = 63;
    assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &summary));
    lv_obj_send_event(audio_volume_slider, LV_EVENT_RELEASED, NULL);
    assert(!audio_dragging && lv_slider_get_value(audio_volume_slider) == 63);
    assert(!strcmp(lv_label_get_text(audio_volume_label), "Célhangerő: 63%"));
    /* A helyi parancsfixture gyors visszajelzése helyett most explicit a még
     * régi szerverértéket adjuk vissza: a csúszka nem ugorhat 6%-ra. */
    assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &summary));
    audio_test_tick(sample);
    assert(lv_slider_get_value(audio_volume_slider) == 63);
    assert(app_loxone_audio_summary(home_config.cards[0].id, home_revision, &actual));
    assert(actual.track.volume == 6 && actual.desired_volume == 63 && actual.volume_pending);
    assert(!strcmp(lv_label_get_text(audio_volume_label), "Célhangerő: 63%"));
    audio_test_glyphs(audio_panel);
    audio_test_idle(sample);
    save_frame("audio-volume-pending");
    /* Egy korábbi köztes állapot sem helyettesítheti az újabb célt. */
    summary.track.volume = 31;
    assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &summary));
    audio_test_tick(sample);
    assert(lv_slider_get_value(audio_volume_slider) == 63);
    summary.track.volume = 63;
    summary.volume_pending = false;
    assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &summary));
    audio_test_tick(sample);
    assert(lv_slider_get_value(audio_volume_slider) == 63);
    assert(!strcmp(lv_label_get_text(audio_volume_label), "Hangerő: 63%"));

    summary.desired_volume = 22;
    summary.volume_pending = true;
    assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &summary));
    audio_test_tick(sample);
    assert(lv_slider_get_value(audio_volume_slider) == 22);
    assert(!strcmp(lv_label_get_text(audio_volume_label), "Célhangerő: 22%"));
    summary.volume_pending = false;
    summary.volume_failed = true;
    assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &summary));
    audio_test_tick(sample);
    assert(lv_slider_get_value(audio_volume_slider) == 63);
    assert(!strcmp(lv_label_get_text(audio_volume_label), "Hangerő: 63%"));
    assert(!strcmp(lv_label_get_text(audio_status), "Hangerőállítás sikertelen"));
    audio_test_glyphs(audio_panel);
    audio_test_idle(sample);
    /* Adat/maximum hiányánál a tartalék 0..1 tartomány stabil, tiltott. */
    summary.volume_failed = false;
    summary.track.volume_known = false;
    summary.volume_max = 0;
    assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &summary));
    audio_test_tick(sample);
    assert(lv_obj_has_state(audio_volume_slider, LV_STATE_DISABLED));
    assert(!strcmp(lv_label_get_text(audio_volume_label), "Hangerő: nincs adat"));
    audio_test_idle(sample);
    audio_ui_reset();
    assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &saved));
    audio_ui_open(0, true);
    puts("PASS: Audio RELEASED keeps the pending target, confirmed volume stays independent, failure restores actual value, zero idle invalidations");
}

static void audio_test_user_tracks(const app_energy_sample_t *sample)
{
    lx_audio_summary_t saved, summary;
    assert(app_loxone_audio_summary(home_config.cards[0].id, home_revision, &saved));
    /* Felhasználói eseményből származó adatok. Csak a UI átadását vizsgáljuk,
     * a JPEG letöltése/dekódolása külön, a valódi médiakóddal tesztelendő. */
    const struct
    {
        const char *title, *artist, *album, *station, *cover;
        unsigned volume;
    } tracks[] = {
        {"", "", "", "95.8 Sláger FM",
         "https://cdn-profiles.tunein.com/s25966/images/logoq.jpg?t=637069206200000000", 6},
        {"Here Comes the Hotstepper - Heartical Mix", "iNi Kamoze", "Here Comes The Hotstepper", "",
         "https://i.scdn.co/image/ab67616d0000b2736ffa23f19d8c125204e6220c", 12},
    };
    for (unsigned i = 0; i < sizeof(tracks) / sizeof(tracks[0]); ++i)
    {
        summary = saved;
        summary.track.mode = LX_AUDIO_MODE_STOP;
        summary.track.volume = tracks[i].volume;
        summary.track.volume_known = true;
        summary.native_ready = true;
        summary.status = LX_AUDIO_READY;
        summary.volume_pending = summary.volume_failed = false;
        snprintf(summary.track.title, sizeof(summary.track.title), "%s", tracks[i].title);
        snprintf(summary.track.artist, sizeof(summary.track.artist), "%s", tracks[i].artist);
        snprintf(summary.track.album, sizeof(summary.track.album), "%s", tracks[i].album);
        snprintf(summary.track.station, sizeof(summary.track.station), "%s", tracks[i].station);
        snprintf(summary.track.cover, sizeof(summary.track.cover), "%s", tracks[i].cover);
        summary.track.version = 17 + i;
        assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &summary));
        app_sim_audio_media_request_reset();
        audio_test_tick(sample);
        const char *title = i ? tracks[i].title : tracks[i].station;
        assert(!strcmp(lv_label_get_text(audio_track_title), title));
        assert(!strcmp(audio_cards[0].title_cache, title));
        assert(!strcmp(lv_label_get_text(audio_track_artist), tracks[i].artist));
        assert(!strcmp(lv_label_get_text(audio_track_source), tracks[i].album));
        assert(!strcmp(lv_label_get_text(audio_status), "Leállítva"));
        for (unsigned detail = 0; detail < 2; ++detail)
        {
            app_sim_audio_media_request_t request;
            assert(app_sim_audio_media_request_get(detail ? LX_AUDIO_MEDIA_SLOTS - 1 : 0, &request));
            assert(!strcmp(request.url, tracks[i].cover));
            assert(request.generation == summary.generation && request.version == summary.track.version);
            assert(request.size == (detail ? 256U : 112U));
        }
        audio_test_glyphs(audio_panel);
        audio_test_glyphs(lv_obj_get_parent(audio_cards[0].play));
        audio_test_idle(sample);
        save_frame(i ? "audio-detail-user-spotify" : "audio-detail-user-radio");

        /* Azonos URL új verziója és az üres URL is továbbjut a médiaréteghez. */
        for (unsigned clear = 0; clear < 2; ++clear)
        {
            ++summary.track.version;
            if (clear)
                summary.track.cover[0] = 0;
            assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &summary));
            app_sim_audio_media_request_reset();
            audio_test_tick(sample);
            for (unsigned detail = 0; detail < 2; ++detail)
            {
                app_sim_audio_media_request_t request;
                assert(app_sim_audio_media_request_get(detail ? LX_AUDIO_MEDIA_SLOTS - 1 : 0, &request));
                assert(!strcmp(request.url, summary.track.cover) && request.version == summary.track.version);
            }
        }
    }
    assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &saved));
    audio_test_tick(sample);
    puts("PASS: user radio/Spotify titles, source fallback and exact cover URL/version/generation forwarded to card/detail media slots");
}

/* Valódi RGB565-borítóval is megmarad a kijelzett kép tulajdonjoga a
 * kapcsolatvesztés alatt; a HTTP/worker tesztje külön fut. */
static void audio_test_attach_cover(audio_ui_picture_t *picture)
{
    int icon = home_icon_index(LX_AUDIO_ICON_ID);
    assert(icon >= 0 && picture->object && !picture->owned);
    const app_template_asset_t *asset = &home_assets.assets[icon];
    lx_audio_image_t *image = lx_audio_image_decode(asset->bytes, asset->size, picture->size);
    assert(image && image->pixels);
    picture->owned = image;
    picture->descriptor = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
            .w = image->width, .h = image->height, .stride = image->width * 2},
        .data_size = image->width * image->height * 2, .data = (const uint8_t *)image->pixels,
    };
    lv_image_set_src(picture->object, &picture->descriptor);
    lv_refr_now(display);
}

static void audio_test_disposed_cover(lv_event_t *event)
{
    /* A tényleges kártyatörlés előtt már nincs a felszabadított képre mutató forrás. */
    assert(lv_image_get_src(lv_event_get_current_target(event)) == NULL);
    ++*(unsigned *)lv_event_get_user_data(event);
}

static void audio_test_disconnect(const app_energy_sample_t *sample)
{
    lx_audio_summary_t saved;
    assert(app_loxone_audio_summary(home_config.cards[0].id, home_revision, &saved));
    lv_obj_t *play = audio_cards[0].play, *screen = audio_cards[0].screen;
    lv_obj_t *volume = audio_cards[0].volume, *cover = audio_cards[0].cover.object;
    audio_test_attach_cover(&audio_cards[0].cover);
    lx_audio_image_t *owned = audio_cards[0].cover.owned;
    audio_ui_open(0, false);
    assert(audio_overlay);
    unsigned cancelled = app_sim_audio_volume_cancellations;
    app_sim_home_connected = false;

    /* A firmware runtime a grace-kezelés előtt ezt a kaput hívja. A régi
     * kód lenullázta a widgeteket, majd a következő home_render az LVGL
     * NULL assertjében megállt, miközben a webkiszolgáló tovább működött. */
    home_controls_suspend();
    assert(s_view == VIEW_HOME && home_ready_to_draw && !audio_overlay);
    assert(app_sim_audio_volume_cancellations == cancelled + 1);
    assert(audio_cards[0].play == play && audio_cards[0].screen == screen);
    assert(audio_cards[0].volume == volume && audio_cards[0].cover.object == cover);
    assert(audio_cards[0].cover.owned == owned && lv_image_get_src(cover) == &audio_cards[0].cover.descriptor);
    assert(owned->pixels && owned->width);
    audio_test_tick(sample);
    assert(lv_obj_has_state(play, LV_STATE_DISABLED) && lv_obj_has_state(volume, LV_STATE_DISABLED));
    unsigned commands = app_sim_audio_commands;
    lv_obj_send_event(play, LV_EVENT_CLICKED, NULL);
    lv_obj_send_event(screen, LV_EVENT_CLICKED, NULL);
    assert(app_sim_audio_commands == commands && !audio_overlay);

    unsigned invalidations = 0;
    lv_display_add_event_cb(display, count_invalidations, LV_EVENT_INVALIDATE_AREA, &invalidations);
    for (unsigned i = 0; i < 4; ++i)
    {
        home_controls_suspend();
        audio_test_tick(sample);
    }
    assert(invalidations == 0);
    lv_display_remove_event_cb_with_user_data(display, count_invalidations, &invalidations);

    app_sim_home_connected = true;
    audio_test_tick(sample);
    assert(audio_cards[0].play == play && audio_cards[0].screen == screen);
    assert(!lv_obj_has_state(play, LV_STATE_DISABLED) && !lv_obj_has_state(volume, LV_STATE_DISABLED));
    lv_obj_send_event(play, LV_EVENT_CLICKED, NULL);
    assert(app_sim_audio_commands == commands + 1);
    lv_obj_send_event(screen, LV_EVENT_CLICKED, NULL);
    assert(audio_overlay && audio_detail && audio_favorites_ids[0] == 1000);
    lv_obj_send_event(audio_overlay, LV_EVENT_CLICKED, NULL);
    assert(!audio_overlay && audio_cards[0].play == play && audio_cards[0].cover.object == cover);
    assert(app_sim_audio_update(home_config.cards[0].id, home_revision, &saved));
    audio_test_idle(sample);
    puts("PASS: actual disconnect suspend, retained card/cover ownership, offline input guard, reconnect and zero idle invalidations");
}

static void audio_test_page_dispose(app_runtime_state_t runtime, app_energy_sample_t sample)
{
    audio_test_configure(4);
    app_sim_home_config.page_count = 2;
    app_sim_home_config.pages[0].card_count = 2;
    app_sim_home_config.pages[1] = (lx_home_page_t){.first_card = 2, .card_count = 2};
    snprintf(app_sim_home_config.pages[1].id, sizeof(app_sim_home_config.pages[1].id), "%s", "audio-upper");
    snprintf(app_sim_home_config.pages[1].title, sizeof(app_sim_home_config.pages[1].title), "%s", "Emeleti Audio");
    home_request_set(2);
    irc_test_open(false, runtime, sample);
    assert(home_first_card == 0 && home_card_count == 2);
    audio_test_attach_cover(&audio_cards[0].cover);
    unsigned disposed = 0;
    lv_obj_add_event_cb(audio_cards[0].cover.object, audio_test_disposed_cover, LV_EVENT_DELETE, &disposed);
    home_request_set(3);
    irc_test_open(false, runtime, sample);
    assert(disposed == 1 && home_first_card == 2 && home_card_count == 2);
    assert(audio_cards[0].play && audio_cards[0].cover.object);
    assert(!audio_cards[2].play && !audio_cards[3].play);
    audio_ui_open(2, true);
    assert(audio_overlay && !strcmp(audio_card_id, "audio-2"));
    audio_ui_reset();
    audio_test_idle(&sample);
    home_request_set(0);
    assert(home_prepare(false));
    for (unsigned i = 0; i < LX_HOME_MAX_CARDS; ++i)
        assert(!audio_cards[i].play && !audio_cards[i].cover.object && !audio_cards[i].cover.owned);
    puts("PASS: page change disposes cover before widget deletion, rebuilds correct Audio cards and clears all stale references");
}

static void test_audio_ui(app_runtime_state_t runtime, app_energy_sample_t sample)
{
    app_sim_home_reset();
    audio_test_configure(4);
    ticks = 31000000;
    home_request_set(2);
    irc_test_open(false, runtime, sample);
    assert(home_audio_skin() && home_assets.asset_count == 4);
    assert(home_audio_skin()->header.w == LX_HOME_CARD_WIDTH &&
           home_audio_skin()->header.h == LX_HOME_CARD_HEIGHT);
    for (unsigned i = 0; i < 4; ++i)
    {
        lv_obj_t *card = lv_obj_get_parent(audio_cards[i].play);
        assert(lv_obj_get_width(card) == LX_HOME_CARD_WIDTH && lv_obj_get_height(card) == LX_HOME_CARD_HEIGHT);
        assert(!home_icons[i] && audio_cards[i].screen && audio_cards[i].volume);
        assert(lv_obj_get_width(audio_cards[i].play) >= 48 && lv_obj_get_height(audio_cards[i].play) >= 48);
    }
    assert(!strcmp(lv_label_get_text(lv_obj_get_child(audio_cards[0].play, 0)), LV_SYMBOL_PAUSE));
    audio_test_idle(&sample);
    save_frame("audio-cards-day");
    audio_test_disconnect(&sample);

    /* A három érintési cél külön műveletet végez. */
    unsigned before = app_sim_audio_commands;
    lv_obj_send_event(audio_cards[0].play, LV_EVENT_CLICKED, NULL);
    assert(app_sim_audio_commands == before + 1 && app_sim_audio_last_action == LX_AUDIO_PAUSE && !audio_overlay);
    audio_test_tick(&sample);
    assert(!strcmp(lv_label_get_text(lv_obj_get_child(audio_cards[0].play, 0)), LV_SYMBOL_PLAY));
    lv_obj_send_event(audio_cards[0].volume, LV_EVENT_CLICKED, NULL);
    assert(audio_overlay && !audio_detail && audio_volume_slider && !audio_favorites_snapshot);
    assert(lv_slider_get_max_value(audio_volume_slider) == 85);
    before = app_sim_audio_commands;
    lv_obj_send_event(audio_volume_slider, LV_EVENT_PRESSED, NULL);
    lv_slider_set_value(audio_volume_slider, 63, LV_ANIM_OFF);
    lv_obj_send_event(audio_volume_slider, LV_EVENT_VALUE_CHANGED, NULL);
    assert(app_sim_audio_commands == before + 1 && app_sim_audio_last_action == LX_AUDIO_VOLUME && audio_dragging);
    lv_obj_send_event(audio_volume_slider, LV_EVENT_RELEASED, NULL);
    assert(app_sim_audio_commands == before + 2 && !audio_dragging);
    audio_test_idle(&sample);
    save_frame("audio-volume-day");
    unsigned cancelled = app_sim_audio_volume_cancellations;
    audio_ui_reset();
    assert(app_sim_audio_volume_cancellations == cancelled + 1);

    lv_obj_send_event(audio_cards[0].screen, LV_EVENT_CLICKED, NULL);
    assert(audio_overlay && audio_detail && audio_favorites_snapshot && audio_transport_buttons[2]);
    assert(audio_favorites_snapshot->total == 70 && audio_favorites_snapshot->count == 32);
    assert(audio_favorites_ids[0] == 1000 && audio_favorites_ids[5] == 1005);
    before = app_sim_audio_commands;
    lv_obj_send_event(audio_transport_buttons[2], LV_EVENT_CLICKED, NULL);
    assert(app_sim_audio_commands == before + 1 && app_sim_audio_last_action == LX_AUDIO_NEXT);
    lv_obj_send_event(audio_transport_buttons[0], LV_EVENT_CLICKED, NULL);
    assert(app_sim_audio_commands == before + 2 && app_sim_audio_last_action == LX_AUDIO_PREVIOUS);
    lv_obj_send_event(audio_favorites_rows[1], LV_EVENT_CLICKED, NULL);
    assert(app_sim_audio_commands == before + 3 && app_sim_audio_last_favorite == 1001);
    audio_test_idle(&sample);
    save_frame("audio-detail-day");

    /* A 32-es hálózati lap és a hat látható sor határán sem vész el elem. */
    bool seen[70] = {false};
    unsigned pages = 0;
    for (;;)
    {
        for (unsigned i = 0; i < AUDIO_UI_VISIBLE_ROWS; ++i)
            if (audio_favorites_ids[i])
            {
                assert(audio_favorites_ids[i] >= 1000 && audio_favorites_ids[i] < 1070);
                seen[audio_favorites_ids[i] - 1000] = true;
            }
        if (lv_obj_has_state(audio_favorites_next, LV_STATE_DISABLED))
            break;
        assert(++pages < 20);
        lv_obj_send_event(audio_favorites_next, LV_EVENT_CLICKED, NULL);
    }
    for (unsigned i = 0; i < 70; ++i)
        assert(seen[i]);
    assert(audio_favorites_ids[5] == 1069);
    lv_obj_send_event(audio_favorites_rows[5], LV_EVENT_CLICKED, NULL);
    assert(app_sim_audio_last_favorite == 1069);
    lv_obj_send_event(audio_favorites_prev, LV_EVENT_CLICKED, NULL);
    assert(audio_favorites_ids[0] == 1058 && audio_favorites_ids[5] == 1063);
    audio_ui_reset();

    /* Rövid hálózati oldalak mellett visszafelé is elérhető mind a 70 elem. */
    const unsigned short_pages[] = {20, 3, 1};
    for (unsigned mode = 0; mode < sizeof(short_pages) / sizeof(short_pages[0]); ++mode)
    {
        app_sim_audio_set_favorite_page_size(short_pages[mode]);
        audio_ui_open(0, true);
        unsigned first = short_pages[mode] == 20 ? 40 : 69;
        assert(app_loxone_audio_favorites_request(audio_card_id, audio_ui_revision, first) == ESP_OK);
        audio_ui_update();
        bool previous_seen[70] = {false};
        unsigned rounds = 0;
        for (;;)
        {
            for (unsigned i = 0; i < AUDIO_UI_VISIBLE_ROWS; ++i)
                if (audio_favorites_ids[i])
                    previous_seen[audio_favorites_ids[i] - 1000] = true;
            if (lv_obj_has_state(audio_favorites_prev, LV_STATE_DISABLED))
                break;
            assert(++rounds <= 70);
            lv_obj_send_event(audio_favorites_prev, LV_EVENT_CLICKED, NULL);
            for (unsigned i = 0; audio_favorites_backward_end && i < AUDIO_UI_VISIBLE_ROWS; ++i)
                audio_test_tick(&sample);
            assert(!audio_favorites_backward_end);
        }
        for (unsigned i = 0; i < first; ++i)
            assert(previous_seen[i]);
        audio_test_idle(&sample);
        audio_ui_reset();
    }
    app_sim_audio_set_favorite_page_size(32);
    audio_ui_open(0, true);
    audio_test_state_labels(&sample);
    audio_test_volume_feedback(&sample);
    audio_test_user_tracks(&sample);

    /* A törölt modal régi eseménye és a megszakadt oldal nem vezérelhet. */
    unsigned old_generation = audio_ui_generation;
    audio_ui_reset();
    audio_ui_open(0, true);
    lv_obj_t *stale = lv_obj_create(s_screen);
    lv_obj_add_event_cb(stale, audio_ui_transport_event, LV_EVENT_CLICKED, (void *)(uintptr_t)old_generation);
    before = app_sim_audio_commands;
    lv_obj_send_event(stale, LV_EVENT_CLICKED, NULL);
    assert(app_sim_audio_commands == before && audio_overlay);
    lv_obj_delete(stale);
    app_sim_home_connected = false;
    audio_test_tick(&sample);
    assert(!audio_overlay && lv_obj_has_state(audio_cards[0].play, LV_STATE_DISABLED));
    lv_obj_send_event(audio_cards[0].play, LV_EVENT_CLICKED, NULL);
    assert(app_sim_audio_commands == before);
    app_sim_home_connected = true;
    audio_test_tick(&sample);
    irc_test_open(true, runtime, sample);
    audio_test_idle(&sample);
    save_frame("audio-cards-night");
    audio_ui_open(0, true);
    audio_test_idle(&sample);
    save_frame("audio-detail-night");
    audio_ui_reset();

    home_request_set(0);
    assert(home_prepare(true));
    audio_test_page_dispose(runtime, sample);
    audio_test_configure(12);
    home_request_set(2);
    irc_test_open(false, runtime, sample);
    assert(home_assets.asset_count == 4 && home_audio_skin());
    for (unsigned i = 0; i < 12; ++i)
        assert(audio_cards[i].play && audio_cards[i].cover.fallback == &home_images[home_icon_index(LX_AUDIO_ICON_ID)]);
    audio_test_idle(&sample);
    save_frame("audio-twelve-cards-day");

    /* Csengetéskor a régi Audio-UI megszűnik; a kamera csak az opcionális
     * borítóworker saját puffereinek elengedése után foglalhat nagy képeket. */
    audio_ui_open(0, false);
    assert(audio_overlay && audio_volume_slider);
    audio_test_attach_cover(&audio_cards[0].cover);
    unsigned disposed_for_camera = 0;
    lv_obj_add_event_cb(audio_cards[0].cover.object, audio_test_disposed_cover, LV_EVENT_DELETE, &disposed_for_camera);
    app_sim_audio_media_busy(true);
    app_sim_intercom = (app_intercom_snapshot_t){.ringing = true, .connected = true, .revision = 901};
    assert(intercom_prepare());
    assert(!audio_overlay && !audio_cards[0].cover.object && home_assets.asset_count == 0);
    assert(disposed_for_camera == 1);
    assert(intercom_audio_media_wait && !intercom_prepared && !intercom_front && !intercom_spare);
    intercom_render();
    assert(intercom_overlay && strstr(lv_label_get_text(intercom_message), "előkészítése"));
    assert(intercom_prepare() && !intercom_prepared && !intercom_spare);
    app_sim_audio_media_busy(false);
    assert(intercom_prepare() && !intercom_audio_media_wait && intercom_prepared && intercom_spare);
    intercom_render();
    assert(intercom_front && !intercom_prepared);
    app_sim_intercom.ringing = false;
    assert(!intercom_prepare());
    intercom_render();
    assert(!intercom_overlay && !intercom_front && !intercom_spare);
    assert(home_request_get() == 2);
    irc_test_open(false, runtime, sample);
    assert(audio_cards[0].play && home_audio_skin());
    audio_test_idle(&sample);

    /* A régi SD-csomagból hiányzó skin/ikon sem teheti elérhetetlenné a vezérlést. */
    home_request_set(0);
    assert(home_prepare(false));
    audio_test_configure(4);
    home_missing_card_icons = true;
    home_request_set(2);
    irc_test_open(false, runtime, sample);
    assert(home_assets.asset_count == 2 && !home_audio_skin());
    lv_obj_send_event(audio_cards[0].screen, LV_EVENT_CLICKED, NULL);
    assert(audio_overlay && audio_detail && audio_favorites_ids[0] == 1000);
    audio_ui_reset();
    audio_test_idle(&sample);
    save_frame("audio-cards-legacy-pack");
    home_request_set(0);
    assert(home_prepare(false));
    home_missing_card_icons = false;
    puts("PASS: actual Audio LVGL card and volume/detail controls, 70 favorites with short pages, stale-event guards, 12-card shared skin, Intercom handoff, legacy fallback and zero idle invalidations");
}
