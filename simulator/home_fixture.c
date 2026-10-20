/* Csak szimulátorhoz: ugyanazt a publikus UI-határt helyettesítjük helyi adatokkal.
 * A valódi parser/parancsküldő tesztje külön, a tényleges C-modulokon fut. */
#include "home_fixture.h"
#include <stdio.h>
#include <string.h>
bool app_sim_home_enabled, app_sim_home_connected, app_sim_home_pending;
unsigned app_sim_home_commands;
lx_home_command_t app_sim_home_last_action;
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
    app_sim_home_config.card_count=8;
    for(unsigned i=0;i<8;i++) {
        lx_home_card_t *card=&app_sim_home_config.cards[i];snprintf(card->id,sizeof(card->id),"card-%u",i);
        snprintf(card->name,sizeof(card->name),"%s",card_names[i]);snprintf(card->icon,sizeof(card->icon),"%s",card_icons[i]);
        card->type=i<4?LX_HOME_CARD_VALUE:i<6?LX_HOME_CARD_SWITCH:LX_HOME_CARD_PULSE;
        app_sim_home_values.command_supported[i]=i>=4;
        card->decimals=i==0?1:0;strcpy(card->warning_color,"#E66B6B");
        if(i<4)snprintf(card->unit,sizeof(card->unit),"%s",units[i]);
        if(i<6)app_sim_home_values.cards[i]=(lx_home_value_t){.value=card_values[i],.valid=true};
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
    app_sim_home_commands++;app_sim_home_last_action=action;app_sim_home_pending=true;return ESP_OK;
}
