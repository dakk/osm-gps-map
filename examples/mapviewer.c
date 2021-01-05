/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4 -*- */
/* vim:set et sw=4 ts=4 cino=t0,(0: */
/*
 * main.c
 * Copyright (C) John Stowers 2008 <john.stowers@gmail.com>
 *
 * This is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <math.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "osm-gps-map.h"

static gboolean opt_debug = FALSE;
static GOptionEntry entries[] =
{
  { "debug", 'd', 0, G_OPTION_ARG_NONE, &opt_debug, "Enable debugging", NULL },
  { NULL }
};

static GdkPixbuf *g_star_image = NULL;
static OsmGpsMapImage *g_last_image = NULL;

static gboolean
on_button_press_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    OsmGpsMapPoint coord;
    float lat, lon;
    OsmGpsMap *map = OSM_GPS_MAP(widget);

    int left_button =   (event->button == 1) && (event->state == 0);
    int middle_button = (event->button == 2) || ((event->button == 1) && (event->state & GDK_SHIFT_MASK));
    int right_button =  (event->button == 3) || ((event->button == 1) && (event->state & GDK_CONTROL_MASK));

    osm_gps_map_convert_screen_to_geographic(map, event->x, event->y, &coord);
    osm_gps_map_point_get_degrees(&coord, &lat, &lon);

    if (event->type == GDK_3BUTTON_PRESS) {
        if (middle_button) {
            if (g_last_image)
                osm_gps_map_image_remove (map, g_last_image);
        }
    } else if (event->type == GDK_2BUTTON_PRESS) {
        if (left_button) {
            osm_gps_map_gps_add (map,
                                 lat,
                                 lon,
                                 g_random_double_range(0,360));
        }
        if (middle_button) {
            g_last_image = osm_gps_map_image_add (map,
                                                  lat,
                                                  lon,
                                                  g_star_image);
        }

    }

    return FALSE;
}

static gboolean
on_button_release_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    float lat,lon;
    GtkEntry *entry = GTK_ENTRY(user_data);
    OsmGpsMap *map = OSM_GPS_MAP(widget);

    g_object_get(map, "latitude", &lat, "longitude", &lon, NULL);
    gchar *msg = g_strdup_printf("Map Centre: lattitude %f longitude %f",lat,lon);
    gtk_entry_set_text(entry, msg);
    g_free(msg);

    return FALSE;
}

static gboolean
on_zoom_in_clicked_event (GtkWidget *widget, gpointer user_data)
{
    int zoom;
    OsmGpsMap *map = OSM_GPS_MAP(user_data);
    g_object_get(map, "zoom", &zoom, NULL);
    osm_gps_map_set_zoom(map, zoom+1);
    return FALSE;
}

static gboolean
on_zoom_out_clicked_event (GtkWidget *widget, gpointer user_data)
{
    int zoom;
    OsmGpsMap *map = OSM_GPS_MAP(user_data);
    g_object_get(map, "zoom", &zoom, NULL);
    osm_gps_map_set_zoom(map, zoom-1);
    return FALSE;
}

static gboolean
on_home_clicked_event (GtkWidget *widget, gpointer user_data)
{
    OsmGpsMap *map = OSM_GPS_MAP(user_data);
    osm_gps_map_set_center_and_zoom(map, -43.5326,172.6362,12);
    return FALSE;
}

static void
on_tiles_queued_changed (OsmGpsMap *image, GParamSpec *pspec, gpointer user_data)
{
    gchar *s;
    int tiles;
    GtkLabel *label = GTK_LABEL(user_data);
    g_object_get(image, "tiles-queued", &tiles, NULL);
    s = g_strdup_printf("%d", tiles);
    gtk_label_set_text(label, s);
    g_free(s);
}

static void
on_gps_alpha_changed (GtkAdjustment *adjustment, gpointer user_data)
{}

static void
on_gps_width_changed (GtkAdjustment *adjustment, gpointer user_data)
{}

static void
on_star_align_changed (GtkAdjustment *adjustment, gpointer user_data)
{
    const char *propname = user_data;
    float f = gtk_adjustment_get_value(adjustment);
    if (g_last_image)
        g_object_set (g_last_image, propname, f, NULL);
}

#if GTK_CHECK_VERSION(3,4,0)
static void
on_gps_color_changed (GtkColorChooser *widget, gpointer user_data)
{}
#else
static void
on_gps_color_changed (GtkColorButton *widget, gpointer user_data)
{}

#endif

static void
on_close (GtkWidget *widget, gpointer user_data)
{
    gtk_widget_destroy(widget);
    gtk_main_quit();
}

static void
usage (GOptionContext *context)
{
    int i;

    puts(g_option_context_get_help(context, TRUE, NULL));
}

int
main (int argc, char **argv)
{
    GtkBuilder *builder;
    GtkWidget *widget;
    GtkAccelGroup *ag;
    OsmGpsMap *map;
    OsmGpsMapLayer *osd;
    GError *error = NULL;
    GOptionContext *context;

    gtk_init (&argc, &argv);

    context = g_option_context_new ("- Map browser");
    g_option_context_set_help_enabled(context, FALSE);
    g_option_context_add_main_entries (context, entries, NULL);

    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        usage(context);
        return 1;
    }


    if (opt_debug)
        gdk_window_set_debug_updates(TRUE);

    map = g_object_new (OSM_TYPE_GPS_MAP,
                        NULL);

    osd = g_object_new (OSM_TYPE_GPS_MAP_OSD,
                        "show-scale",TRUE,
                        "show-coordinates",TRUE,
                        "show-crosshair",TRUE,
                        "show-dpad",TRUE,
                        "show-zoom",TRUE,
                        "show-gps-in-dpad",TRUE,
                        "show-gps-in-zoom",FALSE,
                        "dpad-radius", 30,
                        NULL);
    osm_gps_map_layer_add(OSM_GPS_MAP(map), osd);
    g_object_unref(G_OBJECT(osd));

    //Enable keyboard navigation
    osm_gps_map_set_keyboard_shortcut(map, OSM_GPS_MAP_KEY_FULLSCREEN, GDK_KEY_F11);
    osm_gps_map_set_keyboard_shortcut(map, OSM_GPS_MAP_KEY_UP, GDK_KEY_Up);
    osm_gps_map_set_keyboard_shortcut(map, OSM_GPS_MAP_KEY_DOWN, GDK_KEY_Down);
    osm_gps_map_set_keyboard_shortcut(map, OSM_GPS_MAP_KEY_LEFT, GDK_KEY_Left);
    osm_gps_map_set_keyboard_shortcut(map, OSM_GPS_MAP_KEY_RIGHT, GDK_KEY_Right);

    //Build the UI
    g_star_image = gdk_pixbuf_new_from_file_at_size ("poi.png", 24,24,NULL);

    builder = gtk_builder_new();
    gtk_builder_add_from_file (builder, "mapviewer.ui", &error);
    if (error)
        g_error ("ERROR: %s\n", error->message);

    gtk_box_pack_start (
                GTK_BOX(gtk_builder_get_object(builder, "map_box")),
                GTK_WIDGET(map), TRUE, TRUE, 0);

    //Init values
    float lw,a;
    GdkRGBA c;
    gtk_adjustment_set_value (
                GTK_ADJUSTMENT(gtk_builder_get_object(builder, "gps_width_adjustment")),
                lw);
    gtk_adjustment_set_value (
                GTK_ADJUSTMENT(gtk_builder_get_object(builder, "gps_alpha_adjustment")),
                a);
    gtk_adjustment_set_value (
                GTK_ADJUSTMENT(gtk_builder_get_object(builder, "star_xalign_adjustment")),
                0.5);
    gtk_adjustment_set_value (
                GTK_ADJUSTMENT(gtk_builder_get_object(builder, "star_yalign_adjustment")),
                0.5);
#if GTK_CHECK_VERSION(3,4,0)
    gtk_color_chooser_set_rgba (
                GTK_COLOR_CHOOSER(gtk_builder_get_object(builder, "gps_colorbutton")),
                &c);
#else
    gtk_color_button_set_rgba (
                GTK_COLOR_BUTTON(gtk_builder_get_object(builder, "gps_colorbutton")),
                &c);
#endif

    //Connect to signals
    g_signal_connect (
                gtk_builder_get_object(builder, "zoom_in_button"), "clicked",
                G_CALLBACK (on_zoom_in_clicked_event), (gpointer) map);
    g_signal_connect (
                gtk_builder_get_object(builder, "zoom_out_button"), "clicked",
                G_CALLBACK (on_zoom_out_clicked_event), (gpointer) map);
    g_signal_connect (
                gtk_builder_get_object(builder, "home_button"), "clicked",
                G_CALLBACK (on_home_clicked_event), (gpointer) map);
    g_signal_connect (
                gtk_builder_get_object(builder, "gps_alpha_adjustment"), "value-changed",
                G_CALLBACK (on_gps_alpha_changed), (gpointer) map);
    g_signal_connect (
                gtk_builder_get_object(builder, "gps_width_adjustment"), "value-changed",
                G_CALLBACK (on_gps_width_changed), (gpointer) map);
    g_signal_connect (
                gtk_builder_get_object(builder, "star_xalign_adjustment"), "value-changed",
                G_CALLBACK (on_star_align_changed), (gpointer) "x-align");
    g_signal_connect (
                gtk_builder_get_object(builder, "star_yalign_adjustment"), "value-changed",
                G_CALLBACK (on_star_align_changed), (gpointer) "y-align");
    g_signal_connect (G_OBJECT (map), "button-release-event",
                G_CALLBACK (on_button_release_event),
                (gpointer) gtk_builder_get_object(builder, "text_entry"));

    widget = GTK_WIDGET(gtk_builder_get_object(builder, "window1"));
    g_signal_connect (widget, "destroy",
                      G_CALLBACK (on_close), (gpointer) map);
    //Setup accelerators.
    ag = gtk_accel_group_new();
    gtk_accel_group_connect(ag, GDK_KEY_w, GDK_CONTROL_MASK, GTK_ACCEL_MASK,
                    g_cclosure_new(gtk_main_quit, NULL, NULL));
    gtk_accel_group_connect(ag, GDK_KEY_q, GDK_CONTROL_MASK, GTK_ACCEL_MASK,
                    g_cclosure_new(gtk_main_quit, NULL, NULL));
    gtk_window_add_accel_group(GTK_WINDOW(widget), ag);

    gtk_widget_show_all (widget);

    g_log_set_handler ("OsmGpsMap", G_LOG_LEVEL_MASK, g_log_default_handler, NULL);
    gtk_main ();

    return 0;
}
