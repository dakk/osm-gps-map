/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4 -*- */
/* vim:set et sw=4 ts=4 */
/*
 * Copyright (C) 2013 John Stowers <john.stowers@gmail.com>
 * Copyright (C) Marcus Bauer 2008 <marcus.bauer@gmail.com>
 * Copyright (C) John Stowers 2009 <john.stowers@gmail.com>
 * Copyright (C) Till Harbaum 2009 <till@harbaum.org>
 *
 * Contributions by
 * Everaldo Canuto 2009 <everaldo.canuto@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:osm-gps-map
 * @short_description: The map display widget
 * @stability: Stable
 * @include: osm-gps-map.h
 *
 * #OsmGpsMap is a widget for displaying a map, optionally overlaid with a
 * track(s) of GPS co-ordinates, images, points of interest or on screen display
 * controls. #OsmGpsMap downloads (and caches for offline use) map data from a
 * number of websites, including
 * <ulink url="http://www.openstreetmap.org"><citetitle>OpenStreetMap</citetitle></ulink>
 *
 * <example>
 *  <title>Showing a map</title>
 *  <programlisting>
 * int main (int argc, char **argv)
 * {
 *     g_thread_init(NULL);
 *     gtk_init (&argc, &argv);
 *
 *     GtkWidget *map = osm_gps_map_new ();
 *     GtkWidget *w = gtk_window_new (GTK_WINDOW_TOPLEVEL);
 *     gtk_container_add (GTK_CONTAINER(w), map);
 *     gtk_widget_show_all (w);
 *
 *     gtk_main ();
 *     return 0;
 * }
 *  </programlisting>
 * </example>
 *
 * #OsmGpsMap allows great flexibility in customizing how the map tiles are
 * cached, see #OsmGpsMap:tile-cache-base and #OsmGpsMap:tile-cache for more
 * information.
 *
 * A number of different map sources are supported, see #OsmGpsMapSource_t. The
 * default source, %OSM_GPS_MAP_SOURCE_OPENSTREETMAP always works. Other sources,
 * particular those from proprietary providers may work occasionally, and then
 * cease to work. To check if a source is supported for the given version of
 * this library, call osm_gps_map_source_is_valid().
 *
 * <example>
 *  <title>Map with custom source and cache dir</title>
 *  <programlisting>
 * int main (int argc, char **argv)
 * {
 *     g_thread_init(NULL);
 *     gtk_init (&argc, &argv);
 *     OsmGpsMapSource_t source = OSM_GPS_MAP_SOURCE_VIRTUAL_EARTH_SATELLITE;
 *
 *     if ( !osm_gps_map_source_is_valid(source) )
 *         return 1;
 *
 *     GtkWidget *map = g_object_new (OSM_TYPE_GPS_MAP,
 *                      "map-source", source,
 *                      "tile-cache", "/tmp/",
 *                       NULL);
 *     GtkWidget *w = gtk_window_new (GTK_WINDOW_TOPLEVEL);
 *     gtk_container_add (GTK_CONTAINER(w), map);
 *     gtk_widget_show_all (w);
 *
 *     gtk_main ();
 *     return 0;
 * }
 *  </programlisting>
 * </example>
 *
 * Finally, if you wish to use a custom map source not supported by #OsmGpsMap,
 * such as a custom map created with
 * <ulink url="http://www.cloudmade.com"><citetitle>CloudMade</citetitle></ulink>
 * then you can also pass a specially formatted string to #OsmGpsMap:repo-uri.
 *
 * <example>
 *  <title>Map using custom CloudMade map and on screen display</title>
 *  <programlisting>
 * int main (int argc, char **argv)
 * {
 *     g_thread_init(NULL);
 *     gtk_init (&argc, &argv);
 *     const gchar *cloudmate = "http://a.tile.cloudmade.com/YOUR_API_KEY/1/256/&num;Z/&num;X/&num;Y.png";
 *
 *     GtkWidget *map = g_object_new (OSM_TYPE_GPS_MAP,
 *                      "repo-uri", cloudmate,
 *                       NULL);
 *     OsmGpsMapOsd *osd = osm_gps_map_osd_new ();
 *     GtkWidget *w = gtk_window_new (GTK_WINDOW_TOPLEVEL);
 *     osm_gps_map_layer_add (OSM_GPS_MAP(map), OSM_GPS_MAP_LAYER(osd));
 *     gtk_container_add (GTK_CONTAINER(w), map);
 *     gtk_widget_show_all (w);
 *
 *     gtk_main ();
 *     return 0;
 * }
 *  </programlisting>
 * </example>
 **/

#include "config.h"

#include <fcntl.h>
#include <math.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gdk/gdk.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gprintf.h>
#include <libsoup/soup.h>

#include "converter.h"
#include "private.h"
#include "osm-gps-map-widget.h"
#include "osm-gps-map-compat.h"

#define ENABLE_DEBUG                (0)
#define EXTRA_BORDER                (0)
#define OSM_GPS_MAP_SCROLL_STEP     (10)
#define MAX_DOWNLOAD_TILES          10000
#define DOT_RADIUS                  4.0

struct _OsmGpsMapPrivate
{

    int map_zoom;
    int max_zoom;
    int min_zoom;

    int tile_zoom_offset;

    int map_x;
    int map_y;

    /* Controls auto centering the map when a new GPS position arrives */
    gfloat map_auto_center_threshold;

    /* Latitude and longitude of the center of the map, in radians */
    gfloat center_rlat;
    gfloat center_rlon;

    /* Incremented at each redraw */
    guint redraw_cycle;
    /* ID of the idle redraw operation */
    guint idle_map_redraw;

    //gps tracking state
    GSList *trip_history;
    float gps_heading;

    OsmGpsMapPoint *gps;

    //additional images or tracks added to the map
    GSList *images;

    //Used for storing the joined tiles
    cairo_surface_t *pixmap;

    //The tile painted when one cannot be found
    GdkPixbuf *null_tile;

    //A list of OsmGpsMapLayer* layers, such as the OSD
    GSList *layers;

    //For tracking click and drag
    int drag_counter;
    int drag_mouse_dx;
    int drag_mouse_dy;
    int drag_start_mouse_x;
    int drag_start_mouse_y;
    int drag_start_map_x;
    int drag_start_map_y;
    int drag_limit;
    guint drag_expose_source;

    /* Properties for dragging a point with right mouse button. */
    OsmGpsMapPoint* drag_point;

    /* for customizing the redering of the gps track */
    int ui_gps_point_inner_radius;
    int ui_gps_point_outer_radius;

    /* For storing keybindings */
    guint keybindings[OSM_GPS_MAP_KEY_MAX];

    /* flags controlling which features are enabled */
    guint keybindings_enabled : 1;
    guint map_auto_download_enabled : 1;
    guint map_auto_center_enabled : 1;
    guint trip_history_record_enabled : 1;
    guint trip_history_show_enabled : 1;
    guint gps_point_enabled : 1;

    /* state flags */
    guint is_disposed : 1;
    guint is_constructed : 1;
    guint is_dragging : 1;
    guint is_button_down : 1;
    guint is_fullscreen : 1;
    guint is_google : 1;
    guint is_dragging_point : 1;
};


enum
{
    PROP_0,
    PROP_AUTO_CENTER,
    PROP_RECORD_TRIP_HISTORY,
    PROP_SHOW_TRIP_HISTORY,
    PROP_TILE_ZOOM_OFFSET,
    PROP_ZOOM,
    PROP_MAX_ZOOM,
    PROP_MIN_ZOOM,
    PROP_LATITUDE,
    PROP_LONGITUDE,
    PROP_MAP_X,
    PROP_MAP_Y,
    PROP_GPS_POINT_R1,
    PROP_GPS_POINT_R2,
    PROP_DRAG_LIMIT,
    PROP_AUTO_CENTER_THRESHOLD,
    PROP_SHOW_GPS_POINT
};

G_DEFINE_TYPE (OsmGpsMap, osm_gps_map, GTK_TYPE_DRAWING_AREA);



static void
my_log_handler (const gchar * log_domain, GLogLevelFlags log_level, const gchar * message, gpointer user_data)
{
    if (!(log_level & G_LOG_LEVEL_DEBUG) || ENABLE_DEBUG)
        g_log_default_handler (log_domain, log_level, message, user_data);
}

static float
osm_gps_map_get_scale_at_point(int zoom, float rlat, float rlon)
{
    /* world at zoom 1 == 512 pixels */
    return cos(rlat) * M_PI * OSM_EQ_RADIUS / (1<<(7+zoom));
}

static GSList *
gslist_remove_one_gobject(GSList **list, GObject *gobj)
{
    GSList *data = g_slist_find(*list, gobj);
    if (data) {
        g_object_unref(gobj);
        *list = g_slist_delete_link(*list, data);
    }
    return data;
}

static void
gslist_of_gobjects_free(GSList **list)
{
    if (list) {
        g_slist_foreach(*list, (GFunc) g_object_unref, NULL);
        g_slist_free(*list);
        *list = NULL;
    }
}

static void
gslist_of_data_free (GSList **list)
{
    if (list) {
        g_slist_foreach(*list, (GFunc) g_free, NULL);
        g_slist_free(*list);
        *list = NULL;
    }
}

static void
draw_white_rectangle(cairo_t *cr, double x, double y, double width, double height)
{
    cairo_save (cr);
    cairo_set_source_rgb (cr, 1, 1, 1);
    cairo_rectangle (cr, x, y, width, height);
    cairo_fill (cr);
    cairo_restore (cr);
}

static void
osm_gps_map_print_images (OsmGpsMap *map, cairo_t *cr)
{
    GSList *list;
    int min_x = 0,min_y = 0,max_x = 0,max_y = 0;
    int map_x0, map_y0;
    OsmGpsMapPrivate *priv = map->priv;

    map_x0 = priv->map_x - EXTRA_BORDER;
    map_y0 = priv->map_y - EXTRA_BORDER;
    for(list = priv->images; list != NULL; list = list->next)
    {
        GdkRectangle loc;
        OsmGpsMapImage *im = OSM_GPS_MAP_IMAGE(list->data);
        const OsmGpsMapPoint *pt = osm_gps_map_image_get_point(im);

        /* pixel_x,y, offsets */
        loc.x = lon2pixel(priv->map_zoom, pt->rlon) - map_x0;
        loc.y = lat2pixel(priv->map_zoom, pt->rlat) - map_y0;

        osm_gps_map_image_draw (
                         im,
                         cr,
                         &loc);

        max_x = MAX(loc.x + loc.width, max_x);
        min_x = MIN(loc.x - loc.width, min_x);
        max_y = MAX(loc.y + loc.height, max_y);
        min_y = MIN(loc.y - loc.height, min_y);
    }

    gtk_widget_queue_draw_area (
                                GTK_WIDGET(map),
                                min_x + EXTRA_BORDER, min_y + EXTRA_BORDER,
                                max_x + EXTRA_BORDER, max_y + EXTRA_BORDER);

}

static void
osm_gps_map_draw_gps_point (OsmGpsMap *map, cairo_t *cr)
{
    OsmGpsMapPrivate *priv = map->priv;
    int map_x0, map_y0;
    int x, y;
    int r, r2, mr;

    r = priv->ui_gps_point_inner_radius;
    r2 = priv->ui_gps_point_outer_radius;
    mr = MAX(3*r,r2);
    map_x0 = priv->map_x - EXTRA_BORDER;
    map_y0 = priv->map_y - EXTRA_BORDER;
    x = lon2pixel(priv->map_zoom, priv->gps->rlon) - map_x0;
    y = lat2pixel(priv->map_zoom, priv->gps->rlat) - map_y0;

    /* draw transparent area */
    if (r2 > 0) {
        cairo_set_line_width (cr, 1.5);
        cairo_set_source_rgba (cr, 0.75, 0.75, 0.75, 0.4);
        cairo_arc (cr, x, y, r2, 0, 2 * M_PI);
        cairo_fill (cr);
        /* draw transparent area border */
        cairo_set_source_rgba (cr, 0.55, 0.55, 0.55, 0.4);
        cairo_arc (cr, x, y, r2, 0, 2 * M_PI);
        cairo_stroke(cr);
    }

    /* draw ball gradient */
    if (r > 0) {
        cairo_pattern_t *pat;
        /* draw direction arrow */
        if(!isnan(priv->gps_heading)) {
            cairo_move_to (cr, x-r*cos(priv->gps_heading), y-r*sin(priv->gps_heading));
            cairo_line_to (cr, x+3*r*sin(priv->gps_heading), y-3*r*cos(priv->gps_heading));
            cairo_line_to (cr, x+r*cos(priv->gps_heading), y+r*sin(priv->gps_heading));
            cairo_close_path (cr);

            cairo_set_source_rgba (cr, 0.3, 0.3, 1.0, 0.5);
            cairo_fill_preserve (cr);

            cairo_set_line_width (cr, 1.0);
            cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.5);
            cairo_stroke(cr);
        }

        pat = cairo_pattern_create_radial (x-(r/5), y-(r/5), (r/5), x,  y, r);
        cairo_pattern_add_color_stop_rgba (pat, 0, 1, 1, 1, 1.0);
        cairo_pattern_add_color_stop_rgba (pat, 1, 0, 0, 1, 1.0);
        cairo_set_source (cr, pat);
        cairo_arc (cr, x, y, r, 0, 2 * M_PI);
        cairo_fill (cr);
        cairo_pattern_destroy (pat);
        /* draw ball border */
        cairo_set_line_width (cr, 1.0);
        cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);
        cairo_arc (cr, x, y, r, 0, 2 * M_PI);
        cairo_stroke(cr);
    }

    gtk_widget_queue_draw_area (GTK_WIDGET(map),
                                x-mr,
                                y-mr,
                                mr*2,
                                mr*2);
}


#define MSG_RESPONSE_BODY(a)    ((a)->response_body->data)
#define MSG_RESPONSE_LEN(a)     ((a)->response_body->length)
#define MSG_RESPONSE_LEN_FORMAT "%"G_GOFFSET_FORMAT



gboolean
osm_gps_map_map_redraw (OsmGpsMap *map)
{
    cairo_t *cr;
    int w, h;
    OsmGpsMapPrivate *priv = map->priv;
    GtkWidget *widget = GTK_WIDGET(map);

    priv->idle_map_redraw = 0;

    /* dont't redraw if we have not been shown yet */
    if (!priv->pixmap)
        return FALSE;

    /* don't redraw the entire map while the OSD is doing */
    /* some animation or the like. This is to keep the animation */
    /* fluid */
    if (priv->layers) {
        GSList *list;
        for(list = priv->layers; list != NULL; list = list->next) {
            OsmGpsMapLayer *layer = list->data;
            if (osm_gps_map_layer_busy(layer))
                return FALSE;
        }
    }

    /* the motion_notify handler uses priv->surface to redraw the area; if we
     * change it while we are dragging, we will end up showing it in the wrong
     * place. This could be fixed by carefully recompute the coordinates, but
     * for now it's easier just to disable redrawing the map while dragging */
    if (priv->is_dragging)
        return FALSE;

    /* paint to the backing surface */
    cr = cairo_create (priv->pixmap);

    /* undo all offsets that may have happened when dragging */
    priv->drag_mouse_dx = 0;
    priv->drag_mouse_dy = 0;

    priv->redraw_cycle++;

    /* clear white background */
    w = gtk_widget_get_allocated_width (widget);
    h = gtk_widget_get_allocated_width (widget);
    draw_white_rectangle(cr, 0, 0, w + EXTRA_BORDER * 2, h + EXTRA_BORDER * 2);

    osm_gps_map_print_images(map, cr);


    if (priv->layers) {
        GSList *list;
        for(list = priv->layers; list != NULL; list = list->next) {
            OsmGpsMapLayer *layer = list->data;
            osm_gps_map_layer_render (layer, map);
        }
    }

    gtk_widget_queue_draw (GTK_WIDGET (map));

    cairo_destroy (cr);

    return FALSE;
}

void
osm_gps_map_map_redraw_idle (OsmGpsMap *map)
{
    OsmGpsMapPrivate *priv = map->priv;

    if (priv->idle_map_redraw == 0)
        priv->idle_map_redraw = g_idle_add ((GSourceFunc)osm_gps_map_map_redraw, map);
}

/* call this to update center_rlat and center_rlon after
 * changin map_x or map_y */
static void
center_coord_update(OsmGpsMap *map) {

    GtkWidget *widget = GTK_WIDGET(map);
    OsmGpsMapPrivate *priv = map->priv;
    GtkAllocation allocation;

    gtk_widget_get_allocation(widget, &allocation);
    gint pixel_x = priv->map_x + allocation.width/2;
    gint pixel_y = priv->map_y + allocation.height/2;

    priv->center_rlon = pixel2lon(priv->map_zoom, pixel_x);
    priv->center_rlat = pixel2lat(priv->map_zoom, pixel_y);

    g_signal_emit_by_name(widget, "changed");
}

/* Automatically center the map if the current point, i.e the most recent
 * gps point, approaches the edge, and map_auto_center is set. Does not
 * request the map be redrawn */
static void
maybe_autocenter_map (OsmGpsMap *map)
{
    OsmGpsMapPrivate *priv;
    GtkAllocation allocation;

    g_return_if_fail (OSM_IS_GPS_MAP (map));
    priv = map->priv;
    gtk_widget_get_allocation(GTK_WIDGET(map), &allocation);

    if(priv->map_auto_center_enabled)   {
        int pixel_x = lon2pixel(priv->map_zoom, priv->gps->rlon);
        int pixel_y = lat2pixel(priv->map_zoom, priv->gps->rlat);
        int x = pixel_x - priv->map_x;
        int y = pixel_y - priv->map_y;
        int width = allocation.width;
        int height = allocation.height;
        if( x < (width/2 - width/8)     || x > (width/2 + width/8)  ||
            y < (height/2 - height/8)   || y > (height/2 + height/8)) {

            priv->map_x = pixel_x - allocation.width/2;
            priv->map_y = pixel_y - allocation.height/2;
            center_coord_update(map);
        }
    }
}

static gboolean
on_window_key_press(GtkWidget *widget, GdkEventKey *event, OsmGpsMapPrivate *priv)
{
    int i;
    int step;
    gboolean handled;
    GtkAllocation allocation;
    OsmGpsMap *map = OSM_GPS_MAP(widget);

    /* if no keybindings are set, let the app handle them... */
    if (!priv->keybindings_enabled)
        return FALSE;

    handled = FALSE;
    gtk_widget_get_allocation(GTK_WIDGET(map), &allocation);
    step = allocation.width/OSM_GPS_MAP_SCROLL_STEP;

    /* the map handles some keys on its own */
    for (i = 0; i < OSM_GPS_MAP_KEY_MAX; i++) {
        /* not the key we have a binding for */
        if (map->priv->keybindings[i] != event->keyval)
            continue;

        switch(i) {
            case OSM_GPS_MAP_KEY_FULLSCREEN: {
                GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(widget));
                if(!priv->is_fullscreen)
                    gtk_window_fullscreen(GTK_WINDOW(toplevel));
                else
                    gtk_window_unfullscreen(GTK_WINDOW(toplevel));

                priv->is_fullscreen = !priv->is_fullscreen;
                handled = TRUE;
                } break;
            case OSM_GPS_MAP_KEY_ZOOMIN:
                osm_gps_map_zoom_in(map);
                handled = TRUE;
                break;
            case OSM_GPS_MAP_KEY_ZOOMOUT:
                osm_gps_map_zoom_out(map);
                handled = TRUE;
                break;
            case OSM_GPS_MAP_KEY_UP:
                priv->map_y -= step;
                center_coord_update(map);
                osm_gps_map_map_redraw_idle(map);
                handled = TRUE;
                break;
            case OSM_GPS_MAP_KEY_DOWN:
                priv->map_y += step;
                center_coord_update(map);
                osm_gps_map_map_redraw_idle(map);
                handled = TRUE;
                break;
              case OSM_GPS_MAP_KEY_LEFT:
                priv->map_x -= step;
                center_coord_update(map);
                osm_gps_map_map_redraw_idle(map);
                handled = TRUE;
                break;
            case OSM_GPS_MAP_KEY_RIGHT:
                priv->map_x += step;
                center_coord_update(map);
                osm_gps_map_map_redraw_idle(map);
                handled = TRUE;
                break;
            default:
                break;
        }
    }

    return handled;
}


static void
osm_gps_map_init (OsmGpsMap *object)
{
    int i;
    OsmGpsMapPrivate *priv;

    priv = G_TYPE_INSTANCE_GET_PRIVATE (object, OSM_TYPE_GPS_MAP, OsmGpsMapPrivate);
    object->priv = priv;

    priv->pixmap = NULL;

    priv->trip_history = NULL;
    priv->gps = osm_gps_map_point_new_radians(0.0, 0.0);
    priv->gps_heading = OSM_GPS_MAP_INVALID;

    priv->images = NULL;
    priv->layers = NULL;

    priv->drag_counter = 0;
    priv->drag_mouse_dx = 0;
    priv->drag_mouse_dy = 0;
    priv->drag_start_mouse_x = 0;
    priv->drag_start_mouse_y = 0;

    priv->keybindings_enabled = FALSE;
    for (i = 0; i < OSM_GPS_MAP_KEY_MAX; i++)
        priv->keybindings[i] = 0;


    gtk_widget_add_events (GTK_WIDGET (object),
                           GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                           GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK |
                           GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK);
#ifdef HAVE_GDK_EVENT_GET_SCROLL_DELTAS
    gtk_widget_add_events (GTK_WIDGET (object), GDK_SMOOTH_SCROLL_MASK)
#endif
    gtk_widget_set_can_focus (GTK_WIDGET (object), TRUE);

    g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_MASK, my_log_handler, NULL);

    /* setup signal handlers */
    g_signal_connect(object, "key_press_event",
                    G_CALLBACK(on_window_key_press), priv);
}


static void
osm_gps_map_setup(OsmGpsMap *map)
{
    const char *uri;
    OsmGpsMapPrivate *priv = map->priv;

    priv->max_zoom = 18;
    priv->min_zoom = 1;

    /* check if we are being called for a second (or more) time in the lifetime
       of the object, and if so, do some extra cleanup */
    if ( priv->is_constructed ) {
        g_debug("Setup called again in map lifetime");
        /* flush the ram cache */

        /* adjust zoom if necessary */
        if(priv->map_zoom > priv->max_zoom)
            osm_gps_map_set_zoom(map, priv->max_zoom);

        if(priv->map_zoom < priv->min_zoom)
            osm_gps_map_set_zoom(map, priv->min_zoom);

        osm_gps_map_map_redraw_idle(map);
    }
}

static GObject *
osm_gps_map_constructor (GType gtype, guint n_properties, GObjectConstructParam *properties)
{
    GObject *object;
    OsmGpsMap *map;

    /* always chain up to the parent constructor */
    object = G_OBJECT_CLASS(osm_gps_map_parent_class)->constructor(gtype, n_properties, properties);

    map = OSM_GPS_MAP(object);

    osm_gps_map_setup(map);
    map->priv->is_constructed = TRUE;

    return object;
}

static void
osm_gps_map_dispose (GObject *object)
{
    OsmGpsMap *map = OSM_GPS_MAP(object);
    OsmGpsMapPrivate *priv = map->priv;

    if (priv->is_disposed)
        return;

    priv->is_disposed = TRUE;



    /* images and layers contain GObjects which need unreffing, so free here */
    gslist_of_gobjects_free(&priv->images);
    gslist_of_gobjects_free(&priv->layers);

    if(priv->pixmap)
        cairo_surface_destroy (priv->pixmap);

    if (priv->null_tile)
        g_object_unref (priv->null_tile);

    if (priv->idle_map_redraw != 0)
        g_source_remove (priv->idle_map_redraw);

    if (priv->drag_expose_source != 0)
        g_source_remove (priv->drag_expose_source);

    g_free(priv->gps);


    G_OBJECT_CLASS (osm_gps_map_parent_class)->dispose (object);
}

static void
osm_gps_map_finalize (GObject *object)
{
    OsmGpsMap *map = OSM_GPS_MAP(object);
    OsmGpsMapPrivate *priv = map->priv;

    /* trip and tracks contain simple non GObject types, so free them here */
    gslist_of_data_free(&priv->trip_history);

    G_OBJECT_CLASS (osm_gps_map_parent_class)->finalize (object);
}

static void
osm_gps_map_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    g_return_if_fail (OSM_IS_GPS_MAP (object));
    OsmGpsMap *map = OSM_GPS_MAP(object);
    OsmGpsMapPrivate *priv = map->priv;

    switch (prop_id)
    {
        case PROP_AUTO_CENTER:
            priv->map_auto_center_enabled = g_value_get_boolean (value);
            break;
        case PROP_RECORD_TRIP_HISTORY:
            priv->trip_history_record_enabled = g_value_get_boolean (value);
            break;
        case PROP_SHOW_TRIP_HISTORY:
            priv->trip_history_show_enabled = g_value_get_boolean (value);
            break;
        case PROP_ZOOM:
            priv->map_zoom = g_value_get_int (value);
            break;
        case PROP_MAX_ZOOM:
            priv->max_zoom = g_value_get_int (value);
            break;
        case PROP_MIN_ZOOM:
            priv->min_zoom = g_value_get_int (value);
            break;
        case PROP_MAP_X:
            priv->map_x = g_value_get_int (value);
            center_coord_update(map);
            break;
        case PROP_MAP_Y:
            priv->map_y = g_value_get_int (value);
            center_coord_update(map);
            break;
        case PROP_GPS_POINT_R1:
            priv->ui_gps_point_inner_radius = g_value_get_int (value);
            break;
        case PROP_GPS_POINT_R2:
            priv->ui_gps_point_outer_radius = g_value_get_int (value);
            break;
        case PROP_DRAG_LIMIT:
            priv->drag_limit = g_value_get_int (value);
            break;
        case PROP_AUTO_CENTER_THRESHOLD:
            priv->map_auto_center_threshold = g_value_get_float (value);
            break;
        case PROP_SHOW_GPS_POINT:
            priv->gps_point_enabled = g_value_get_boolean (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
osm_gps_map_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    g_return_if_fail (OSM_IS_GPS_MAP (object));
    OsmGpsMap *map = OSM_GPS_MAP(object);
    OsmGpsMapPrivate *priv = map->priv;

    switch (prop_id)
    {
        case PROP_AUTO_CENTER:
            g_value_set_boolean(value, priv->map_auto_center_enabled);
            break;
        case PROP_RECORD_TRIP_HISTORY:
            g_value_set_boolean(value, priv->trip_history_record_enabled);
            break;
        case PROP_SHOW_TRIP_HISTORY:
            g_value_set_boolean(value, priv->trip_history_show_enabled);
            break;
        case PROP_ZOOM:
            g_value_set_int(value, priv->map_zoom);
            break;
        case PROP_MAX_ZOOM:
            g_value_set_int(value, priv->max_zoom);
            break;
        case PROP_MIN_ZOOM:
            g_value_set_int(value, priv->min_zoom);
            break;
        case PROP_LATITUDE:
            g_value_set_float(value, rad2deg(priv->center_rlat));
            break;
        case PROP_LONGITUDE:
            g_value_set_float(value, rad2deg(priv->center_rlon));
            break;
        case PROP_MAP_X:
            g_value_set_int(value, priv->map_x);
            break;
        case PROP_MAP_Y:
            g_value_set_int(value, priv->map_y);
            break;
        case PROP_GPS_POINT_R1:
            g_value_set_int(value, priv->ui_gps_point_inner_radius);
            break;
        case PROP_GPS_POINT_R2:
            g_value_set_int(value, priv->ui_gps_point_outer_radius);
            break;
        case PROP_DRAG_LIMIT:
            g_value_set_int(value, priv->drag_limit);
            break;
        case PROP_AUTO_CENTER_THRESHOLD:
            g_value_set_float(value, priv->map_auto_center_threshold);
            break;
        case PROP_SHOW_GPS_POINT:
            g_value_set_boolean(value, priv->gps_point_enabled);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static gboolean
osm_gps_map_scroll_event (GtkWidget *widget, GdkEventScroll  *event)
{
    OsmGpsMap *map;
    OsmGpsMapPoint *pt;
    float lat, lon, c_lat, c_lon;

    map = OSM_GPS_MAP(widget);
    pt = osm_gps_map_point_new_degrees(0.0,0.0);
    /* arguably we could use get_event_location here, but I'm not convinced it
    is forward compatible to cast between GdkEventScroll and GtkEventButton */
    osm_gps_map_convert_screen_to_geographic(map, event->x, event->y, pt);
    osm_gps_map_point_get_degrees (pt, &lat, &lon);

    c_lat = rad2deg(map->priv->center_rlat);
    c_lon = rad2deg(map->priv->center_rlon);



    if ((event->direction == GDK_SCROLL_UP) && (map->priv->map_zoom < map->priv->max_zoom)) {
        lat = c_lat + ((lat - c_lat)/2.0);
        lon = c_lon + ((lon - c_lon)/2.0);
        osm_gps_map_set_center_and_zoom(map, lat, lon, map->priv->map_zoom+1);
    } else if ((event->direction == GDK_SCROLL_DOWN) && (map->priv->map_zoom > map->priv->min_zoom)) {
        lat = c_lat + ((c_lat - lat)*1.0);
        lon = c_lon + ((c_lon - lon)*1.0);
        osm_gps_map_set_center_and_zoom(map, lat, lon, map->priv->map_zoom-1);
    }

    osm_gps_map_point_free (pt);

    return FALSE;
}

static gboolean
osm_gps_map_button_press (GtkWidget *widget, GdkEventButton *event)
{
    OsmGpsMap *map = OSM_GPS_MAP(widget);
    OsmGpsMapPrivate *priv = map->priv;

    if (priv->layers)
    {
        GSList *list;
        for(list = priv->layers; list != NULL; list = list->next)
        {
            OsmGpsMapLayer *layer = list->data;
            if (osm_gps_map_layer_button_press(layer, map, event))
                return FALSE;
        }
    }

    priv->is_button_down = TRUE;
    priv->drag_counter = 0;
    priv->drag_start_mouse_x = (int) event->x;
    priv->drag_start_mouse_y = (int) event->y;
    priv->drag_start_map_x = priv->map_x;
    priv->drag_start_map_y = priv->map_y;

    return FALSE;
}

static gboolean
osm_gps_map_button_release (GtkWidget *widget, GdkEventButton *event)
{
    OsmGpsMap *map = OSM_GPS_MAP(widget);
    OsmGpsMapPrivate *priv = map->priv;

    if(!priv->is_button_down)
        return FALSE;

    if (priv->is_dragging)
    {
        priv->is_dragging = FALSE;

        priv->map_x = priv->drag_start_map_x;
        priv->map_y = priv->drag_start_map_y;

        priv->map_x += (priv->drag_start_mouse_x - (int) event->x);
        priv->map_y += (priv->drag_start_mouse_y - (int) event->y);

        center_coord_update(map);

        osm_gps_map_map_redraw_idle(map);
    }


    priv->drag_counter = -1;
    priv->is_button_down = FALSE;

    return FALSE;
}

static gboolean
osm_gps_map_idle_expose (GtkWidget *widget)
{
    OsmGpsMapPrivate *priv = OSM_GPS_MAP(widget)->priv;
    priv->drag_expose_source = 0;
    gtk_widget_queue_draw (widget);
    return FALSE;
}

static gboolean
osm_gps_map_motion_notify (GtkWidget *widget, GdkEventMotion  *event)
{
    GdkModifierType state;
    OsmGpsMap *map = OSM_GPS_MAP(widget);
    OsmGpsMapPrivate *priv = map->priv;
    gint x, y;

    GdkDeviceManager* manager = gdk_display_get_device_manager( gdk_display_get_default() );
    GdkDevice* pointer = gdk_device_manager_get_client_pointer( manager);

    if(!priv->is_button_down)
        return FALSE;

    if(priv->is_dragging_point)
    {
        osm_gps_map_convert_screen_to_geographic(map, event->x, event->y, priv->drag_point);
        osm_gps_map_map_redraw_idle(map);
        return FALSE;
    }

    if (event->is_hint)
        // gdk_window_get_pointer (event->window, &x, &y, &state);
        gdk_window_get_device_position( event->window, pointer, &x, &y, &state);

    else
    {
        x = event->x;
        y = event->y;
        state = event->state;
    }

    // are we being dragged
    if (!(state & GDK_BUTTON1_MASK))
        return FALSE;

    if (priv->drag_counter < 0)
        return FALSE;

    /* not yet dragged far enough? */
    if(!priv->drag_counter &&
            ( (x - priv->drag_start_mouse_x) * (x - priv->drag_start_mouse_x) +
              (y - priv->drag_start_mouse_y) * (y - priv->drag_start_mouse_y) <
              priv->drag_limit*priv->drag_limit))
        return FALSE;

    priv->drag_counter++;

    priv->is_dragging = TRUE;

    if (priv->map_auto_center_enabled)
        g_object_set(G_OBJECT(widget), "auto-center", FALSE, NULL);

    priv->drag_mouse_dx = x - priv->drag_start_mouse_x;
    priv->drag_mouse_dy = y - priv->drag_start_mouse_y;

    /* instead of redrawing directly just add an idle function */
    if (!priv->drag_expose_source)
        priv->drag_expose_source =
            g_idle_add ((GSourceFunc)osm_gps_map_idle_expose, widget);

    return FALSE;
}

static gboolean
osm_gps_map_configure (GtkWidget *widget, GdkEventConfigure *event)
{
    int w,h;
    GdkWindow *window;
    OsmGpsMap *map = OSM_GPS_MAP(widget);
    OsmGpsMapPrivate *priv = map->priv;

    if (priv->pixmap)
        cairo_surface_destroy (priv->pixmap);

    w = gtk_widget_get_allocated_width (widget);
    h = gtk_widget_get_allocated_height (widget);
    window = gtk_widget_get_window(widget);

    priv->pixmap = gdk_window_create_similar_surface (
                        window,
                        CAIRO_CONTENT_COLOR,
                        w + EXTRA_BORDER * 2,
                        h + EXTRA_BORDER * 2);

    // pixel_x,y, offsets
    gint pixel_x = lon2pixel(priv->map_zoom, priv->center_rlon);
    gint pixel_y = lat2pixel(priv->map_zoom, priv->center_rlat);

    priv->map_x = pixel_x - w/2;
    priv->map_y = pixel_y - h/2;

    osm_gps_map_map_redraw(OSM_GPS_MAP(widget));

    g_signal_emit_by_name(widget, "changed");

    return FALSE;
}

static gboolean
osm_gps_map_draw (GtkWidget *widget, cairo_t *cr)
{
    OsmGpsMap *map = OSM_GPS_MAP(widget);
    OsmGpsMapPrivate *priv = map->priv;

    if (!priv->drag_mouse_dx && !priv->drag_mouse_dy) {
        cairo_set_source_surface (cr, priv->pixmap, 0, 0);
    } else {
        cairo_set_source_surface (cr, priv->pixmap,
            priv->drag_mouse_dx - EXTRA_BORDER,
            priv->drag_mouse_dy - EXTRA_BORDER);
    }

    cairo_paint (cr);

    if (priv->layers) {
        GSList *list;
        for(list = priv->layers; list != NULL; list = list->next) {
            OsmGpsMapLayer *layer = list->data;
            osm_gps_map_layer_draw(layer, map, cr);
        }
    }

    return FALSE;
}

static void
osm_gps_map_class_init (OsmGpsMapClass *klass)
{
    GObjectClass* object_class;
    GtkWidgetClass *widget_class;

    g_type_class_add_private (klass, sizeof (OsmGpsMapPrivate));

    object_class = G_OBJECT_CLASS (klass);
    object_class->dispose = osm_gps_map_dispose;
    object_class->finalize = osm_gps_map_finalize;
    object_class->constructor = osm_gps_map_constructor;
    object_class->set_property = osm_gps_map_set_property;
    object_class->get_property = osm_gps_map_get_property;

    widget_class = GTK_WIDGET_CLASS (klass);
    widget_class->draw = osm_gps_map_draw;
    widget_class->configure_event = osm_gps_map_configure;
    widget_class->button_press_event = osm_gps_map_button_press;
    widget_class->button_release_event = osm_gps_map_button_release;
    widget_class->motion_notify_event = osm_gps_map_motion_notify;
    widget_class->scroll_event = osm_gps_map_scroll_event;
    //widget_class->get_preferred_width = osm_gps_map_get_preferred_width;
    //widget_class->get_preferred_height = osm_gps_map_get_preferred_height;

    /* default implementation of draw_gps_point */
    klass->draw_gps_point = osm_gps_map_draw_gps_point;

    g_object_class_install_property (object_class,
                                     PROP_AUTO_CENTER,
                                     g_param_spec_boolean ("auto-center",
                                                           "auto center",
                                                           "map auto center",
                                                           TRUE,
                                                           G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT));

    g_object_class_install_property (object_class,
                                     PROP_AUTO_CENTER_THRESHOLD,
                                     g_param_spec_float ("auto-center-threshold",
                                                         "auto center threshold",
                                                         "the amount of the window the gps point must move before auto centering",
                                                         0.0, /* minimum property value */
                                                         1.0, /* maximum property value */
                                                         0.25,
                                                         G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT));

    g_object_class_install_property (object_class,
                                     PROP_RECORD_TRIP_HISTORY,
                                     g_param_spec_boolean ("record-trip-history",
                                                           "record trip history",
                                                           "should all gps points be recorded in a trip history",
                                                           TRUE,
                                                           G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT));

    g_object_class_install_property (object_class,
                                     PROP_SHOW_TRIP_HISTORY,
                                     g_param_spec_boolean ("show-trip-history",
                                                           "show trip history",
                                                           "should the recorded trip history be shown on the map",
                                                           TRUE,
                                                           G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT));

    /**
     * OsmGpsMap:show-gps-point:
     *
     * Controls whether the current gps point is shown on the map. Note that
     * for derived classes that implement the draw_gps_point vfunc, if this
     * property is %FALSE
     **/
    g_object_class_install_property (object_class,
                                     PROP_SHOW_GPS_POINT,
                                     g_param_spec_boolean ("show-gps-point",
                                                           "show gps point",
                                                           "should the current gps point be shown on the map",
                                                           TRUE,
                                                           G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT));


    /**
     * OsmGpsMap:zoom:
     *
     * The map zoom level. Connect to ::notify::zoom if you want to be informed
     * when this changes.
    **/
    g_object_class_install_property (object_class,
                                     PROP_ZOOM,
                                     g_param_spec_int ("zoom",
                                                       "zoom",
                                                       "Map zoom level",
                                                       MIN_ZOOM, /* minimum property value */
                                                       MAX_ZOOM, /* maximum property value */
                                                       3,
                                                       G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property (object_class,
                                     PROP_MAX_ZOOM,
                                     g_param_spec_int ("max-zoom",
                                                       "max zoom",
                                                       "Maximum zoom level",
                                                       MIN_ZOOM, /* minimum property value */
                                                       MAX_ZOOM, /* maximum property value */
                                                       OSM_MAX_ZOOM,
                                                       G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property (object_class,
                                     PROP_MIN_ZOOM,
                                     g_param_spec_int ("min-zoom",
                                                       "min zoom",
                                                       "Minimum zoom level",
                                                       MIN_ZOOM, /* minimum property value */
                                                       MAX_ZOOM, /* maximum property value */
                                                       OSM_MIN_ZOOM,
                                                       G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property (object_class,
                                     PROP_LATITUDE,
                                     g_param_spec_float ("latitude",
                                                         "latitude",
                                                         "Latitude in degrees",
                                                         -90.0, /* minimum property value */
                                                         90.0, /* maximum property value */
                                                         0,
                                                         G_PARAM_READABLE));

    g_object_class_install_property (object_class,
                                     PROP_LONGITUDE,
                                     g_param_spec_float ("longitude",
                                                         "longitude",
                                                         "Longitude in degrees",
                                                         -180.0, /* minimum property value */
                                                         180.0, /* maximum property value */
                                                         0,
                                                         G_PARAM_READABLE));

    g_object_class_install_property (object_class,
                                     PROP_MAP_X,
                                     g_param_spec_int ("map-x",
                                                       "map-x",
                                                       "Initial map x location",
                                                       G_MININT, /* minimum property value */
                                                       G_MAXINT, /* maximum property value */
                                                       890,
                                                       G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property (object_class,
                                     PROP_MAP_Y,
                                     g_param_spec_int ("map-y",
                                                       "map-y",
                                                       "Initial map y location",
                                                       G_MININT, /* minimum property value */
                                                       G_MAXINT, /* maximum property value */
                                                       515,
                                                       G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));


    /**
     * OsmGpsMap:map-source:
     *
     * A #OsmGpsMapSource_t representing the tile repository to use
     *
     * <note>
     * <para>
     * If you do not wish to use the default map tiles (provided by OpenStreeMap)
     * it is recommened that you set this property at construction, instead
     * of setting #OsmGpsMap:repo-uri.
     * </para>
     * </note>
     **/
    g_object_class_install_property (object_class,
                                     PROP_DRAG_LIMIT,
                                     g_param_spec_int ("drag-limit",
                                                       "drag limit",
                                                       "The number of pixels the user has to move the pointer in order to start dragging",
                                                       0,           /* minimum property value */
                                                       G_MAXINT,    /* maximum property value */
                                                       10,
                                                       G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

    /**
     * OsmGpsMap::changed:
     *
     * The #OsmGpsMap::changed signal is emitted any time the map zoom or map center
     * is chaged (such as by dragging or zooming).
     *
     * <note>
     * <para>
     * If you are only interested in the map zoom, then you can simply connect
     * to ::notify::zoom
     * </para>
     * </note>
     **/
    g_signal_new ("changed", OSM_TYPE_GPS_MAP,
                  G_SIGNAL_RUN_FIRST, 0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}



/**
 * osm_gps_map_get_bbox:
 * @pt1: (out): point to be filled with the top left location
 * @pt2: (out): point to be filled with the bottom right location
 *
 * Returns the geographic locations of the bounding box describing the contents
 * of the current window, i.e the top left and bottom right corners.
 **/
void
osm_gps_map_get_bbox (OsmGpsMap *map, OsmGpsMapPoint *pt1, OsmGpsMapPoint *pt2)
{
    GtkAllocation allocation;
    OsmGpsMapPrivate *priv = map->priv;

    if (pt1 && pt2) {
        gtk_widget_get_allocation(GTK_WIDGET(map), &allocation);
        pt1->rlat = pixel2lat(priv->map_zoom, priv->map_y);
        pt1->rlon = pixel2lon(priv->map_zoom, priv->map_x);
        pt2->rlat = pixel2lat(priv->map_zoom, priv->map_y + allocation.height);
        pt2->rlon = pixel2lon(priv->map_zoom, priv->map_x + allocation.width);
    }
}

/**
 * osm_gps_map_zoom_fit_bbox:
 * Zoom and center the map so that both points fit inside the window.
 **/
void
osm_gps_map_zoom_fit_bbox (OsmGpsMap *map, float latitude1, float latitude2, float longitude1, float longitude2)
{
    GtkAllocation allocation;
    int zoom;
    gtk_widget_get_allocation (GTK_WIDGET (map), &allocation);
    zoom = latlon2zoom (allocation.height, allocation.width, deg2rad(latitude1), deg2rad(latitude2), deg2rad(longitude1), deg2rad(longitude2));
    osm_gps_map_set_center (map, (latitude1 + latitude2) / 2, (longitude1 + longitude2) / 2);
    osm_gps_map_set_zoom (map, zoom);
}

/**
 * osm_gps_map_set_center_and_zoom:
 *
 * Since: 0.7.0
 **/
void osm_gps_map_set_center_and_zoom (OsmGpsMap *map, float latitude, float longitude, int zoom)
{
    osm_gps_map_set_center (map, latitude, longitude);
    osm_gps_map_set_zoom (map, zoom);
}

/**
 * osm_gps_map_set_center:
 *
 **/
void
osm_gps_map_set_center (OsmGpsMap *map, float latitude, float longitude)
{
    int pixel_x, pixel_y;
    OsmGpsMapPrivate *priv;
    GtkAllocation allocation;

    g_return_if_fail (OSM_IS_GPS_MAP (map));

    priv = map->priv;
    gtk_widget_get_allocation(GTK_WIDGET(map), &allocation);
    g_object_set(G_OBJECT(map), "auto-center", FALSE, NULL);

    priv->center_rlat = deg2rad(latitude);
    priv->center_rlon = deg2rad(longitude);

    pixel_x = lon2pixel(priv->map_zoom, priv->center_rlon);
    pixel_y = lat2pixel(priv->map_zoom, priv->center_rlat);

    priv->map_x = pixel_x - allocation.width/2;
    priv->map_y = pixel_y - allocation.height/2;

    osm_gps_map_map_redraw_idle(map);

    g_signal_emit_by_name(map, "changed");
}

/**
 * osm_gps_map_set_zoom_offset:
 *
 **/
void
osm_gps_map_set_zoom_offset (OsmGpsMap *map, int zoom_offset)
{
    OsmGpsMapPrivate *priv;

    g_return_if_fail (OSM_GPS_MAP (map));
    priv = map->priv;

    if (zoom_offset != priv->tile_zoom_offset)
    {
        priv->tile_zoom_offset = zoom_offset;
        osm_gps_map_map_redraw_idle (map);
    }
}

/**
 * osm_gps_map_set_zoom:
 *
 **/
int
osm_gps_map_set_zoom (OsmGpsMap *map, int zoom)
{
    int width_center, height_center;
    OsmGpsMapPrivate *priv;
    GtkAllocation allocation;

    g_return_val_if_fail (OSM_IS_GPS_MAP (map), 0);
    priv = map->priv;

    if (zoom != priv->map_zoom)
    {
        gtk_widget_get_allocation(GTK_WIDGET(map), &allocation);
        width_center  = allocation.width / 2;
        height_center = allocation.height / 2;

        /* update zoom but constrain [min_zoom..max_zoom] */
        priv->map_zoom = CLAMP(zoom, priv->min_zoom, priv->max_zoom);
        priv->map_x = lon2pixel(priv->map_zoom, priv->center_rlon) - width_center;
        priv->map_y = lat2pixel(priv->map_zoom, priv->center_rlat) - height_center;

        osm_gps_map_map_redraw_idle(map);

        g_signal_emit_by_name(map, "changed");
        g_object_notify(G_OBJECT(map), "zoom");
    }
    return priv->map_zoom;
}

/**
 * osm_gps_map_zoom_in:
 *
 **/
int
osm_gps_map_zoom_in (OsmGpsMap *map)
{
    g_return_val_if_fail (OSM_IS_GPS_MAP (map), 0);
    return osm_gps_map_set_zoom(map, map->priv->map_zoom+1);
}

/**
 * osm_gps_map_zoom_out:
 *
 **/
int
osm_gps_map_zoom_out (OsmGpsMap *map)
{
    g_return_val_if_fail (OSM_IS_GPS_MAP (map), 0);
    return osm_gps_map_set_zoom(map, map->priv->map_zoom-1);
}

/**
 * osm_gps_map_new:
 *
 * Returns a new #OsmGpsMap object, defaults to showing data from
 * <ulink url="http://www.openstreetmap.org"><citetitle>OpenStreetMap</citetitle></ulink>
 *
 * See the properties description for more information about construction
 * parameters than could be passed to g_object_new()
 *
 * Returns: a newly created #OsmGpsMap object.
 **/
GtkWidget *
osm_gps_map_new (void)
{
    return g_object_new (OSM_TYPE_GPS_MAP, NULL);
}

/**
 * osm_gps_map_scroll:
 * @map:
 * @dx:
 * @dy:
 *
 * Scrolls the map by @dx, @dy pixels (positive north, east)
 *
 **/
void
osm_gps_map_scroll (OsmGpsMap *map, gint dx, gint dy)
{
    OsmGpsMapPrivate *priv;

    g_return_if_fail (OSM_IS_GPS_MAP (map));
    priv = map->priv;

    priv->map_x += dx;
    priv->map_y += dy;
    center_coord_update(map);

    osm_gps_map_map_redraw_idle (map);
}

/**
 * osm_gps_map_get_scale:
 * @map:
 *
 * Returns: the scale of the map at the center, in meters/pixel.
 *
 **/
float
osm_gps_map_get_scale (OsmGpsMap *map)
{
    OsmGpsMapPrivate *priv;

    g_return_val_if_fail (OSM_IS_GPS_MAP (map), OSM_GPS_MAP_INVALID);
    priv = map->priv;

    return osm_gps_map_get_scale_at_point(priv->map_zoom, priv->center_rlat, priv->center_rlon);
}


/**
 * osm_gps_map_set_keyboard_shortcut:
 * @key: a #OsmGpsMapKey_t
 * @keyval:
 *
 * Associates a keyboard shortcut with the supplied @keyval
 * (as returned by #gdk_keyval_from_name or simiar). The action given in @key
 * will be triggered when the corresponding @keyval is pressed. By default
 * no keyboard shortcuts are associated.
 *
 **/
void
osm_gps_map_set_keyboard_shortcut (OsmGpsMap *map, OsmGpsMapKey_t key, guint keyval)
{
    g_return_if_fail (OSM_IS_GPS_MAP (map));
    g_return_if_fail(key < OSM_GPS_MAP_KEY_MAX);

    map->priv->keybindings[key] = keyval;
    map->priv->keybindings_enabled = TRUE;
}


/**
 * osm_gps_map_gps_clear:
 *
 * Since: 0.7.0
 **/
void
osm_gps_map_gps_clear (OsmGpsMap *map)
{
    OsmGpsMapPrivate *priv;

    g_return_if_fail (OSM_IS_GPS_MAP (map));
    priv = map->priv;

    osm_gps_map_map_redraw_idle(map);
}

/**
 * osm_gps_map_gps_add:
 * @latitude: degrees
 * @longitude: degrees
 * @heading: degrees or #OSM_GPS_MAP_INVALID to disable showing heading
 *
 * Since: 0.7.0
 **/
void
osm_gps_map_gps_add (OsmGpsMap *map, float latitude, float longitude, float heading)
{
    OsmGpsMapPrivate *priv;

    g_return_if_fail (OSM_IS_GPS_MAP (map));
    priv = map->priv;

    /* update the current point */
    priv->gps->rlat = deg2rad(latitude);
    priv->gps->rlon = deg2rad(longitude);
    priv->gps_heading = deg2rad(heading);

    osm_gps_map_map_redraw_idle (map);
    maybe_autocenter_map (map);
}

/**
 * osm_gps_map_image_add:
 *
 * Returns: (transfer full): A #OsmGpsMapImage representing the added pixbuf
 * Since: 0.7.0
 **/
OsmGpsMapImage *
osm_gps_map_image_add (OsmGpsMap *map, float latitude, float longitude, GdkPixbuf *image)
{
    return osm_gps_map_image_add_with_alignment_z (map, latitude, longitude, image, 0.5, 0.5, 0);
}

/**
 * osm_gps_map_image_add_z:
 *
 * Returns: (transfer full): A #OsmGpsMapImage representing the added pixbuf
 * Since: 0.7.4
 **/
OsmGpsMapImage *
osm_gps_map_image_add_z (OsmGpsMap *map, float latitude, float longitude, GdkPixbuf *image, gint zorder)
{
    return osm_gps_map_image_add_with_alignment_z (map, latitude, longitude, image, 0.5, 0.5, zorder);
}

static void
on_image_changed (OsmGpsMapImage *image, GParamSpec *pspec, OsmGpsMap *map)
{
    osm_gps_map_map_redraw_idle (map);
}

/**
 * osm_gps_map_image_add_with_alignment:
 *
 * Returns: (transfer full): A #OsmGpsMapImage representing the added pixbuf
 * Since: 0.7.0
 **/
OsmGpsMapImage *
osm_gps_map_image_add_with_alignment (OsmGpsMap *map, float latitude, float longitude, GdkPixbuf *image, float xalign, float yalign)
{
    return osm_gps_map_image_add_with_alignment_z (map, latitude, longitude, image, xalign, yalign, 0);
}

static gint
osm_gps_map_image_z_compare(gconstpointer item1, gconstpointer item2)
{
    gint z1 = osm_gps_map_image_get_zorder(OSM_GPS_MAP_IMAGE(item1));
    gint z2 = osm_gps_map_image_get_zorder(OSM_GPS_MAP_IMAGE(item2));

    return(z1 - z2 + 1);
}

/**
 * osm_gps_map_image_add_with_alignment_z:
 *
 * Returns: (transfer full): A #OsmGpsMapImage representing the added pixbuf
 * Since: 0.7.4
 **/
OsmGpsMapImage *
osm_gps_map_image_add_with_alignment_z (OsmGpsMap *map, float latitude, float longitude, GdkPixbuf *image, float xalign, float yalign, gint zorder)
{
    OsmGpsMapImage *im;
    OsmGpsMapPoint pt;

    g_return_val_if_fail (OSM_IS_GPS_MAP (map), NULL);
    pt.rlat = deg2rad(latitude);
    pt.rlon = deg2rad(longitude);

    im = g_object_new (OSM_TYPE_GPS_MAP_IMAGE, "pixbuf", image, "x-align", xalign, "y-align", yalign, "point", &pt, "z-order", zorder, NULL);
    g_signal_connect(im, "notify",
                    G_CALLBACK(on_image_changed), map);

    map->priv->images = g_slist_insert_sorted(map->priv->images, im,
                                              (GCompareFunc) osm_gps_map_image_z_compare);
    osm_gps_map_map_redraw_idle(map);

    g_object_ref(im);
    return im;
}

/**
 * osm_gps_map_image_remove:
 *
 * Since: 0.7.0
 **/
gboolean
osm_gps_map_image_remove (OsmGpsMap *map, OsmGpsMapImage *image)
{
    GSList *data;

    g_return_val_if_fail (OSM_IS_GPS_MAP (map), FALSE);
    g_return_val_if_fail (image != NULL, FALSE);

    data = gslist_remove_one_gobject (&map->priv->images, G_OBJECT(image));
    osm_gps_map_map_redraw_idle(map);
    return data != NULL;
}

/**
 * osm_gps_map_image_remove_all:
 *
 * Since: 0.7.0
 **/
void
osm_gps_map_image_remove_all (OsmGpsMap *map)
{
    g_return_if_fail (OSM_IS_GPS_MAP (map));

    gslist_of_gobjects_free(&map->priv->images);
    osm_gps_map_map_redraw_idle(map);
}

/**
 * osm_gps_map_layer_add:
 * @layer: a #OsmGpsMapLayer object
 *
 * Since: 0.7.0
 **/
void
osm_gps_map_layer_add (OsmGpsMap *map, OsmGpsMapLayer *layer)
{
    g_return_if_fail (OSM_IS_GPS_MAP (map));
    g_return_if_fail (OSM_GPS_MAP_IS_LAYER (layer));

    g_object_ref(G_OBJECT(layer));
    map->priv->layers = g_slist_append(map->priv->layers, layer);
}

/**
 * osm_gps_map_layer_remove:
 * @layer: a #OsmGpsMapLayer object
 *
 * Since: 0.7.0
 **/
gboolean
osm_gps_map_layer_remove (OsmGpsMap *map, OsmGpsMapLayer *layer)
{
    GSList *data;

    g_return_val_if_fail (OSM_IS_GPS_MAP (map), FALSE);
    g_return_val_if_fail (layer != NULL, FALSE);

    data = gslist_remove_one_gobject (&map->priv->layers, G_OBJECT(layer));
    osm_gps_map_map_redraw_idle(map);
    return data != NULL;
}

/**
 * osm_gps_map_layer_remove_all:
 *
 * Since: 0.7.0
 **/
void
osm_gps_map_layer_remove_all (OsmGpsMap *map)
{
    g_return_if_fail (OSM_IS_GPS_MAP (map));

    gslist_of_gobjects_free(&map->priv->layers);
    osm_gps_map_map_redraw_idle(map);
}

/**
 * osm_gps_map_convert_screen_to_geographic:
 * @map:
 * @pixel_x: pixel location on map, x axis
 * @pixel_y: pixel location on map, y axis
 * @pt: (out): location
 *
 * Convert the given pixel location on the map into corresponding
 * location on the globe
 *
 * Since: 0.7.0
 **/
void
osm_gps_map_convert_screen_to_geographic(OsmGpsMap *map, gint pixel_x, gint pixel_y, OsmGpsMapPoint *pt)
{
    OsmGpsMapPrivate *priv;
    int map_x0, map_y0;

    g_return_if_fail (OSM_IS_GPS_MAP (map));
    g_return_if_fail (pt);

    priv = map->priv;
    map_x0 = priv->map_x - EXTRA_BORDER;
    map_y0 = priv->map_y - EXTRA_BORDER;

    pt->rlat = pixel2lat(priv->map_zoom, map_y0 + pixel_y);
    pt->rlon = pixel2lon(priv->map_zoom, map_x0 + pixel_x);
}

/**
 * osm_gps_map_convert_geographic_to_screen:
 * @map:
 * @pt: location
 * @pixel_x: (out): pixel location on map, x axis
 * @pixel_y: (out): pixel location on map, y axis
 *
 * Convert the given location on the globe to the corresponding
 * pixel locations on the map.
 *
 * Since: 0.7.0
 **/
void
osm_gps_map_convert_geographic_to_screen(OsmGpsMap *map, OsmGpsMapPoint *pt, gint *pixel_x, gint *pixel_y)
{
    OsmGpsMapPrivate *priv;
    int map_x0, map_y0;

    g_return_if_fail (OSM_IS_GPS_MAP (map));
    g_return_if_fail (pt);

    priv = map->priv;
    map_x0 = priv->map_x - EXTRA_BORDER;
    map_y0 = priv->map_y - EXTRA_BORDER;

    if (pixel_x)
        *pixel_x = lon2pixel(priv->map_zoom, pt->rlon) - map_x0 + priv->drag_mouse_dx;
    if (pixel_y)
        *pixel_y = lat2pixel(priv->map_zoom, pt->rlat) - map_y0 + priv->drag_mouse_dy;
}

/**
 * osm_gps_map_get_event_location:
 * @map:
 * @event: A #GtkEventButton that occured on the map
 *
 * A convenience function for getting the geographic location of events,
 * such as mouse clicks, on the map
 *
 * Returns: (transfer full): The point on the globe corresponding to the click
 * Since: 0.7.0
 **/
OsmGpsMapPoint *
osm_gps_map_get_event_location (OsmGpsMap *map, GdkEventButton *event)
{
    OsmGpsMapPoint *p = osm_gps_map_point_new_degrees(0.0,0.0);
    osm_gps_map_convert_screen_to_geographic(map, event->x, event->y, p);
    return p;
}
