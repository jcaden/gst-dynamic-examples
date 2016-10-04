#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#define BUGGY_CODE FALSE
#define FORCE_RACE_CONDITIONS FALSE

#define SLEEP_TIME 200000 /*us*/

#define NAME "change_filter"
#define GST_CAT_DEFAULT change_filter
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define WINDOW_WIDTH 400
#define WINDOW_HEIGHT 393

#define SRC_NAME "src"
#define FILTER_NAME "filter"

typedef enum
{
  FILTER_SET,
  FILTER_UNSET
} FilterStatus;

#define APP_DATA_INIT {0, NULL, FILTER_UNSET, NULL, NULL}

typedef struct _AppData
{
  guintptr video_window_handle;
  GstElement *pipeline;
  FilterStatus filter_status;
  GtkWidget *status_bar;
  GtkWidget *filter_button;
} AppData;

static GstBusSyncReply
bus_sync_handler (GstBus *bus, GstMessage *message, gpointer user_data)
{
  AppData *app_data = user_data;

  // ignore anything but 'prepare-window-handle' element messages
  if (!gst_is_video_overlay_prepare_window_handle_message (message)) {
    if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR) {
      GError *error;
      gchar *debug;

      gst_message_parse_error (message, &error, &debug);
      GST_ERROR ("ERROR from element %s: %s",
          GST_OBJECT_NAME (message->src), error->message);
      if (debug) {
        GST_ERROR ("Debugging info: %s", debug);
      }
      g_error_free (error);
      g_free (debug);

      g_assert_not_reached ();
    } else if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_WARNING) {
      GST_ERROR ("Warning on bug: %"
          GST_PTR_FORMAT, message);
    }
    return GST_BUS_PASS;
  }

  if (app_data->video_window_handle != 0) {
    GstVideoOverlay *overlay;

    overlay = GST_VIDEO_OVERLAY (GST_MESSAGE_SRC (message));
    gst_video_overlay_set_window_handle (overlay,
        app_data->video_window_handle);
  } else {
    g_warning ("Should have obtained video_window_handle by now!");
  }

  gst_message_unref (message);
  return GST_BUS_DROP;
}

static void
video_widget_realize_cb (GtkWidget *widget, gpointer data)
{
  AppData *app_data = data;

  app_data->video_window_handle =
      GDK_WINDOW_XID (gtk_widget_get_window (widget));
}

static void
create_pipeline (AppData *app_data)
{
  GError *err = NULL;
  GstBus *bus;
  GstElement *pipeline =
      gst_parse_launch ("v4l2src name=" SRC_NAME " ! autovideosink name=sink",
          &err);

  if (pipeline == NULL) {
    GST_ERROR ("Error while creating the pipeline: %s", err->message);
    g_error_free (err);
    g_assert_not_reached ();
  }

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_sync_handler, app_data,
      NULL);
  gst_object_unref (bus);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  app_data->pipeline = pipeline;
}

static GstPadProbeReturn
connect_element_probe (GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
  AppData *app_data = data;
  GstElement *filter;
  GstPad *peer;

  peer = gst_pad_get_peer (pad);
  if (peer == NULL) {
    return GST_PAD_PROBE_REMOVE;
  }

  GST_DEBUG ("Adding filter...");
  /* Unlink pads */
  gst_pad_unlink (pad, peer);

  /* If an idle probe is used, this code works even if we stop for a while */
#if FORCE_RACE_CONDITIONS
  g_usleep (SLEEP_TIME);
#endif

  /* Create element */
  filter = gst_element_factory_make ("clockoverlay", FILTER_NAME);
  g_object_set (filter, "font-desc", "Arial 32", NULL);
  gst_bin_add (GST_BIN (app_data->pipeline), filter);
  gst_element_sync_state_with_parent (filter);

  /* Connect new element */
  gst_element_link_pads (GST_ELEMENT (GST_OBJECT_PARENT (pad)),
      GST_OBJECT_NAME (pad), filter, NULL);
  gst_element_link_pads (filter, NULL, GST_ELEMENT (GST_OBJECT_PARENT (peer)),
      GST_OBJECT_NAME (peer));

  g_object_unref (peer);

  GST_DEBUG ("Filter added correctly");

  gtk_statusbar_push (GTK_STATUSBAR (app_data->status_bar), 0,
      "Filter added correctly");
  app_data->filter_status = FILTER_SET;
  gtk_widget_set_sensitive (app_data->filter_button, TRUE);

  return GST_PAD_PROBE_REMOVE;
}

static void
connect_new_filter (AppData *app_data)
{
  GstPad *src_pad;
  GstElement *src;

  gtk_button_set_label (GTK_BUTTON (app_data->filter_button),
      "Remove clockoverlay");

  src = gst_bin_get_by_name (GST_BIN (app_data->pipeline), SRC_NAME);
  g_assert (src);
  GST_DEBUG_OBJECT (src, SRC_NAME
      " found");

  src_pad = gst_element_get_static_pad (src, "src");
  g_assert (src_pad);

#if !BUGGY_CODE
  gst_pad_add_probe (src_pad, GST_PAD_PROBE_TYPE_IDLE, connect_element_probe,
      app_data, NULL);
#else
  /* Trying to execute the same code directly may fail depending on race conditions */
  connect_element_probe (src_pad, NULL, app_data);
#endif

  g_object_unref (src_pad);
  g_object_unref (src);
}

static GstPadProbeReturn
disconnect_element_probe (GstPad *src_peer, GstPadProbeInfo *info,
    gpointer data)
{
  GstPad *src_pad, *sink_peer, *sink_pad;
  GstElement *filter;
  AppData *app_data = data;

  sink_pad = gst_pad_get_peer (src_peer);
  g_assert (sink_pad);

  filter = gst_pad_get_parent_element (sink_pad);
  g_assert (filter);

  src_pad = gst_element_get_static_pad (filter, "src");
  sink_peer = gst_pad_get_peer (src_pad);
  g_assert (sink_peer);


  gst_pad_unlink (src_peer, sink_pad);
  gst_pad_unlink (src_pad, sink_peer);
#if FORCE_RACE_CONDITIONS
  g_usleep (SLEEP_TIME);
#endif
  gst_pad_link (src_peer, sink_peer);

  gst_element_set_state (filter, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (app_data->pipeline), filter);

  g_object_unref (sink_peer);
  g_object_unref (src_pad);
  g_object_unref (sink_pad);
  g_object_unref (filter);

  GST_DEBUG ("Filter removed correctly");

  app_data->filter_status = FILTER_UNSET;
  gtk_statusbar_push (GTK_STATUSBAR (app_data->status_bar), 0,
      "Filter removed correctly");
  gtk_widget_set_sensitive (app_data->filter_button, TRUE);

  return GST_PAD_PROBE_REMOVE;
}

static void
disconnect_filter (AppData *app_data)
{
  GstPad *sink_pad, *src_peer;
  GstElement
      *filter = gst_bin_get_by_name (GST_BIN (app_data->pipeline), FILTER_NAME);

  g_assert (filter);

  sink_pad = gst_element_get_static_pad (filter, "video_sink");
  g_assert (sink_pad);
  src_peer = gst_pad_get_peer (sink_pad);
  g_assert (src_peer);

  gtk_button_set_label (GTK_BUTTON (app_data->filter_button),
      "Add clockoverlay");

  /* Note that we are waiting for the src pad to be idle, otherwise it can
   * emit buffers while the pads are disconnected */
#if !BUGGY_CODE
  gst_pad_add_probe (src_peer, GST_PAD_PROBE_TYPE_IDLE,
      disconnect_element_probe, app_data, NULL);
#else
  // If calling without blocking it may fail
  disconnect_element_probe (src_peer, NULL, app_data);
#endif

  g_object_unref (src_peer);
  g_object_unref (sink_pad);
  g_object_unref (filter);
}

static void
filter_button_clicked (AppData *app_data)
{
  gtk_widget_set_sensitive (app_data->filter_button, FALSE);

  if (app_data->filter_status == FILTER_SET) {
    disconnect_filter (app_data);
  } else {
    connect_new_filter (app_data);
  }
}

static void
activate_gui (GtkApplication *app, gpointer user_data)
{
  GtkWidget *window, *video_widget, *window_content, *filter_button,
      *button_box, *status_bar;
  AppData *app_data = user_data;

  window = gtk_application_window_new (app);
  gtk_window_set_title (GTK_WINDOW (window), "Change filter");
  gtk_window_set_default_size (GTK_WINDOW (window), WINDOW_WIDTH,
      WINDOW_HEIGHT);

  window_content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
  gtk_container_add (GTK_CONTAINER (window), window_content);

  button_box = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_container_add (GTK_CONTAINER (window_content), button_box);

  filter_button = gtk_button_new_with_label ("Add clockoverlay");
  g_signal_connect_swapped (filter_button, "clicked",
      G_CALLBACK (filter_button_clicked), app_data);
  gtk_container_add (GTK_CONTAINER (button_box), filter_button);
  app_data->filter_button = filter_button;

  video_widget = gtk_drawing_area_new ();
  g_signal_connect (video_widget, "realize",
      G_CALLBACK (video_widget_realize_cb), app_data);
  gtk_container_add (GTK_CONTAINER (window_content), video_widget);
  gtk_box_set_child_packing (GTK_BOX (window_content), video_widget, TRUE, TRUE,
      1, GTK_PACK_START);

  status_bar = gtk_statusbar_new ();
  gtk_container_add (GTK_CONTAINER (window_content), status_bar);
  app_data->status_bar = status_bar;

  gtk_widget_show_all (window);
  gtk_widget_realize (video_widget);

  g_assert (app_data->video_window_handle != 0);

  create_pipeline (app_data);
}

int
main (int argc, char **argv)
{
  GtkApplication *app;
  int status;
  AppData app_data = APP_DATA_INIT;
  GOptionGroup *gst_group;

  gst_init (&argc, &argv);
  gst_group = gst_init_get_option_group ();

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, NAME, 0, NAME);

  app = gtk_application_new ("org.kurento.change_filter",
      G_APPLICATION_FLAGS_NONE);
  g_signal_connect (app, "activate", G_CALLBACK (activate_gui), &app_data);

  g_application_add_option_group (G_APPLICATION (app), gst_group);
  status = g_application_run (G_APPLICATION (app), argc, argv);
  g_object_unref (app);

  if (app_data.pipeline != NULL) {
    gst_element_set_state (app_data.pipeline, GST_STATE_NULL);
    g_clear_object (&app_data.pipeline);
  }

  return status;
}