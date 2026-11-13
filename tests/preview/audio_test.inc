/* A valódi LVGL Audio-kártyát és modalt vizsgáljuk explicit helyi adatokkal.
 * Ez nem állít élő Audioserver-elérést; annak protokolltesztje külön fut. */
#include "../../simulator/audio_fixture.h"

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
    app_sim_audio_media_busy(true);
    app_sim_intercom = (app_intercom_snapshot_t){.ringing = true, .connected = true, .revision = 901};
    assert(intercom_prepare());
    assert(!audio_overlay && !audio_cards[0].cover.object && home_assets.asset_count == 0);
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
