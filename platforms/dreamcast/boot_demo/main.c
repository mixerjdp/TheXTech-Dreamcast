/*
 * TheXTech Dreamcast boot demo — standalone PVR splash for Flycast/CDI testing.
 * Builds with KallistiOS Makefile independently of the full engine.
 */

#include <kos.h>
#include <dc/pvr.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <stdio.h>

static pvr_init_params_t pvr_params = {
    { PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0 },
    512 * 1024,
    0, 0, 0, 0, 0
};

static void draw_rect(float x1, float y1, float x2, float y2, uint32_t argb)
{
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t vert;

    pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    vert.flags = PVR_CMD_VERTEX;
    vert.z = 1.0f;
    vert.u = vert.v = 0;
    vert.argb = argb;
    vert.oargb = 0;

    vert.x = x1; vert.y = y1; pvr_prim(&vert, sizeof(vert));
    vert.x = x2; vert.y = y1; pvr_prim(&vert, sizeof(vert));
    vert.x = x1; vert.y = y2; pvr_prim(&vert, sizeof(vert));
    vert.flags = PVR_CMD_VERTEX_EOL;
    vert.x = x2; vert.y = y2; pvr_prim(&vert, sizeof(vert));
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("TheXTech Dreamcast boot demo\n");
    vid_set_mode(DM_640x480, PM_RGB565);
    pvr_init(&pvr_params);
    pvr_set_bg_color(0.05f, 0.10f, 0.25f);

    int running = 1;
    while(running)
    {
        maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        if(cont)
        {
            cont_state_t *st = (cont_state_t *)maple_dev_status(cont);
            if(st && (st->buttons & CONT_START))
                running = 0;
        }

        pvr_wait_ready();
        pvr_scene_begin();
        pvr_list_begin(PVR_LIST_OP_POLY);

        /* Title bar */
        draw_rect(40, 80, 600, 160, 0xFF2060C0);
        /* Accent */
        draw_rect(40, 180, 600, 220, 0xFFE0A020);
        /* Footer bar */
        draw_rect(40, 400, 600, 440, 0xFF103050);

        pvr_list_finish();
        pvr_list_begin(PVR_LIST_TR_POLY);
        pvr_list_finish();
        pvr_scene_finish();
    }

    pvr_shutdown();
    return 0;
}
