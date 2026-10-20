/* Valódi SD-csomagkezelő: a közös ikonok nem lapozható témák. */
#include "app_templates.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    assert(argc == 6); setenv("ZOLEE_TEMPLATE_ROOT",argv[2],1);
    assert(app_templates_init()==ESP_OK);
    unsigned generation=0;
    assert(!app_templates_home_info(NULL,&generation));
    char error[160];
    assert(app_templates_install(argv[1],error)==ESP_OK);
    app_template_info_t info;
    assert(app_templates_home_info(&info,&generation)&&info.version==1);
    assert(!strcmp(info.id,"loxone-home"));
    assert(app_templates_list(NULL,APP_TEMPLATE_MAX,NULL)==1);
    assert(app_templates_select("loxone-home")!=ESP_OK);
    app_template_view_t *view=calloc(1,sizeof(*view));assert(view);
    const char *icons[]={"washing-machine","refrigerator","washing-machine"};
    assert(app_templates_home_load(icons,3,false,view));
    assert(view->asset_count==4&&view->generation==generation);
    assert(view->assets[0].width==1280&&view->assets[0].height==800);
    assert(!strcmp(view->assets[2].name,"icon-washing-machine.png"));
    app_templates_view_free(view);
    assert(app_templates_home_load(icons,3,true,view));
    assert(!strcmp(view->assets[0].name,"background-dark.png"));
    app_templates_view_free(view);
    const char *bad[]={"../unsafe"};
    assert(!app_templates_home_load(bad,1,false,view)&&!view->asset_count);
    for(int i=3;i<6;i++) {
        assert(app_templates_install(argv[i],error)!=ESP_OK);
        assert(app_templates_home_info(NULL,NULL));
    }
    /* Webes frissítés közben a már előtöltött nézet továbbra is használható. */
    assert(app_templates_home_load(icons,3,false,view));
    size_t size=view->assets[2].size; unsigned char first=view->assets[2].bytes[0];
    assert(app_templates_install(argv[1],error)==ESP_OK);
    unsigned next;assert(app_templates_home_info(NULL,&next)&&next>generation);
    assert(view->assets[2].size==size&&view->assets[2].bytes[0]==first);
    app_templates_view_free(view);free(view);
    assert(app_templates_delete("loxone-home",error)==ESP_OK&&!app_templates_home_info(NULL,NULL));
    assert(app_templates_list(NULL,APP_TEMPLATE_MAX,NULL)==1);
    puts("PASS: actual 64-icon resource ZIP validation/install/update/delete, no selectable theme, bounded active assets, deduplication, day/night and old-view ownership");
}
