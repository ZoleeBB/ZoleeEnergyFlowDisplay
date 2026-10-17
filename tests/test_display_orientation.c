#include <assert.h>
#include <stdio.h>
#include "app_display_orientation.h"
static void check(int rotation,bool mirror_x,bool mirror_y)
{
    app_display_orientation_t d=app_display_orientation_for_rotation(rotation);
    assert(d.rotation_degrees==rotation && d.touch_swap_xy);
    assert(d.touch_mirror_x==mirror_x && d.touch_mirror_y==mirror_y);
    const int samples[][2]={{0,0},{799,0},{0,1279},{799,1279},{233,971}};
    for(int i=0;i<5;i++){
        const int x=samples[i][0],y=samples[i][1];
        const int lx=mirror_y?1279-y:y,ly=mirror_x?799-x:x;
        assert(lx>=0 && lx<1280 && ly>=0 && ly<800);
        assert(lx==(rotation==270?1279-y:y));
        assert(ly==(rotation==270?x:799-x));
    }
}
int main(void)
{
    check(270,false,true);
    check(90,true,false);
    assert(app_display_orientation_for_rotation(275).rotation_degrees==270);
    assert(app_display_normalize_rotation(0)==270);
    assert(app_display_normalize_rotation(90)==90);
    assert(app_display_normalize_rotation(270)==270);
    assert(app_display_normalize_rotation(275)==270);
    const int32_t points[][2]={{0,0},{1279,0},{0,799},{1279,799},{399,488}};
    for(int i=0;i<5;i++) {
        int32_t x=points[i][0],y=points[i][1];
        app_display_map_touch_point(270,&x,&y);
        assert(x==points[i][0]&&y==points[i][1]);
        app_display_map_touch_point(90,&x,&y);
        assert(x==1279-points[i][0]&&y==799-points[i][1]);
        app_display_map_touch_point(90,&x,&y);
        assert(x==points[i][0]&&y==points[i][1]);
    }
    puts("PASS: display and touch orientation 90/270");
    return 0;
}
