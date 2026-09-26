/* GIMP LiquidRescale Plug-in
 * Copyright (C) 2007-2010 Carlo Baldassi (the "Author") <carlobaldassi@gmail.com>.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the Licence, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org.licences/>.
 */

#include <stdio.h>

#include <libgimp/gimp.h>
#include <lqr.h>

#include "config.h"
#include "plugin-intl.h"

#include "io_functions.h"

#include "main_common.h"

/* The pixels of the layer to rescale, as float in the colour space of the
   layer, so that 16-bit and floating point images keep their precision and
   liblqr sees real channel values. */
const Babl *
layer_pixel_format(gint32 layer_ID) {
    GimpDrawable *drawable = gimp_drawable_get_by_id(layer_ID);
    const Babl *space = babl_format_get_space(gimp_drawable_get_format(drawable));
    const gchar *name;

    if (gimp_drawable_is_rgb(drawable)) {
        name = gimp_drawable_has_alpha(drawable) ? "R'G'B'A float" : "R'G'B' float";
    } else {
        name = gimp_drawable_has_alpha(drawable) ? "Y'A float" : "Y' float";
    }

    return babl_format_with_space(name, space);
}

gint
layer_channels(gint32 layer_ID) {
    return babl_format_get_n_components(layer_pixel_format(layer_ID));
}

gfloat *
float_buffer_from_layer(gint32 layer_ID) {
    gint w, h;
    gsize size;
    GeglBuffer *buffer_in;
    gfloat *buffer;
    const Babl *format = layer_pixel_format(layer_ID);

    gimp_progress_init(_("Parsing layer..."));

    w = gimp_drawable_get_width_id(layer_ID);
    h = gimp_drawable_get_height_id(layer_ID);

    if (!g_size_checked_mul(&size, w, h) ||
        !g_size_checked_mul(&size, size, babl_format_get_n_components(format))) {
        return NULL;
    }
    LQR_TRY_N_N (buffer = g_try_new(gfloat, size));

    buffer_in = gimp_drawable_get_buffer(gimp_drawable_get_by_id(layer_ID));
    gegl_buffer_get(buffer_in, GEGL_RECTANGLE (0, 0, w, h), 1.0, format, buffer,
                    GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
    g_object_unref(buffer_in);

    gimp_progress_end();

    return buffer;
}

/* Masks are painted marks, whose strength liblqr reads from 8-bit values,
   as the plugin got them in GIMP 2. Any mask layer is read as 8-bit RGBA. */
#define MASK_CHANNELS 4

static guchar *
mask_buffer_from_layer(gint32 layer_ID) {
    gint w, h;
    GeglBuffer *buffer_in;
    guchar *buffer;

    w = gimp_drawable_get_width_id(layer_ID);
    h = gimp_drawable_get_height_id(layer_ID);

    LQR_TRY_N_N (buffer = g_try_new(guchar, (gsize) MASK_CHANNELS * w * h));

    buffer_in = gimp_drawable_get_buffer(gimp_drawable_get_by_id(layer_ID));
    gegl_buffer_get(buffer_in, GEGL_RECTANGLE (0, 0, w, h), 1.0,
                    babl_format("R'G'B'A u8"), buffer,
                    GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
    g_object_unref(buffer_in);

    return buffer;
}

LqrRetVal
update_bias(LqrCarver *r, gint32 layer_ID, gint bias_factor,
            gint base_x_off, gint base_y_off) {
    guchar *rgb;
    gint w, h;
    gint x_off, y_off;

    if ((layer_ID == 0) || (bias_factor == 0)) {
        return LQR_OK;
    }

    gimp_drawable_get_offsets_id(layer_ID, &x_off, &y_off);
    x_off -= base_x_off;
    y_off -= base_y_off;

    w = gimp_drawable_get_width_id(layer_ID);
    h = gimp_drawable_get_height_id(layer_ID);

    rgb = mask_buffer_from_layer(layer_ID);
    CATCH_MEM (rgb);

    CATCH (lqr_carver_bias_add_rgb_area
                   (r, rgb, bias_factor, MASK_CHANNELS, w, h, x_off, y_off));

    g_free(rgb);

    return LQR_OK;
}

LqrRetVal
set_rigmask(LqrCarver *r, gint32 layer_ID, gint base_x_off, gint base_y_off) {
    guchar *rgb;
    gint w, h;
    gint x_off, y_off;

    if (layer_ID == 0) {
        return LQR_OK;
    }

    gimp_drawable_get_offsets_id(layer_ID, &x_off, &y_off);
    x_off -= base_x_off;
    y_off -= base_y_off;

    w = gimp_drawable_get_width_id(layer_ID);
    h = gimp_drawable_get_height_id(layer_ID);

    rgb = mask_buffer_from_layer(layer_ID);
    CATCH_MEM (rgb);

    CATCH (lqr_carver_rigmask_add_rgb_area
                   (r, rgb, MASK_CHANNELS, w, h, x_off, y_off));

    g_free(rgb);

    return LQR_OK;
}


LqrRetVal
write_carver_to_layer(LqrCarver *r, gint32 layer_ID) {
    GeglBuffer *buffer_out;
    gint y;
    gint w, h;
    void *out_line;
    gint update_step;
    const Babl *format = layer_pixel_format(layer_ID);

    gimp_progress_init(_("Applying changes..."));
    update_step = MAX ((lqr_carver_get_height(r) - 1) / 20, 1);

    w = gimp_drawable_get_width_id(layer_ID);
    h = gimp_drawable_get_height_id(layer_ID);

    buffer_out = gimp_drawable_get_buffer(GIMP_DRAWABLE(gimp_drawable_get_by_id(layer_ID)));


    while (lqr_carver_scan_line_ext(r, &y, &out_line)) {
        if (lqr_carver_scan_by_row(r)) {
            gegl_buffer_set(buffer_out, GEGL_RECTANGLE (0, y, w, 1), 0, format, out_line, GEGL_AUTO_ROWSTRIDE);
        } else {
            gegl_buffer_set(buffer_out, GEGL_RECTANGLE (y, 0, 1, h), 0, format, out_line, GEGL_AUTO_ROWSTRIDE);
        }

        if (y % update_step == 0) {
            gimp_progress_update((gdouble) y / (lqr_carver_get_height(r) - 1));
        }

    }

    gegl_buffer_flush(buffer_out);
    gimp_drawable_update(GIMP_DRAWABLE(gimp_drawable_get_by_id(layer_ID)), 0, 0, w, h);

    g_object_unref(buffer_out);

    gimp_progress_end();

    return LQR_OK;
}

LqrRetVal
write_vmap_to_layer(LqrVMap *vmap, gpointer data) {
    gint w, h, bpp;
    gint depth;
    gint *buffer;
    gint32 seam_layer_ID;
    gint32 *seam_layer_p;
    gint32 image_ID;
    GeglBuffer *buffer_out;
    gint x_off, y_off;
    gchar *name;
    GeglColor *col_start, *col_end;
    guchar *outrow;
    gdouble value, rd, gr, bl, al;
    gdouble start_rgba[4], end_rgba[4];
    gint vs, y, x, k;
    gint update_step;

    image_ID = VMAP_FUNC_ARG (data)->image_ID;
    x_off = VMAP_FUNC_ARG (data)->x_off;
    y_off = VMAP_FUNC_ARG (data)->y_off;
    name = VMAP_FUNC_ARG (data)->name;
    col_start = VMAP_FUNC_ARG (data)->colour_start;
    col_end = VMAP_FUNC_ARG (data)->colour_end;
    seam_layer_ID = -1;
    seam_layer_p = VMAP_FUNC_ARG (data)->vmap_layer_ID_p;
    if (seam_layer_p) {
        seam_layer_ID = *seam_layer_p;
    }

    w = lqr_vmap_get_width(vmap);
    h = lqr_vmap_get_height(vmap);
    buffer = lqr_vmap_get_data(vmap);
    depth = lqr_vmap_get_depth(vmap);

    gimp_progress_init(_("Drawing seam map..."));
    update_step = MAX ((h - 1) / 20, 1);

    if (!gimp_drawable_is_valid_id(seam_layer_ID)) {

        seam_layer_ID =
                gimp_layer_new_id(image_ID, name, w, h, GIMP_RGBA_IMAGE, 100,
                                  GIMP_LAYER_MODE_NORMAL);

        gimp_drawable_fill_id(seam_layer_ID, GIMP_FILL_TRANSPARENT);
        gimp_image_insert_layer_id(image_ID, seam_layer_ID, 0, -1);
        gimp_layer_set_offsets(GIMP_LAYER(gimp_drawable_get_by_id(seam_layer_ID)), x_off, y_off);
        if (seam_layer_p) {
            *seam_layer_p = seam_layer_ID;
        }
    } else {
        gimp_layer_resize_id(seam_layer_ID, w, h, 0, 0);
    }
    bpp = 4;

    CATCH_MEM (outrow = g_try_new(guchar, w * bpp));

    buffer_out = gimp_drawable_get_buffer(GIMP_DRAWABLE(gimp_drawable_get_by_id(seam_layer_ID)));

    gegl_color_get_pixel(col_start, babl_format("R'G'B'A double"), start_rgba);
    gegl_color_get_pixel(col_end, babl_format("R'G'B'A double"), end_rgba);

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            vs = buffer[y * w + x];
            if (vs == 0) {
                for (k = 0; k < bpp; k++) {
                    outrow[x * bpp + k] = 0;
                }
            } else {
                value = (double) (depth + 1 - vs) / (depth + 1);
                rd = value * start_rgba[0] + (1 - value) * end_rgba[0];
                gr = value * start_rgba[1] + (1 - value) * end_rgba[1];
                bl = value * start_rgba[2] + (1 - value) * end_rgba[2];
                al = 0.5 * (1 + value);
                outrow[x * bpp] = 255 * rd;
                outrow[x * bpp + 1] = 255 * gr;
                outrow[x * bpp + 2] = 255 * bl;
                outrow[x * bpp + 3] = 255 * al;
            }
        }
        gegl_buffer_set(buffer_out, GEGL_RECTANGLE (0, y, w, 1), 0,
                        babl_format("R'G'B'A u8"), outrow, GEGL_AUTO_ROWSTRIDE);
        if (y % update_step == 0) {
            gimp_progress_update((gdouble) y / (h - 1));
        }
    }

    gegl_buffer_flush(buffer_out);
    gimp_drawable_update(GIMP_DRAWABLE(gimp_drawable_get_by_id(seam_layer_ID)), 0, 0, w, h);
    gimp_item_set_visible(GIMP_ITEM(gimp_drawable_get_by_id(seam_layer_ID)), TRUE);
    g_object_unref(buffer_out);
    g_free(outrow);

    gimp_progress_end();

    return LQR_OK;
}

LqrRetVal
write_all_vmaps(LqrVMapList *list, gint32 image_ID, gchar *orig_name,
                gint x_off, gint y_off, GeglColor *col_start, GeglColor *col_end) {
    gchar name[LQR_MAX_NAME_LENGTH];
    VMapFuncArg data;

    /* The name of the layer with the seams map */
    /* (here "%s" represents the selected layer's name) */
    g_snprintf(name, LQR_MAX_NAME_LENGTH, _("%s seam map"), orig_name);


    data.image_ID = image_ID;
    data.name = name;
    data.x_off = x_off;
    data.y_off = y_off;
    data.colour_start = col_start;
    data.colour_end = col_end;
    data.vmap_layer_ID_p = NULL;

    return lqr_vmap_list_foreach(list, write_vmap_to_layer,
                                 (gpointer) (&data));
}
