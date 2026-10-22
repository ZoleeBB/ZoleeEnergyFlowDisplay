/* Csak szimulátorhoz: ugyanazt a publikus UI-határt helyettesítjük helyi adatokkal.
 * A valódi parser/parancsküldő tesztje külön, a tényleges C-modulokon fut. */
#include "home_fixture.h"
#include <stdio.h>
#include <string.h>
bool app_sim_home_enabled, app_sim_home_connected, app_sim_home_pending;
unsigned app_sim_home_commands;
lx_home_command_t app_sim_home_last_action;
char app_sim_home_last_id[LX_HOME_ID_SIZE];
lx_home_config_t app_sim_home_config;
lx_home_values_t app_sim_home_values;
void app_sim_home_reset(void)
{
    memset(&app_sim_home_config,0,sizeof(app_sim_home_config));
    memset(&app_sim_home_values,0,sizeof(app_sim_home_values));
    app_sim_home_enabled=app_sim_home_connected=true;app_sim_home_pending=false;
    app_sim_home_commands=0;
    const char *names[]={"Hűtőszekrény","Mosógép","Szárítógép","Mosogatógép","Sütő","Főzőlap","Klíma"};
    const char *icons[]={"refrigerator","washing-machine","tumble-dryer","dishwasher","oven","hob","ac-indoor"};
    const double power[]={.084,0,0,1.2,0,.85,.43};
    app_sim_home_config.consumer_count=7;
    for(unsigned i=0;i<7;i++) {
        lx_home_consumer_t *item=&app_sim_home_config.consumers[i];
        snprintf(item->id,sizeof(item->id),"consumer-%u",i);
        snprintf(item->name,sizeof(item->name),"%s",names[i]);snprintf(item->icon,sizeof(item->icon),"%s",icons[i]);
        app_sim_home_values.consumers[i]=(lx_home_value_t){.value=power[i],.valid=true};
    }
    const char *card_names[]={"Nappali","Páratartalom","Bojler","Autó töltöttsége","Kerti világítás","Szellőztetés","Melegvíz-rásegítés","Garázskapu"};
    const char *card_icons[]={"temperature","humidity","water-heater","electric-car","garden-light","ventilation","water-heater","garage-door"};
    const char *units[]={"°C","%","°C","%"};
    const double card_values[]={22.4,46,54,72,1,0};
    app_sim_home_config.card_count = 10;
    app_sim_home_config.page_count = 3;
    const char *page_names[] = {"Földszint", "Kert", "Vendégszoba"};
    const unsigned first_card[] = {0, 8, 10}, card_count[] = {8, 2, 0};
    for (unsigned p = 0; p < 3; p++)
    {
        lx_home_page_t *page = &app_sim_home_config.pages[p];
        snprintf(page->id, sizeof(page->id), "page-%u", p);
        snprintf(page->title, sizeof(page->title), "%s", page_names[p]);
        page->first_card = first_card[p];
        page->card_count = card_count[p];
    }
    for(unsigned i=0;i<8;i++) {
        lx_home_card_t *card=&app_sim_home_config.cards[i];snprintf(card->id,sizeof(card->id),"card-%u",i);
        snprintf(card->name,sizeof(card->name),"%s",card_names[i]);snprintf(card->icon,sizeof(card->icon),"%s",card_icons[i]);
        card->type=i<4?LX_HOME_CARD_VALUE:i<6?LX_HOME_CARD_SWITCH:LX_HOME_CARD_PULSE;
        app_sim_home_values.command_supported[i]=i>=4;
        card->decimals=i==0?1:0;strcpy(card->warning_color,"#E66B6B");
        if(i<4)snprintf(card->unit,sizeof(card->unit),"%s",units[i]);
        if(i<6)app_sim_home_values.cards[i]=(lx_home_value_t){.value=card_values[i],.valid=true};
    }
    for (unsigned i = 8; i < 10; i++)
    {
        lx_home_card_t *card = &app_sim_home_config.cards[i];
        snprintf(card->id, sizeof(card->id), "card-%u", i);
        snprintf(card->name, sizeof(card->name), "%s", i == 8 ? "Kerti hőmérséklet" : "Öntözés indítása");
        snprintf(card->icon, sizeof(card->icon), "%s", i == 8 ? "temperature" : "irrigation");
        card->type = i == 8 ? LX_HOME_CARD_VALUE : LX_HOME_CARD_PULSE;
        strcpy(card->unit, "°C");
        strcpy(card->warning_color, "#E66B6B");
        card->decimals = 1;
        app_sim_home_values.cards[i] = (lx_home_value_t){.value = 18.6, .valid = i == 8};
        app_sim_home_values.command_supported[i] = i == 9;
    }
}
bool app_loxone_home_snapshot(lx_home_config_t *config,lx_home_values_t *values,unsigned *revision,
                              bool *connected,bool *pending,char status[LX_HOME_ERROR_SIZE])
{
    if(!app_sim_home_enabled)return false;
    *config=app_sim_home_config;*values=app_sim_home_values;*revision=1;
    *connected=app_sim_home_connected;*pending=app_sim_home_pending;status[0]=0;return true;
}
esp_err_t app_loxone_home_command(const char *id,lx_home_command_t action)
{
    if(!app_sim_home_enabled||!app_sim_home_connected||app_sim_home_pending||!id)return ESP_ERR_INVALID_STATE;
    snprintf(app_sim_home_last_id,sizeof(app_sim_home_last_id),"%s",id);
    app_sim_home_commands++;app_sim_home_last_action=action;app_sim_home_pending=true;return ESP_OK;
}

/* A desktop bemutató nem szimulál csendben valódi energiaajánlást.
 * A render-teszt külön, explicit szintetikus bemenettel lépteti a modellt. */
bool app_loxone_assistant_snapshot(app_assistant_consumers_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!app_sim_home_enabled)
        return false;
    out->count = app_sim_home_config.consumer_count;
    out->revision = out->session = 1;
    out->connected = app_sim_home_connected;
    for (unsigned i = 0; i < out->count && i < LX_HOME_MAX_CONSUMERS; ++i)
    {
        const lx_home_consumer_t *source = &app_sim_home_config.consumers[i];
        app_assistant_consumer_t *target = &out->consumers[i];
        snprintf(target->id, sizeof(target->id), "%s", source->id);
        snprintf(target->name, sizeof(target->name), "%s", source->name);
        target->eligible = source->suggest_start && source->average_known && source->average_kw > 0;
        target->required_kw = source->average_kw;
        target->measured_kw = app_sim_home_values.consumers[i].value;
        target->valid = app_sim_home_values.consumers[i].valid;
    }
    return true;
}

bool app_stats_get_night_reference(app_assistant_night_t *out)
{
    memset(out, 0, sizeof(*out));
    return false;
}

/* A valódi felugró nézet tesztelhető hálózat és hardveres JPEG nélkül. */
app_intercom_snapshot_t app_sim_intercom;
bool app_loxone_intercom_snapshot(app_intercom_snapshot_t *out)
{
    *out = app_sim_intercom;
    return true;
}
