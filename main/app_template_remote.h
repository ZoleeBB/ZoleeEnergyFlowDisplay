#pragma once
/*
 * Gyári SD-témák frissítése a GitHubról – platformfüggetlen szabályok.
 *
 * A firmware a saját kiadási tagjéhez (vX.Y.Z) tartozó témákat keresi, így a
 * telepített témák pontosan illeszkednek a futó firmware-hez. Fejlesztői
 * buildnél a legutóbbi stabil kiadást használja; ha egy téma ennél újabb
 * firmware-t igényel, a telepítéskori validálás elutasítja, és a régi marad.
 */
#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"

#define APP_TEMPLATE_REMOTE_REPO "ZoleeBB/ZoleeEnergyFlowDisplay"
#define APP_TEMPLATE_REMOTE_RAW "https://raw.githubusercontent.com/" APP_TEMPLATE_REMOTE_REPO "/"
#define APP_TEMPLATE_REMOTE_CONTENTS "https://api.github.com/repos/" APP_TEMPLATE_REMOTE_REPO "/contents/templates?ref="
#define APP_TEMPLATE_REMOTE_LATEST "https://api.github.com/repos/" APP_TEMPLATE_REMOTE_REPO "/releases/latest"
#define APP_TEMPLATE_REMOTE_MAX_IDS 16
#define APP_TEMPLATE_REMOTE_MAX_FILES 40

/* A futó firmware stabil tagje, ennek hiányában a legutóbbi stabil kiadás. */
bool app_template_remote_tag(const char *running, const char *latest, char out[32]);
/* A GitHub contents API listájából a biztonságos téma-könyvtárnevek. */
unsigned app_template_remote_ids(const cJSON *contents, char ids[][33], unsigned capacity);
/* A template.json által hivatkozott PNG-k és a két előnézet, ismétlés nélkül. */
bool app_template_remote_files(const cJSON *document, char names[][65], unsigned capacity,
                               unsigned *count);
/* Nyers fájl URL-je a megadott tagnél; hamis, ha bármelyik rész nem biztonságos. */
bool app_template_remote_url(char *out, size_t size, const char *tag, const char *id,
                             const char *file);
/* Csak HTTPS és a két GitHub-host (átirányításnál is). */
bool app_template_remote_url_allowed(const char *url);
