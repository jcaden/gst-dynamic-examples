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
#define WINDOW_HEIGHT 400

#define SRC_NAME "src"
#define FILTER_NAME "filter"
#define TEE_NAME "tee"
#define FILE_SINK_NAME "file_sink"

#define FILE_LOCATION "/tmp/test.mkv"

#define ADD_CLOCK_OVERLAY "Add clockoverlay"
#define REMOVE_CLOCK_OVERLAY "Remove clockoverlay"

#define START_RECORDING "Start recording"
#define STOP_RECORDING "Stop recording"

#define APP_DATA_INIT {0, NULL, FALSE, NULL, NULL, NULL, FALSE, NULL}

typedef struct _AppData
{
  guintptr video_window_handle;
  GstElement *pipeline;
  gboolean filter_set;
  GtkWidget *status_bar;
  GtkWidget *filter_button;
  GtkWidget *record_button;
  gboolean recording;
  GstPad *recording_sink;
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
      gst_parse_launch (
          "v4l2src name=" SRC_NAME " ! tee name=" TEE_NAME
              " ! queue ! autovideosink name=sink", &err);

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

static gboolean
update_filter_widgets_status (AppData *app_data)
{
  const gchar *message;

  if (g_atomic_int_get (&app_data->filter_set)) {
    message = "Filter added correctly";
  } else {
    message = "Filter removed correctly";
  }
  gtk_statusbar_push (GTK_STATUSBAR (app_data->status_bar), 0,
      message);
  gtk_widget_set_sensitive (app_data->filter_button, TRUE);

  return G_SOURCE_REMOVE;
}

static gboolean
update_recording_widgets_status (AppData *app_data)
{
  const gchar *message;

  if (g_atomic_int_get (&app_data->recording)) {
    message = "Recording started correctly";
  } else {
    message = "Recording stopped correctly";
    /* This is always modified from gui thread */
    g_clear_object (&app_data->recording_sink);
  }
  gtk_statusbar_push (GTK_STATUSBAR (app_data->status_bar), 0,
      message);
  gtk_widget_set_sensitive (app_data->record_button, TRUE);

  return G_SOURCE_REMOVE;
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

  g_atomic_int_set (&app_data->filter_set, TRUE);
  g_idle_add ((GSourceFunc) update_filter_widgets_status, app_data);

  return GST_PAD_PROBE_REMOVE;
}

static void
connect_new_filter (AppData *app_data)
{
  GstPad *src_pad;
  GstElement *src;

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

  g_atomic_int_set (&app_data->filter_set, FALSE);
  g_idle_add ((GSourceFunc) update_filter_widgets_status, app_data);

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

  if (g_atomic_int_get (&app_data->filter_set)) {
    gtk_button_set_label (GTK_BUTTON (app_data->filter_button),
        ADD_CLOCK_OVERLAY);
    disconnect_filter (app_data);
  } else {
    gtk_button_set_label (GTK_BUTTON (app_data->filter_button),
        REMOVE_CLOCK_OVERLAY);
    connect_new_filter (app_data);
  }
}

static void
start_recording (AppData *app_data)
{
  GError *error = NULL;
  GstPad *tee_src;
  GstElement *tee, *recording_bin;

  tee = gst_bin_get_by_name (GST_BIN (app_data->pipeline), TEE_NAME);
  g_assert (tee);
  tee_src = gst_element_get_request_pad (tee, "src_%u");
  g_assert (tee_src);

  /* We add a videoconvert to avoid caps renegotiation that could stop the camera */
  /* Vp8enc is configured for real time otherwise the buffers will be delayed */
  recording_bin = gst_parse_bin_from_description (
      "queue max-size-buffers=0 ! videoconvert !"
          " vp8enc deadline=1 threads=1 ! matroskamux !"
          " filesink name=" FILE_SINK_NAME " sync=false location=" FILE_LOCATION,
      TRUE, &error);

  if (recording_bin == NULL) {
    GST_ERROR ("Error creating bin: %s", error->message);
    g_clear_error (&error);
    g_assert_not_reached ();
  }

  gst_bin_add (GST_BIN (app_data->pipeline), recording_bin);
  gst_element_sync_state_with_parent (recording_bin);

  g_assert (
      gst_element_link_pads (tee, GST_OBJECT_NAME (tee_src), recording_bin,
          NULL));

  if (app_data->recording_sink) {
    GST_ERROR ("Recording sink is already set, this should not happen");
    g_assert (!app_data->recording_sink);
  }

  /* This is always modified from gui thread */
  app_data->recording_sink = gst_pad_get_peer (tee_src);

  g_object_unref (tee_src);
  g_object_unref (tee);

  g_atomic_int_set (&app_data->recording, TRUE);
  g_idle_add ((GSourceFunc) update_recording_widgets_status, app_data);
}

static gboolean
release_recording_bin (gpointer recording_bin)
{
  gst_element_set_state (recording_bin, GST_STATE_NULL);

  return G_SOURCE_REMOVE;
}

static GstPadProbeReturn
filesink_eos_probe (GstPad *tee_src, GstPadProbeInfo *info, gpointer data)
{
  GstElement *recording_bin;
  AppData *app_data = data;

#if !BUGGY_CODE
  {
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);

    if (GST_EVENT_TYPE (event) != GST_EVENT_EOS) {
      return GST_PAD_PROBE_OK;
    }
  }
#endif
  recording_bin = gst_pad_get_parent_element (app_data->recording_sink);
  g_assert (recording_bin);

  gst_bin_remove (GST_BIN (app_data->pipeline), recording_bin);

  /* State should be changed from a different thread, as this is queue
   * streaming thread if not buggy code */
  g_idle_add_full (G_PRIORITY_DEFAULT, release_recording_bin, recording_bin,
      g_object_unref);

  g_atomic_int_set (&app_data->recording, FALSE);
  g_idle_add ((GSourceFunc) update_recording_widgets_status, app_data);

  return GST_PAD_PROBE_REMOVE;
}

static GstPadProbeReturn
stop_recording_probe (GstPad *tee_src, GstPadProbeInfo *info, gpointer data)
{
  AppData *app_data = data;
  GstElement *tee, *filesink;
  GstPad *filesink_sink;

  g_assert (app_data->recording_sink);

  gst_pad_unlink (tee_src, app_data->recording_sink);

  filesink = gst_bin_get_by_name_recurse_up (GST_BIN (app_data->pipeline),
      FILE_SINK_NAME);
  g_assert (filesink);
  filesink_sink = gst_element_get_static_pad (filesink, "sink");
  g_assert (filesink_sink);

#if !BUGGY_CODE
  /* Send eos event to finish file correctly */
  gst_pad_add_probe (filesink_sink, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      filesink_eos_probe, app_data, NULL);
  gst_pad_send_event (app_data->recording_sink, gst_event_new_eos ());
#else
  gst_pad_send_event (app_data->recording_sink, gst_event_new_eos ());
  filesink_eos_probe (filesink_sink, NULL, app_data);
#endif

  g_object_unref (filesink_sink);
  g_object_unref (filesink);

  tee = gst_pad_get_parent_element (tee_src);
  g_assert (tee);
  gst_element_remove_pad (tee, tee_src);
  g_object_unref (tee);

  return GST_PAD_PROBE_REMOVE;
}

static void
stop_recording (AppData *app_data)
{
  GstPad *tee_src;

  if (!app_data->recording_sink) {
    GST_ERROR ("Recording sink is not set, this should not happen");
    g_assert (app_data->recording_sink);
  }

  tee_src = gst_pad_get_peer (app_data->recording_sink);
  g_assert (tee_src);

  /* Remember to block on src pad */
#if !BUGGY_CODE
  gst_pad_add_probe (tee_src, GST_PAD_PROBE_TYPE_IDLE, stop_recording_probe,
      app_data, NULL);
#else
  stop_recording_probe (tee_src, NULL, app_data);
#endif

  g_object_unref (tee_src);
}

static void
record_button_clicked (AppData *app_data)
{
  gtk_widget_set_sensitive (app_data->record_button, FALSE);

  if (g_atomic_int_get (&app_data->recording)) {
    gtk_button_set_label (GTK_BUTTON (app_data->record_button),
        START_RECORDING);
    stop_recording (app_data);
  } else {
    gtk_button_set_label (GTK_BUTTON (app_data->record_button),
        STOP_RECORDING);
    start_recording (app_data);
  }
}

static void
activate_gui (GtkApplication *app, gpointer user_data)
{
  GtkWidget *window, *video_widget, *window_content, *filter_button,
      *record_button, *button_box, *status_bar;
  AppData *app_data = user_data;

  window = gtk_application_window_new (app);
  gtk_window_set_title (GTK_WINDOW (window), "Change filter");
  gtk_window_set_default_size (GTK_WINDOW (window), WINDOW_WIDTH,
      WINDOW_HEIGHT);

  window_content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  gtk_container_add (GTK_CONTAINER (window), window_content);

  button_box = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_container_add (GTK_CONTAINER (window_content), button_box);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_EXPAND);

  filter_button = gtk_button_new_with_label (ADD_CLOCK_OVERLAY);
  g_signal_connect_swapped (filter_button, "clicked",
      G_CALLBACK (filter_button_clicked), app_data);
  gtk_container_add (GTK_CONTAINER (button_box), filter_button);
  app_data->filter_button = filter_button;

  record_button = gtk_button_new_with_label (START_RECORDING);
  g_signal_connect_swapped (record_button, "clicked",
      G_CALLBACK (record_button_clicked), app_data);
  gtk_container_add (GTK_CONTAINER (button_box), record_button);
  app_data->record_button = record_button;

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

  if (app_data.recording_sink != NULL) {
    g_clear_object (&app_data.recording_sink);
  }

  if (app_data.pipeline != NULL) {
    gst_element_send_event (app_data.pipeline, gst_event_new_eos ());
    gst_element_set_state (app_data.pipeline, GST_STATE_NULL);
    g_clear_object (&app_data.pipeline);
  }

  gst_deinit ();

  return status;
}
