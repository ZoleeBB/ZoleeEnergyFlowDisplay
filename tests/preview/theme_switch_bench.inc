/* Témaváltási költség mérése a valódi UI-kódon (preview.c része).
 *
 * A balra húzással azonos lépést (app_templates_step_available) használjuk, és
 * a runtime task sorrendjét követjük: betöltőkép -> SD/PNG előkészítés ->
 * nézetépítés -> első teljes kép -> belépési áttűnés. A host CPU nem az
 * ESP32-P4, ezért az abszolút időknél az arányok a fontosak. A végső kép
 * FNV-1a ellenőrzőösszege a változtatás előtti/utáni képpontazonosságot igazolja. */
#include <time.h>

static double bench_now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000.0 + now.tv_nsec / 1000000.0;
}

static uint32_t bench_frame_hash(void)
{
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < 1280U * 800U; i++)
    {
        uint32_t value = pixel_rgb(i);
        for (unsigned shift = 0; shift < 24; shift += 8)
        {
            hash ^= (value >> shift) & 255U;
            hash *= 16777619u;
        }
    }
    return hash;
}

static void bench_ticks(unsigned count)
{
    for (unsigned tick = 0; tick < count; tick++)
    {
        ticks += 16000;
        lv_tick_inc(16);
        lv_timer_handler();
    }
}

static void test_theme_switch_bench(app_runtime_state_t runtime, app_energy_sample_t sample)
{
    const char *rounds_env = getenv("ZOLEE_BENCH_ROUNDS");
    unsigned rounds = rounds_env ? (unsigned)atoi(rounds_env) : 3;
    sample.sun_position_valid = true;
    /* Kiinduló állapot: beépített nézet, kész kép. */
    assert(app_templates_select("builtin") == ESP_OK);
    assert(template_loading_frame());
    template_prepare();
    render_runtime(runtime, sample, true, 31000000);
    lv_refr_now(display);
    bench_ticks(20);
    app_template_info_t themes[16];
    char active[33];
    unsigned count = app_templates_list(themes, 16, active);
    double total[6] = {0};
    for (unsigned round = 0; round < rounds; round++)
    {
        for (unsigned step = 0; step < count; step++)
        {
            double t0 = bench_now_ms();
            assert(app_templates_step_available(1, true));
            template_loading_requested = true;
            assert(template_loading_frame());
            double t1 = bench_now_ms();
            template_prepare();
            double t2 = bench_now_ms();
            render_runtime(runtime, sample, true, 31000000);
            double t3 = bench_now_ms();
            lv_refr_now(display);
            double t4 = bench_now_ms();
            /* A 160 ms-os áttűnés 16 ms-os tickekkel. */
            bench_ticks(12);
            lv_refr_now(display);
            double t5 = bench_now_ms();
            double phase[6] = {t1 - t0, t2 - t1, t3 - t2, t4 - t3, t5 - t4, t5 - t0};
            for (unsigned i = 0; i < 6; i++)
                total[i] += phase[i];
            /* Stabil, animációtól független kép a képpont-összevetéshez. */
            uint32_t hash = 0;
            if (round == 0)
                hash = bench_frame_hash();
            printf("SWITCH round=%u theme=%-22s loading=%7.2f prepare=%7.2f build=%7.2f "
                   "first_frame=%7.2f transition=%7.2f total=%7.2f ms",
                   round, template_view.id[0] ? template_view.id : "builtin",
                   phase[0], phase[1], phase[2], phase[3], phase[4], phase[5]);
            if (round == 0)
                printf(" hash=%08x", hash);
            putchar('\n');
        }
    }
    printf("SWITCH_TOTAL switches=%u loading=%.1f prepare=%.1f build=%.1f first_frame=%.1f "
           "transition=%.1f total=%.1f ms\n",
           rounds * count, total[0], total[1], total[2], total[3], total[4], total[5]);
}

/* Eszköz a gyári csomagokhoz: a futásidőben (LVGL-lel) 1280×800-ra skálázott
 * hátterek pontos képpontjainak mentése. Az így készült natív PNG-vel a kijelző
 * képpontra ugyanazt rajzolja, de a témaváltáskori skálázás elmarad.
 * Használat: ZOLEE_EXPORT_SCALED_BACKGROUNDS=<könyvtár>, majd
 * tools/prepare_native_backgrounds.py. */
__attribute__((noinline)) static void export_scaled_backgrounds(app_runtime_state_t runtime, app_energy_sample_t sample,
                                      const char *directory)
{
#if LV_USE_SNAPSHOT
    app_template_info_t themes[16];
    char active[33];
    unsigned count = app_templates_list(themes, 16, active);
    unsigned exported = 0;
    for (unsigned t = 0; t < count; t++)
    {
        if (!strcmp(themes[t].id, "builtin"))
            continue;
        for (int night = 0; night < 2; night++)
        {
            assert(app_templates_select(themes[t].id) == ESP_OK);
            template_loading_requested = true;
            assert(template_loading_frame());
            template_prepare();
            sample.night_mode = night;
            render_runtime(runtime, sample, true, 31000000);
            lv_refr_now(display);
            for (unsigned i = 0; i < template_raster_count; i++)
            {
                const template_raster_t *raster = &template_rasters[i];
                if (raster->w != 1280 || raster->h != 800)
                    continue;
                char path[2048];
                snprintf(path, sizeof(path), "%s/%s__%s.argb", directory, themes[t].id,
                         template_view.assets[raster->asset].name);
                FILE *file = fopen(path, "wb");
                assert(file);
                const lv_draw_buf_t *buffer = raster->buffer;
                for (int y = 0; y < raster->h; y++)
                    assert(fwrite(buffer->data + (size_t)y * buffer->header.stride, 4, (size_t)raster->w, file) ==
                           (size_t)raster->w);
                assert(fclose(file) == 0);
                printf("EXPORTED %s\n", path);
                exported++;
            }
        }
    }
    printf("EXPORTED_TOTAL %u\n", exported);
#else
    (void)runtime;
    (void)sample;
    (void)directory;
#endif
}
