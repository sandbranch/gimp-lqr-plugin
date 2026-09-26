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


#include "config.h"
#include <stdio.h>

#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>
#include <glib-object.h>
#include <lqr.h>

#include "altsizeentry.h"
#include "plugin-intl.h"

#include "main.h"
#include "interface.h"
#include "render.h"
#include "interface_I.h"
#include "interface_aux.h"
#include "defaults.h"

/* Local function prototypes */
static gint32           layer_from_name                    (gint32 image_ID,
                                                            gchar *name);
static void             set_aux_layer_name                 (GimpLayer *layer,
                                                            gboolean status,
                                                            gchar *name);
static gint32           aux_layer_from_config              (GimpLayer *layer,
                                                            GimpImage *image,
                                                            gchar *name);
static void             save_vals                          (GimpProcedureConfig *config);
static void             read_vals                          (GimpProcedureConfig *config,
                                                            GimpImage *image);
static const gchar     *unsupported_layer                  (gint32 layer_ID);
static void             install_custom_signals             (void);
static void             cancel_work_on_aux_layer           (void);
static GList           *lqr_query_procedures               (GimpPlugIn *plug_in);
static gboolean         lqr_set_i18n                       (GimpPlugIn *plug_in,
                                                            const gchar *procedure_name,
                                                            gchar **gettext_domain,
                                                            gchar **catalog_dir);
static GimpProcedure   *lqr_create_procedure               (GimpPlugIn *plug_in,
                                                            const gchar *name);
static GimpValueArray  *lqr_run                            (GimpProcedure *procedure,
                                                            GimpRunMode run_mode,
                                                            GimpImage *image,
                                                            GimpDrawable **drawables,
                                                            GimpProcedureConfig *config,
                                                            gpointer run_data);
#if defined(G_OS_WIN32)
static gchar           *get_gimp_share_directory_on_windows(void);
#endif

/*  Local variables  */


GeglColor *default_pres_col = NULL;
GeglColor *default_disc_col = NULL;
GeglColor *default_rigmask_col = NULL;
GeglColor *default_gray_col = NULL;

/* Initialize default colors */
static void
initialize_default_colors(void) {
    if (!default_pres_col) {
        default_pres_col = gegl_color_new("rgb(0.0, 1.0, 0.0)");
    }
    if (!default_disc_col) {
        default_disc_col = gegl_color_new("rgb(1.0, 0.0, 0.0)");
    }
    if (!default_rigmask_col) {
        default_rigmask_col = gegl_color_new("rgb(0.0, 0.0, 1.0)");
    }
    if (!default_gray_col) {
        default_gray_col = gegl_color_new("rgb(0.333333, 0.333333, 0.333333)");
    }
}

static PlugInVals vals;
static PlugInImageVals image_vals;
static PlugInDrawableVals drawable_vals;
static PlugInUIVals ui_vals;
static PlugInColVals col_vals;
static PlugInDialogVals dialog_vals;


/* Modern GIMP 3.0 plugin class */
typedef struct _LqrPlugin LqrPlugin;
typedef struct _LqrPluginClass LqrPluginClass;

struct _LqrPlugin {
    GimpPlugIn parent_instance;
};

struct _LqrPluginClass {
    GimpPlugInClass parent_class;
};

#define LQR_TYPE_PLUGIN  (lqr_plugin_get_type ())
#define LQR_PLUGIN(obj)  (G_TYPE_CHECK_INSTANCE_CAST ((obj), LQR_TYPE_PLUGIN, LqrPlugin))


G_DEFINE_TYPE (LqrPlugin, lqr_plugin, GIMP_TYPE_PLUG_IN)

GIMP_MAIN (LQR_TYPE_PLUGIN)

static void
lqr_plugin_class_init(LqrPluginClass *klass) {
    GimpPlugInClass *plug_in_class = GIMP_PLUG_IN_CLASS(klass);

    plug_in_class->query_procedures = lqr_query_procedures;
    plug_in_class->create_procedure = lqr_create_procedure;
    plug_in_class->set_i18n = lqr_set_i18n;
}

/* The translations are installed as locale/<language>/LC_MESSAGES/
   GETTEXT_PACKAGE.mo next to the plug-in; libgimp binds the domain before
   the procedures are queried or run, and translates the menu label with it */
static gboolean
lqr_set_i18n(GimpPlugIn *plug_in,
             const gchar *procedure_name,
             gchar **gettext_domain,
             gchar **catalog_dir) {
    *gettext_domain = g_strdup(GETTEXT_PACKAGE);
    return TRUE;
}

static void
lqr_plugin_init(LqrPlugin *lqr) {

}

static GList *
lqr_query_procedures(GimpPlugIn *plug_in) {
    /* the manual, installed next to the plug-in as help/<language> */
    gchar *dir = g_path_get_dirname(gimp_get_progname());
    gchar *help = g_build_filename(dir, "help", NULL);
    GFile *help_file = g_file_new_for_path(help);

    gimp_plug_in_set_help_domain(plug_in, "plug-in-lqr-help", help_file);

    g_object_unref(help_file);
    g_free(help);
    g_free(dir);

    return g_list_append(NULL, g_strdup (PLUG_IN_NAME));
}

static GimpProcedure *
lqr_create_procedure(GimpPlugIn *plug_in,
                     const gchar *name) {

    GimpProcedure *procedure = NULL;

    if (g_strcmp0(name, PLUG_IN_NAME) == 0) {
        procedure = gimp_image_procedure_new(plug_in, name,
                                             GIMP_PDB_PROC_TYPE_PLUGIN,
                                             lqr_run, NULL, NULL);

        gimp_procedure_set_image_types(procedure, "RGB*, GRAY*");
        gimp_procedure_set_sensitivity_mask(procedure,
                                            GIMP_PROCEDURE_SENSITIVE_DRAWABLE);

        gimp_procedure_set_menu_label(procedure, N_("Li_quid rescale..."));
        gimp_procedure_add_menu_path(procedure, "<Image>/Layer/");

        gimp_procedure_set_documentation(procedure,
                                         N_("scaling which keeps layer features (or removes them)"),
                                         "Resize a layer preserving (or removing) content",
                                         name);
        gimp_procedure_set_attribution(procedure,
                                       "Carlo Baldassi <carlobaldassi@gmail.com>",
                                       "Carlo Baldassi <carlobaldassi@gmail.com>",
                                       "2010");

        /* Add arguments */
        gimp_procedure_add_int_argument(procedure, "width",
                                        "Final width",
                                        "Final width",
                                        1, GIMP_MAX_IMAGE_SIZE, 100,
                                        G_PARAM_READWRITE);

        gimp_procedure_add_int_argument(procedure, "height",
                                        "Final height",
                                        "Final height",
                                        1, GIMP_MAX_IMAGE_SIZE, 100,
                                        G_PARAM_READWRITE);

        gimp_procedure_add_layer_argument(procedure, "pres_layer",
                                          "Preservation layer",
                                          "Layer that marks preserved areas",
                                          TRUE,
                                          G_PARAM_READWRITE);

        gimp_procedure_add_int_argument(procedure, "pres_coeff",
                                        "Preservation coefficient",
                                        "Preservation coefficient",
                                        0, 10000, 1000,
                                        G_PARAM_READWRITE);

        gimp_procedure_add_layer_argument(procedure, "disc_layer",
                                          "Discard layer",
                                          "Layer that marks areas to discard",
                                          TRUE,
                                          G_PARAM_READWRITE);

        gimp_procedure_add_int_argument(procedure, "disc_coeff",
                                        "Discard coefficient",
                                        "Discard coefficient",
                                        0, 10000, 1000,
                                        G_PARAM_READWRITE);

        gimp_procedure_add_double_argument(procedure, "rigidity",
                                           "Rigidity coefficient",
                                           "Rigidity coefficient",
                                           0.0, 1000.0, 0.0,
                                           G_PARAM_READWRITE);

        gimp_procedure_add_layer_argument(procedure, "rigidity_mask_layer",
                                          "Rigidity mask layer",
                                          "Layer used as rigidity mask",
                                          TRUE,
                                          G_PARAM_READWRITE);

        gimp_procedure_add_int_argument(procedure, "delta_x",
                                        "Max displacement",
                                        "max displacement of seams",
                                        0, 100, 1,
                                        G_PARAM_READWRITE);

        gimp_procedure_add_double_argument(procedure, "enl_step",
                                           "Enlargement step",
                                           "Maximum enlargement per step, in percent "
                                           "(liblqr needs more than 100 and at most 200)",
                                           100.1, 200.0, 150.0,
                                           G_PARAM_READWRITE);

        gimp_procedure_add_boolean_argument(procedure, "resize_aux_layers",
                                            "Resize auxiliary layers",
                                            "Whether to resize auxiliary layers",
                                            TRUE,
                                            G_PARAM_READWRITE);

        gimp_procedure_add_boolean_argument(procedure, "resize_canvas",
                                            "Resize canvas",
                                            "Whether to resize canvas",
                                            TRUE,
                                            G_PARAM_READWRITE);

        gimp_procedure_add_int_argument(procedure, "output_target",
                                        "Output target",
                                        "Output target (0: same layer, 1: new layer, 2: new image)",
                                        0, 2, 0,
                                        G_PARAM_READWRITE);

        gimp_procedure_add_boolean_argument(procedure, "seams",
                                            "Output seams",
                                            "Whether to output the seam map",
                                            FALSE,
                                            G_PARAM_READWRITE);

        gimp_procedure_add_int_argument(procedure, "nrg_func",
                                        "Energy function",
                                        "Energy function to use (0: gradient norm, 1: sum of absolute "
                                        "gradients, 2: transversal gradient, 3 to 5: the same with luma, "
                                        "6: null)",
                                        LQR_EF_GRAD_NORM, LQR_EF_NULL, LQR_EF_GRAD_XABS,
                                        G_PARAM_READWRITE);

        gimp_procedure_add_int_argument(procedure, "res_order",
                                        "Resize order",
                                        "Resize order (0: horizontal first, 1: vertical first)",
                                        0, 1, 0,
                                        G_PARAM_READWRITE);

        gimp_procedure_add_int_argument(procedure, "mask_behavior",
                                        "Mask behavior",
                                        "What to do with a layer mask (0: apply, 1: discard)",
                                        GIMP_MASK_APPLY, GIMP_MASK_DISCARD, GIMP_MASK_APPLY,
                                        G_PARAM_READWRITE);

        gimp_procedure_add_boolean_argument(procedure, "scaleback",
                                            "Scale back",
                                            "Whether to scale back when done",
                                            FALSE,
                                            G_PARAM_READWRITE);

        gimp_procedure_add_int_argument(procedure, "scaleback_mode",
                                        "Scale back mode",
                                        "Scale back mode (0: liquid rescale, 1: standard scaling, "
                                        "2: width only, 3: height only (uniform scaling))",
                                        SCALEBACK_MODE_LQRBACK, SCALEBACK_MODE_STDH, SCALEBACK_MODE_LQRBACK,
                                        G_PARAM_READWRITE);

        gimp_procedure_add_boolean_argument(procedure, "no_disc_on_enlarge",
                                            "No discard on enlarge",
                                            "Ignore discard layer upon enlargement",
                                            TRUE,
                                            G_PARAM_READWRITE);

        gimp_procedure_add_string_argument(procedure, "pres_layer_name",
                                           "Preservation layer name",
                                           "Preservation layer name (for noninteractive mode only)",
                                           "",
                                           G_PARAM_READWRITE);

        gimp_procedure_add_string_argument(procedure, "disc_layer_name",
                                           "Discard layer name",
                                           "Discard layer name (for noninteractive mode only)",
                                           "",
                                           G_PARAM_READWRITE);

        gimp_procedure_add_string_argument(procedure, "rigmask_layer_name",
                                           "Rigidity mask layer name",
                                           "Rigidity mask layer name (for noninteractive mode only)",
                                           "",
                                           G_PARAM_READWRITE);

        gimp_procedure_add_string_argument(procedure, "selected_layer_name",
                                           "Selected layer name",
                                           "Selected layer name (for noninteractive mode only)",
                                           "",
                                           G_PARAM_READWRITE);
    }

    return procedure;
}


static GimpValueArray *
lqr_run(
        GimpProcedure *procedure,
        GimpRunMode run_mode,
        GimpImage *image,
        GimpDrawable **drawables,
        GimpProcedureConfig *config,
        gpointer run_data
) {
    GimpDrawable *drawable;

    GimpPDBStatusType status = GIMP_PDB_SUCCESS;
    GError *error = NULL;
    const gchar *problem;
    gint32 layer_ID;
    gint32 image_ID;

    gboolean run_dialog = TRUE;
    gboolean run_render = TRUE;
    gint dialog_resp;
    gint dialog_I_resp;
    gint dialog_aux_resp;
    gboolean render_success = FALSE;

    /* Initialize default colors */
    initialize_default_colors();

    /*  Initialize with default values  */
    vals = default_vals;
    image_vals = default_image_vals;
    drawable_vals = default_drawable_vals;
    ui_vals = default_ui_vals;
    col_vals = default_col_vals;
    dialog_vals = default_dialog_vals;

    /* The first of the selected drawables; for a channel or no drawable,
       the first selected layer */
    drawable = (drawables != NULL) ? drawables[0] : NULL;
    if ((drawable != NULL) && gimp_item_is_channel(GIMP_ITEM(drawable))) {
        gimp_image_unset_active_channel(image);
    }
    if ((drawable == NULL) || !gimp_item_is_layer(GIMP_ITEM(drawable))) {
        GimpLayer **selected_layers;

        drawable = NULL;
        selected_layers = gimp_image_get_selected_layers(image);
        if (selected_layers && selected_layers[0] != NULL)
            drawable = GIMP_DRAWABLE(selected_layers[0]);
        g_free(selected_layers);
    }
    if (drawable == NULL) {
        error = g_error_new_literal(GIMP_PLUG_IN_ERROR, 0, _("No layer to rescale"));
        return gimp_procedure_new_return_values(procedure, GIMP_PDB_CALLING_ERROR, error);
    }

    layer_ID = gimp_item_get_id(GIMP_ITEM(drawable));
    image_ID = gimp_image_get_id(image);

    image_vals.image_ID = image_ID;
    drawable_vals.layer_ID = layer_ID;

    switch (run_mode) {
        case GIMP_RUN_NONINTERACTIVE:
        case GIMP_RUN_WITH_LAST_VALS:
            read_vals(config, image);
            problem = unsupported_layer(drawable_vals.layer_ID);
            if (problem) {
                error = g_error_new_literal(GIMP_PLUG_IN_ERROR, 0, problem);
                return gimp_procedure_new_return_values(procedure, GIMP_PDB_EXECUTION_ERROR, error);
            }
            break;

        case GIMP_RUN_INTERACTIVE:
            problem = unsupported_layer(drawable_vals.layer_ID);
            if (problem) {
                g_message("%s", problem);
                return gimp_procedure_new_return_values(procedure, GIMP_PDB_EXECUTION_ERROR, NULL);
            }

            /* the values of the last run, which GIMP keeps in the config */
            read_vals(config, image);
            vals.selected_layer_name[0] = '\0';
            drawable_vals.layer_ID = layer_ID;

            install_custom_signals();

            while (run_dialog == TRUE) {
                dialog_resp = dialog(image,
                                     drawables,
                                     &image_vals,
                                     &drawable_vals,
                                     &vals,
                                     &ui_vals,
                                     &col_vals,
                                     &dialog_vals
                );
                switch (dialog_resp) {

                    case GTK_RESPONSE_OK:
                        run_dialog = FALSE;
                        break;

                    case RESPONSE_RESET:
                        vals = default_vals;
                        ui_vals = default_ui_vals;
                        col_vals = default_col_vals;
                        break;

                    case RESPONSE_INTERACTIVE:
                        dialog_I_resp = dialog_I(
                                image,
                                drawables,
                                &image_vals,
                                &drawable_vals,
                                &vals,
                                &ui_vals,
                                &col_vals,
                                &dialog_vals
                        );
                        switch (dialog_I_resp) {
                            case GTK_RESPONSE_OK:
                                run_dialog = FALSE;
                                run_render = FALSE;
                                break;
                            case RESPONSE_NONINTERACTIVE:
                                save_vals(config);
                                run_dialog = TRUE;
                                break;
                            default:
                                run_dialog = FALSE;
                                run_render = FALSE;
                                status = GIMP_PDB_CANCEL;
                                break;
                        }
                        break;
                    case RESPONSE_WORK_ON_AUX_LAYER:
                        dialog_aux_resp = dialog_aux(
                                image,
                                drawables,
                                &image_vals,
                                &drawable_vals,
                                &vals,
                                &ui_vals,
                                &col_vals,
                                &dialog_vals);
                        switch (dialog_aux_resp) {
                            case GTK_RESPONSE_OK:
                                break;
                            default:
                                cancel_work_on_aux_layer();
                                run_dialog = FALSE;
                                run_render = FALSE;
                                status = GIMP_PDB_CANCEL;
                                break;
                        }
                        break;
                    case RESPONSE_FATAL:
                        run_dialog = FALSE;
                        status = GIMP_PDB_CALLING_ERROR;
                        break;
                    default:
                        run_dialog = FALSE;
                        status = GIMP_PDB_CANCEL;
                        break;
                }
            }
            break;

        default:
            break;
    }

    image_ID = image_vals.image_ID;
    layer_ID = drawable_vals.layer_ID;

    if (status == GIMP_PDB_SUCCESS) {
        IMAGE_CHECK (image_ID, gimp_procedure_new_return_values(procedure, GIMP_PDB_EXECUTION_ERROR, NULL));
        image = gimp_image_get_by_id(image_ID);
        AUX_LAYER_STATUS(vals.pres_layer_ID, ui_vals.pres_status);
        AUX_LAYER_STATUS(vals.disc_layer_ID, ui_vals.disc_status);
        AUX_LAYER_STATUS(vals.rigmask_layer_ID, ui_vals.rigmask_status);
        ui_vals.last_used_width = vals.new_width;
        ui_vals.last_used_height = vals.new_height;
        ui_vals.last_layer_ID = layer_ID;
        gimp_image_undo_group_start(image);
        render_success = TRUE;
        if (run_render) {
            CarverData *carver_data;

            render_success = FALSE;
            carver_data = render_init_carver(
                    &image_vals,
                    &drawable_vals,
                    &vals,
                    FALSE);
            if (carver_data) {
                GimpImage *target = gimp_image_get_by_id(carver_data->image_ID);

                /* the output goes to a new image, which has its own undo */
                if (target != image) {
                    gimp_image_undo_group_end(image);
                    image = target;
                    gimp_image_undo_group_start(image);
                }
                render_success = render_noninteractive(&vals, &col_vals, carver_data);
                lqr_carver_destroy(carver_data->carver);
                free(carver_data);
            }
        }

        if (run_mode != GIMP_RUN_NONINTERACTIVE)
            gimp_displays_flush();

        if ((run_mode == GIMP_RUN_INTERACTIVE) && render_success) {
            save_vals(config);
        }

        gimp_image_undo_group_end(image);

        if (!render_success)
            status = GIMP_PDB_EXECUTION_ERROR;
    }

    return gimp_procedure_new_return_values(procedure, status, NULL);
}

/* Why the layer cannot be rescaled, or NULL */
static const gchar *
unsupported_layer(gint32 layer_ID) {
    GimpLayer *layer = gimp_layer_get_by_id(layer_ID);

    if (layer == NULL)
        return _("Error: invalid layer");
    if (gimp_item_is_group(GIMP_ITEM(layer)))
        return _("Layer groups cannot be rescaled: select a layer");
    if (gimp_drawable_is_indexed(GIMP_DRAWABLE(layer)))
        return _("Indexed images cannot be rescaled: convert the image to RGB or grayscale");
    return NULL;
}

static gint32
layer_from_name(gint32 image_ID, gchar *name) {
    gint i;
    GimpLayer **layers;

    if ((name == NULL) || (strncmp(name, "", VALS_MAX_NAME_LENGTH) == 0)) {
        return 0;
    }

    GimpImage *image = gimp_image_get_by_id(image_ID);
    if (!image)
        return 0;

    layers = gimp_image_get_layers(image);
    if (!layers)
        return 0;

    for (i = 0; layers[i] != NULL; i++) {
        if (strncmp(name, item_get_name(GIMP_ITEM(layers[i])), VALS_MAX_NAME_LENGTH) == 0) {
            gint32 layer_id = gimp_item_get_id(GIMP_ITEM(layers[i]));
            g_free(layers);
            return layer_id;
        }
    }
    g_free(layers);
    return 0;
}

static void
set_aux_layer_name(GimpLayer *layer, gboolean status, gchar *name) {
    if ((layer == NULL) || (status == FALSE)) {
        name[0] = '\0';
    } else {
        g_strlcpy(name, item_get_name(GIMP_ITEM(layer)), VALS_MAX_NAME_LENGTH);
    }
}

/* An auxiliary layer of the config: the layer itself if it is one of the
   image, otherwise the layer of the image with the given name, or 0 */
static gint32
aux_layer_from_config(GimpLayer *layer, GimpImage *image, gchar *name) {
    gint32 layer_ID = 0;

    if ((layer != NULL) && (gimp_item_get_image(GIMP_ITEM(layer)) == image)) {
        layer_ID = gimp_item_get_id(GIMP_ITEM(layer));
    }
    if (layer_ID == 0) {
        layer_ID = layer_from_name(gimp_image_get_id(image), name);
    }
    /* the pixels of a layer group cannot be written */
    if ((layer_ID != 0) && gimp_item_is_group(GIMP_ITEM(gimp_layer_get_by_id(layer_ID)))) {
        layer_ID = 0;
    }
    return layer_ID;
}

/* Stores the values in the config, which GIMP keeps for the next
   interactive run and for "Repeat" (run with the last values) */
static void
save_vals(GimpProcedureConfig *config) {
    GimpLayer *pres_layer = ui_vals.pres_status ? gimp_layer_get_by_id(vals.pres_layer_ID) : NULL;
    GimpLayer *disc_layer = ui_vals.disc_status ? gimp_layer_get_by_id(vals.disc_layer_ID) : NULL;
    GimpLayer *rigmask_layer = ui_vals.rigmask_status ? gimp_layer_get_by_id(vals.rigmask_layer_ID) : NULL;

    set_aux_layer_name(pres_layer, ui_vals.pres_status, vals.pres_layer_name);
    set_aux_layer_name(disc_layer, ui_vals.disc_status, vals.disc_layer_name);
    set_aux_layer_name(rigmask_layer, ui_vals.rigmask_status, vals.rigmask_layer_name);

    g_object_set(config,
                 "width", vals.new_width,
                 "height", vals.new_height,
                 "pres-layer", pres_layer,
                 "pres-coeff", vals.pres_coeff,
                 "disc-layer", disc_layer,
                 "disc-coeff", vals.disc_coeff,
                 "rigidity", (gdouble) vals.rigidity,
                 "rigidity-mask-layer", rigmask_layer,
                 "delta-x", vals.delta_x,
                 "enl-step", (gdouble) vals.enl_step,
                 "resize-aux-layers", vals.resize_aux_layers,
                 "resize-canvas", vals.resize_canvas,
                 "output-target", vals.output_target,
                 "seams", vals.output_seams,
                 "nrg-func", vals.nrg_func,
                 "res-order", vals.res_order,
                 "mask-behavior", vals.mask_behavior,
                 "scaleback", vals.scaleback,
                 "scaleback-mode", vals.scaleback_mode,
                 "no-disc-on-enlarge", vals.no_disc_on_enlarge,
                 "pres-layer-name", vals.pres_layer_name,
                 "disc-layer-name", vals.disc_layer_name,
                 "rigmask-layer-name", vals.rigmask_layer_name,
                 "selected-layer-name", "",
                 NULL);
}

static void
copy_name(gchar *dest, gchar *src) {
    g_strlcpy(dest, src ? src : "", VALS_MAX_NAME_LENGTH);
    g_free(src);
}

/* Reads the values from the config: the arguments of a non-interactive
   call, or the values of the last run */
static void
read_vals(GimpProcedureConfig *config, GimpImage *image) {
    gint32 image_ID;
    gint32 aux_selected_layer_ID;
    gdouble rigidity, enl_step;
    gchar *pres_layer_name, *disc_layer_name, *rigmask_layer_name, *selected_layer_name;
    GimpLayer *pres_layer = NULL;
    GimpLayer *disc_layer = NULL;
    GimpLayer *rigmask_layer = NULL;

    image_ID = gimp_image_get_id(image);

    /* the arguments are gint, gboolean or gdouble; strings and layers are
       returned as new copies and references */
    g_object_get(config,
                 "width", &vals.new_width,
                 "height", &vals.new_height,
                 "pres-coeff", &vals.pres_coeff,
                 "disc-coeff", &vals.disc_coeff,
                 "rigidity", &rigidity,
                 "delta-x", &vals.delta_x,
                 "enl-step", &enl_step,
                 "resize-aux-layers", &vals.resize_aux_layers,
                 "resize-canvas", &vals.resize_canvas,
                 "output-target", &vals.output_target,
                 "seams", &vals.output_seams,
                 "nrg-func", &vals.nrg_func,
                 "res-order", &vals.res_order,
                 "mask-behavior", &vals.mask_behavior,
                 "scaleback", &vals.scaleback,
                 "scaleback-mode", &vals.scaleback_mode,
                 "no-disc-on-enlarge", &vals.no_disc_on_enlarge,
                 "pres-layer-name", &pres_layer_name,
                 "disc-layer-name", &disc_layer_name,
                 "rigmask-layer-name", &rigmask_layer_name,
                 "selected-layer-name", &selected_layer_name,
                 "pres-layer", &pres_layer,
                 "disc-layer", &disc_layer,
                 "rigidity-mask-layer", &rigmask_layer,
                 NULL);

    vals.rigidity = rigidity;
    vals.enl_step = enl_step;
    copy_name(vals.pres_layer_name, pres_layer_name);
    copy_name(vals.disc_layer_name, disc_layer_name);
    copy_name(vals.rigmask_layer_name, rigmask_layer_name);
    copy_name(vals.selected_layer_name, selected_layer_name);

    vals.pres_layer_ID = aux_layer_from_config(pres_layer, image, vals.pres_layer_name);
    vals.disc_layer_ID = aux_layer_from_config(disc_layer, image, vals.disc_layer_name);
    vals.rigmask_layer_ID = aux_layer_from_config(rigmask_layer, image, vals.rigmask_layer_name);
    g_clear_object(&pres_layer);
    g_clear_object(&disc_layer);
    g_clear_object(&rigmask_layer);

    /* the layer to rescale can be given by name */
    aux_selected_layer_ID = layer_from_name(image_ID, vals.selected_layer_name);
    if (aux_selected_layer_ID) {
        drawable_vals.layer_ID = aux_selected_layer_ID;
    }

    /* Update status flags */
    ui_vals.pres_status = (vals.pres_layer_ID != 0);
    ui_vals.disc_status = (vals.disc_layer_ID != 0);
    ui_vals.rigmask_status = (vals.rigmask_layer_ID != 0);
}

static void
install_custom_signals() {
    /* Install a new signal needed by interface_I */
    g_signal_newv("coordinates-alarm", ALT_TYPE_SIZE_ENTRY, G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                  0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, NULL);
}

static void
cancel_work_on_aux_layer(void) {
    if (!gimp_image_is_valid_id(image_vals.image_ID)) {
        return;
    }
    gimp_image_set_active_layer_id(image_vals.image_ID, drawable_vals.layer_ID);
    if (ui_vals.layer_on_edit_is_new && gimp_drawable_is_valid_id(ui_vals.layer_on_edit_ID)) {
        gimp_image_remove_layer_id(image_vals.image_ID, ui_vals.layer_on_edit_ID);
    }
    gimp_displays_flush();
}

#if defined(G_OS_WIN32)
static gchar *
get_gimp_share_directory_on_windows()
{
  gchar ** tokens;
  gchar ** tokens2;
  gchar * str;
  gchar * ret;
  gint ind = 0;
  gint ind2;
  gboolean found = FALSE;

  tokens = g_strsplit(gimp_data_directory(), "\\", 1000);

  for (ind = 0; ind < 999; ++ind)
    {
      if (tokens[ind] == NULL)
        {
          break;
        }
      str = g_ascii_strdown(tokens[ind], -1);

      if (g_strcmp0(str, "share") == 0)
        {
          found = TRUE;
        }
      g_free(str);
      if (found)
        {
          break;
        }
    }

  if (!found)
    {
      g_message("GIMP share directory not found, resorting to default\n"); 
      ret = g_strdup_printf("C:\\Program Files\\GIMP-2.0\\share");
      return ret;
    }

  tokens2 = g_new(gchar*, ind + 2);
  for (ind2 = 0; ind2 <= ind; ++ind2)
    {
      tokens2[ind2] = g_strdup(tokens[ind2]);
    }
  tokens2[ind + 1] = NULL;
  g_strfreev(tokens);

  ret = g_strjoinv("\\", tokens2);

  g_strfreev(tokens2);
  if (!g_file_test(ret, G_FILE_TEST_IS_DIR))
    {
      g_message("GIMP share directory found but test for it failed, resorting to default\n"); 
      g_free(ret);
      ret = g_strdup_printf("C:\\Program Files\\GIMP-2.0\\share");
    }

  return ret;
}
#endif
