//
// Created by tomhodder on 30/05/25.
//

#include "page_advanced.h"

extern NotebookData *notebook_data;
extern PreviewData preview_data;
extern PlugInUIVals *ui_state;
extern PlugInVals *state;
extern gboolean features_are_sensitive;
extern ToggleData pres_toggle_data;
extern ToggleData disc_toggle_data;
extern PresDiscStatus presdisc_status;
extern ToggleData rigmask_toggle_data;
extern GtkWidget *nrg_func_combo_box;
extern GtkWidget *res_order_combo_box;

/* Generate advanced options page */

/* A scale entry for one of the settings, packed into box and updating the
   setting through its adjustment, as the settings on the other pages do. */
static GtkWidget *
settings_scale_entry(GtkWidget *box, const gchar *text, gdouble value,
                     gdouble lower, gdouble upper, gdouble step, gdouble page,
                     gint digits, const gchar *tip, GCallback update,
                     gpointer setting) {
    GtkWidget *entry;
    GtkWidget *spin_button;

    entry = gimp_scale_entry_new(text, value, lower, upper, digits);
    gimp_label_spin_set_increments(GIMP_LABEL_SPIN(entry), step, page);
    gimp_help_set_help_data(entry, tip, NULL);
    gtk_box_pack_start(GTK_BOX (box), entry, FALSE, FALSE, 0);
    gtk_widget_show(entry);

    spin_button = gimp_label_spin_get_spin_button(GIMP_LABEL_SPIN(entry));
    g_signal_connect (gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(spin_button)),
                      "value-changed", update, setting);

    return entry;
}

GtkWidget *
advanced_page_new(gint32 image_ID, gint32 layer_ID) {
    gint num_extra_layers;
    GtkWidget *label;
    GtkWidget *thispage;
    GtkWidget *scrollwindow;
    gchar rigmask_inactive_tip_string[MAX_STRING_SIZE];
    NewLayerData *new_rigmask_layer_data;
    GtkWidget *rigmask_frame_event_box1;
    GtkWidget *rigmask_frame_event_box2;
    GtkWidget *rigmask_combo_event_box;
    gint32 old_layer_ID;
    GtkWidget *seams_control_expander;
    GtkWidget *operations_expander;
    GtkWidget *rigmask_vbox;
    GtkWidget *rigmask_vbox2;
    GtkWidget *hbox;
    GtkWidget *new_hbox;
    GtkWidget *new_icon;
    GtkWidget *new_label;
    GtkWidget *edit_hbox;
    GtkWidget *edit_icon;
    GtkWidget *edit_label;
    GtkWidget *rigmask_button;
    GtkWidget *rigmask_new_button;
    GtkWidget *rigmask_edit_button;
    GtkWidget *operations_vbox;
    GtkWidget *no_disc_on_enlarge_button;
    GtkWidget *rigmask_combo_grid;
    GtkWidget *rigmask_layer_label;
    GtkWidget *combo;

    GtkWidget *nrg_event_box;
    GtkWidget *res_order_event_box;

    GimpLayer *layer = gimp_layer_get_by_id(layer_ID);

    label = gtk_label_new(_("Advanced"));
    notebook_data->label = label;

    new_rigmask_layer_data = g_new (NewLayerData, 1);

    new_rigmask_layer_data->preview_data = &preview_data;
    new_rigmask_layer_data->layer_ID = &(state->rigmask_layer_ID);
    new_rigmask_layer_data->status = &(ui_state->rigmask_status);
    /* The name of a newly created layer for rigidity mask */
    /* (here "%s" represents the selected layer's name) */
    g_snprintf(new_rigmask_layer_data->name, LQR_MAX_NAME_LENGTH,
               _("%s rigidity mask"),
               gimp_drawable_get_name_id(preview_data.orig_layer_ID));

    new_rigmask_layer_data->colour = gegl_color_new("black");
    // gimp_rgb_set(&(new_rigmask_layer_data->colour), 0, 0, 1);
    gegl_color_set_rgba(new_rigmask_layer_data->colour, 0.0, 0.0, 1.0, 1.0);

    new_rigmask_layer_data->layer_type = AUX_LAYER_RIGMASK;

    num_extra_layers = count_extra_layers(image_ID);
    features_are_sensitive = (num_extra_layers > 0 ? TRUE : FALSE);
    preview_data.rigmask_combo_awaked = FALSE;

    if (!features_are_sensitive ||
        !gimp_drawable_is_valid_id(state->rigmask_layer_ID) ||
        !gimp_drawable_is_layer_id(state->rigmask_layer_ID) ||
        (state->rigmask_layer_ID == layer_ID)) {
        ui_state->rigmask_status = FALSE;
        state->rigmask_layer_ID = 0;
    }

    thispage = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER (thispage), 12);
    gtk_widget_show(thispage);

    scrollwindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW (scrollwindow), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW (scrollwindow), thispage);

    notebook_data->advanced_page = scrollwindow;


    /*  Seams control  */

    /* Please keep the <b> and </b> tags in translations */
    seams_control_expander = gtk_expander_new(_("<b>Seams control</b>"));
    gtk_expander_set_use_markup(GTK_EXPANDER(seams_control_expander), TRUE);
    gtk_expander_set_expanded(GTK_EXPANDER(seams_control_expander), TRUE);
    g_signal_connect (seams_control_expander, "activate",
                      G_CALLBACK
                              (callback_expander_changed),
                      (gpointer) (&ui_state->seams_control_expanded));

    gtk_box_pack_start(GTK_BOX (thispage), seams_control_expander, FALSE, FALSE, 0);
    gtk_widget_show(seams_control_expander);

    g_snprintf(rigmask_inactive_tip_string, MAX_STRING_SIZE,
               _("Extra layers are needed to be used as rigidity masks.\n"
                 "You can create one with the \"New\" button and paint on it, "
                 "then press the \"Refresh\" button.\n"
                 "Note that painting in black has no effect"));

    rigmask_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER (seams_control_expander), rigmask_vbox);
    gtk_widget_show(rigmask_vbox);

    /* Delta x */
    settings_scale_entry(rigmask_vbox, _("Max transversal step:"),
                         state->delta_x, 0, MAX_DELTA_X, 1, 1, 0,
                         _("Maximum displacement along a seam. "
                           "Increasing this value allows to overcome "
                           "the 45 degrees bound"),
                         G_CALLBACK (gimp_int_adjustment_update),
                         &state->delta_x);

    /* Rigidity */
    settings_scale_entry(rigmask_vbox, _("Overall rigidity:"),
                         state->rigidity, 0, MAX_RIGIDITY, 0.2, 10, 2,
                         _("Increasing this value results "
                           "in straighter seams"),
                         G_CALLBACK (gimp_float_adjustment_update),
                         &state->rigidity);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX (rigmask_vbox), hbox, FALSE, FALSE, 0);
    gtk_widget_show(hbox);

    rigmask_frame_event_box1 = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX (rigmask_frame_event_box1),
                                     FALSE);
    gtk_box_pack_start(GTK_BOX (hbox), rigmask_frame_event_box1, FALSE, FALSE,
                       0);
    gtk_widget_show(rigmask_frame_event_box1);


    if (!features_are_sensitive) {
        gtk_event_box_set_above_child(GTK_EVENT_BOX (rigmask_frame_event_box1),
                                      TRUE);
        gtk_widget_set_tooltip_text(rigmask_frame_event_box1, rigmask_inactive_tip_string);
    }


    rigmask_button = gtk_check_button_new_with_label(_("Use a rigidity mask"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (rigmask_button),
                                 ui_state->rigmask_status);

    gtk_widget_set_sensitive(rigmask_button, features_are_sensitive);


    gtk_container_add(GTK_CONTAINER (rigmask_frame_event_box1),
                      rigmask_button);
    gtk_widget_show(rigmask_button);

    g_signal_connect (rigmask_button, "toggled",
                      G_CALLBACK
                              (callback_status_button),
                      (gpointer) (&ui_state->rigmask_status));

    g_signal_connect (rigmask_button, "toggled",
                      G_CALLBACK
                              (callback_resize_aux_layers_button_set_sensitive),
                      (gpointer) (&presdisc_status));

    callback_resize_aux_layers_button_set_sensitive(NULL,
                                                    (gpointer)
                                                            (&presdisc_status));

    gimp_help_set_help_data(rigmask_button,
                            _
                                    ("Use an extra layer to mark areas where seams should be straighter"),
                            NULL);

    rigmask_edit_button = gtk_button_new();
    gtk_box_pack_end(GTK_BOX (hbox), rigmask_edit_button, FALSE, FALSE, 0);
    gtk_widget_show(rigmask_edit_button);

    edit_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_add(GTK_CONTAINER(rigmask_edit_button), edit_hbox);
    gtk_widget_show(edit_hbox);

    edit_icon = gtk_image_new_from_stock(GTK_STOCK_EDIT, GTK_ICON_SIZE_MENU);
    gtk_box_pack_start(GTK_BOX(edit_hbox), edit_icon, TRUE, TRUE, 0);
    gtk_widget_show(edit_icon);
    edit_label = gtk_label_new(_("Edit"));
    gtk_box_pack_end(GTK_BOX(edit_hbox), edit_label, TRUE, TRUE, 0);
    gtk_widget_show(edit_label);

    gimp_help_set_help_data(rigmask_edit_button,
                            _("Edit the currently selected rigidity mask layer"),
                            NULL);

    rigmask_new_button = gtk_button_new();
    gtk_box_pack_end(GTK_BOX (hbox), rigmask_new_button, FALSE, FALSE, 0);
    gtk_widget_show(rigmask_new_button);

    new_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_add(GTK_CONTAINER(rigmask_new_button), new_hbox);
    gtk_widget_show(new_hbox);

    new_icon = gtk_image_new_from_stock(GTK_STOCK_NEW, GTK_ICON_SIZE_MENU);
    gtk_box_pack_start(GTK_BOX(new_hbox), new_icon, TRUE, TRUE, 0);
    gtk_widget_show(new_icon);
    new_label = gtk_label_new(_("New"));
    gtk_box_pack_end(GTK_BOX(new_hbox), new_label, TRUE, TRUE, 0);
    gtk_widget_show(new_label);

    gimp_help_set_help_data(rigmask_new_button,
                            _("Creates a new transparent layer "
                              "ready to be used as a rigidity mask"), NULL);

    g_signal_connect (rigmask_new_button, "clicked",
                      G_CALLBACK
                              (callback_new_mask_button),
                      (gpointer) (new_rigmask_layer_data));

    g_signal_connect (rigmask_edit_button, "clicked",
                      G_CALLBACK
                              (callback_edit_mask_button),
                      (gpointer) (new_rigmask_layer_data));


    rigmask_frame_event_box2 = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX (rigmask_frame_event_box2),
                                     FALSE);
    gtk_box_pack_start(GTK_BOX (rigmask_vbox), rigmask_frame_event_box2, FALSE,
                       FALSE, 0);
    gtk_widget_show(rigmask_frame_event_box2);


    if (!features_are_sensitive) {
        gtk_event_box_set_above_child(GTK_EVENT_BOX (rigmask_frame_event_box2),
                                      TRUE);
        gtk_widget_set_tooltip_text(rigmask_frame_event_box2, rigmask_inactive_tip_string);
    }

    rigmask_vbox2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER (rigmask_frame_event_box2), rigmask_vbox2);
    gtk_widget_show(rigmask_vbox2);


    rigmask_combo_event_box = gtk_event_box_new();
    gtk_box_pack_start(GTK_BOX (rigmask_vbox2), rigmask_combo_event_box, FALSE,
                       FALSE, 0);
    gtk_widget_show(rigmask_combo_event_box);

    if (features_are_sensitive) {
        gimp_help_set_help_data(rigmask_combo_event_box,
                                _("Layer to be used as a mask for "
                                  "rigidity settings.\n"
                                  "Use the \"Refresh\" button to update the list"),
                                NULL);
    }

    rigmask_combo_grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER (rigmask_combo_grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID (rigmask_combo_grid), 4);
    gtk_grid_set_row_spacing(GTK_GRID (rigmask_combo_grid), 2);
    gtk_container_add(GTK_CONTAINER (rigmask_combo_event_box), rigmask_combo_grid);
    gtk_widget_show(rigmask_combo_grid);

    combo =
            gimp_layer_combo_box_new(dialog_layer_constraint_func,
                                     layer,
                                     NULL);

    g_object_set(combo, "ellipsize", PANGO_ELLIPSIZE_START, NULL);

    old_layer_ID = state->rigmask_layer_ID;

    gimp_int_combo_box_connect(GIMP_INT_COMBO_BOX(combo),
                               layer_ID,
                               G_CALLBACK (callback_rigmask_combo_get_active),
                               (gpointer) (&preview_data),
                               NULL);

    gimp_int_combo_box_set_active(GIMP_INT_COMBO_BOX(combo), old_layer_ID);

    rigmask_layer_label = gtk_label_new(_("Layer:"));
    gtk_widget_set_halign(rigmask_layer_label, GTK_ALIGN_START);
    gtk_widget_set_valign(rigmask_layer_label, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(rigmask_combo_grid), rigmask_layer_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(rigmask_combo_grid), combo, 1, 0, 1, 1);
    gtk_widget_set_hexpand(combo, TRUE);
    gtk_widget_show(rigmask_layer_label);

    gtk_widget_set_sensitive(rigmask_layer_label, ui_state->rigmask_status
                                                  && features_are_sensitive);

    gtk_widget_set_sensitive(combo, ui_state->rigmask_status
                                    && features_are_sensitive);

    gtk_widget_set_sensitive(rigmask_edit_button, ui_state->rigmask_status
                                                  && features_are_sensitive);

    rigmask_toggle_data.combo = combo;
    rigmask_toggle_data.combo_label = rigmask_layer_label;
    rigmask_toggle_data.edit_button = rigmask_edit_button;
    preview_data.rigmask_combo = combo;

    gtk_widget_show(combo);

    rigmask_toggle_data.status = &(ui_state->rigmask_status);

    rigmask_toggle_data.scale = NULL;
    rigmask_toggle_data.guess_label = NULL;
    rigmask_toggle_data.guess_button_hor = NULL;
    rigmask_toggle_data.guess_button_ver = NULL;

    g_signal_connect (G_OBJECT(rigmask_button), "toggled",
                      G_CALLBACK(callback_combo_set_sensitive),
                      (gpointer) (&rigmask_toggle_data));

    g_signal_connect (G_OBJECT(rigmask_button), "toggled",
                      G_CALLBACK(callback_rigmask_combo_set_sensitive_preview),
                      (gpointer) (&preview_data));

    /* Energy function */

    nrg_event_box = gtk_event_box_new();
    gtk_box_pack_start(GTK_BOX (rigmask_vbox), nrg_event_box, FALSE, FALSE,
                       0);
    gtk_widget_show(nrg_event_box);

    gimp_help_set_help_data(nrg_event_box,
                            _
                                    ("This affects the automatic feature recognition.\n"
                                     "It's the filter which will be used to determine "
                                     "the relevance of each pixel"),
                            NULL);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER (hbox), 4);
    gtk_container_add(GTK_CONTAINER (nrg_event_box), hbox);
    gtk_widget_show(hbox);

    label = gtk_label_new(_("Feature recog.:"));
    gtk_box_pack_start(GTK_BOX (hbox), label, FALSE, FALSE, 0);
    gtk_widget_show(label);

    nrg_func_combo_box =
            gimp_int_combo_box_new(_("Transversal grad. (bright.) "), LQR_EF_GRAD_XABS,
                                   _("Grad. sum (bright.)"), LQR_EF_GRAD_SUMABS,
                                   _("Grad. norm (bright.)"), LQR_EF_GRAD_NORM,
                                   _("Transversal grad. (luma) "), LQR_EF_LUMA_GRAD_XABS,
                                   _("Grad. sum (luma)"), LQR_EF_LUMA_GRAD_SUMABS,
                                   _("Grad. norm (luma)"), LQR_EF_LUMA_GRAD_NORM,
                    /* Null can be translated as Zero */
                                   _("Null"), LQR_EF_NULL, NULL);
    gimp_int_combo_box_set_active(GIMP_INT_COMBO_BOX(nrg_func_combo_box),
                                  state->nrg_func);

    gtk_box_pack_start(GTK_BOX (hbox), nrg_func_combo_box, TRUE, TRUE, 0);
    gtk_widget_show(nrg_func_combo_box);


    /* Operations control */

    /* Please keep the <b> and </b> tags in translations */
    operations_expander = gtk_expander_new(_("<b>Operations control</b>"));
    gtk_expander_set_use_markup(GTK_EXPANDER(operations_expander), TRUE);
    gtk_expander_set_expanded(GTK_EXPANDER(operations_expander), TRUE);
    g_signal_connect (operations_expander, "activate",
                      G_CALLBACK
                              (callback_expander_changed),
                      (gpointer) (&ui_state->operations_expanded));

    gtk_box_pack_start(GTK_BOX (thispage), operations_expander, FALSE, FALSE, 0);
    gtk_widget_show(operations_expander);

    operations_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(operations_expander), operations_vbox);
    gtk_widget_show(operations_vbox);

    /* Enlargement step */

    settings_scale_entry(operations_vbox, _("Max enlargement per step:"),
                         state->enl_step, 100.1, 200, 1, 10, 1,
                         _("When enlarging beyond the value set here the "
                           "rescaling will be performed in multiple steps."),
                         G_CALLBACK (gimp_float_adjustment_update),
                         &state->enl_step);

    /* Resize order */

    res_order_event_box = gtk_event_box_new();
    gtk_box_pack_start(GTK_BOX (operations_vbox), res_order_event_box, FALSE, FALSE,
                       0);
    gtk_widget_show(res_order_event_box);

    gimp_help_set_help_data(res_order_event_box,
                            _("This controls the order of operations "
                              "if rescaling in both directions"), NULL);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER (hbox), 4);
    gtk_container_add(GTK_CONTAINER (res_order_event_box), hbox);
    gtk_widget_show(hbox);

    label = gtk_label_new(_("Rescale order:"));
    gtk_box_pack_start(GTK_BOX (hbox), label, FALSE, FALSE, 0);
    gtk_widget_show(label);

    res_order_combo_box =
            gimp_int_combo_box_new(_("Horizontal first"), LQR_RES_ORDER_HOR,
                                   _("Vertical first"), LQR_RES_ORDER_VERT, NULL);
    gimp_int_combo_box_connect(GIMP_INT_COMBO_BOX(res_order_combo_box),
                               state->res_order,
                               G_CALLBACK (callback_res_order_changed),
                               (gpointer) &preview_data,
                               NULL);

    gtk_box_pack_start(GTK_BOX (hbox), res_order_combo_box, TRUE, TRUE, 0);
    gtk_widget_show(res_order_combo_box);

    /* No discard when enlarging ? */

    no_disc_on_enlarge_button =
            gtk_check_button_new_with_label(_("Ignore discard mask when enlarging"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (no_disc_on_enlarge_button),
                                 state->no_disc_on_enlarge);

    gimp_help_set_help_data(no_disc_on_enlarge_button,
                            _
                                    ("This will have the same effect as setting the strenght "
                                     "to 0 in the discard mask when the first rescale step is "
                                     "an image enlargment (which normally is the best choice).\n"
                                     "Note that this option is ignored in interactive mode"),
                            NULL);

    gtk_box_pack_start(GTK_BOX (operations_vbox), no_disc_on_enlarge_button, FALSE,
                       FALSE, 0);
    gtk_widget_show(no_disc_on_enlarge_button);

    g_signal_connect (no_disc_on_enlarge_button, "toggled",
                      G_CALLBACK
                              (callback_status_button),
                      (gpointer) (&state->no_disc_on_enlarge));

    g_signal_connect (no_disc_on_enlarge_button, "toggled",
                      G_CALLBACK
                              (callback_set_disc_warning), (gpointer) (&preview_data));

    callback_set_disc_warning(no_disc_on_enlarge_button,
                              (gpointer) &preview_data);

    return scrollwindow;
}
