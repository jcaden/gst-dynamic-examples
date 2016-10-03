#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#define WINDOW_WIDTH 400
#define WINDOW_HEIGHT 355

#define APP_DATA_INIT {0, NULL}

typedef struct _AppData
{
  guintptr video_window_handle;
  GstElement *pipeline;
} AppData;

static GstBusSyncReply
bus_sync_handler (GstBus *bus, GstMessage *message, gpointer user_data)
{
  AppData *app_data = user_data;

  // ignore anything but 'prepare-window-handle' element messages
  if (!gst_is_video_overlay_prepare_window_handle_message (message)) {
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
  GstElement *pipeline = gst_parse_launch ("v4l2src ! autovideosink", &err);

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

static void
activate (GtkApplication *app, gpointer user_data)
{
  GtkWidget *window;
  GtkWidget *video_window;
  AppData *app_data = user_data;
  GtkWidget *content, *button, *button_box;

  window = gtk_application_window_new (app);
  gtk_window_set_title (GTK_WINDOW (window), "Change filter");
  gtk_window_set_default_size (GTK_WINDOW (window), WINDOW_WIDTH, WINDOW_HEIGHT);

  content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
  gtk_container_add (GTK_CONTAINER (window), content);

  button_box = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_container_add (GTK_CONTAINER (content), button_box);

  button = gtk_button_new_with_label ("Exit");
  g_signal_connect_swapped (button, "clicked", G_CALLBACK (gtk_widget_destroy),
      window);
  gtk_container_add (GTK_CONTAINER (button_box), button);

  video_window = gtk_drawing_area_new ();
  g_signal_connect (video_window, "realize",
      G_CALLBACK (video_widget_realize_cb), app_data);
  gtk_container_add (GTK_CONTAINER (content), video_window);
  gtk_box_set_child_packing (GTK_BOX (content), video_window, TRUE, TRUE, 1,
      GTK_PACK_START);

  gtk_widget_show_all (window);
  gtk_widget_realize (video_window);

  g_assert (app_data->video_window_handle != 0);

  create_pipeline (app_data);
}

int
main (int argc, char **argv)
{
  GtkApplication *app;
  int status;
  AppData app_data = APP_DATA_INIT;

  gst_init (&argc, &argv);

  app = gtk_application_new ("org.kurento.change_filter",
      G_APPLICATION_FLAGS_NONE);
  g_signal_connect (app, "activate", G_CALLBACK (activate), &app_data);
  status = g_application_run (G_APPLICATION (app), argc, argv);
  g_object_unref (app);

  if (app_data.pipeline != NULL) {
    gst_element_set_state (app_data.pipeline, GST_STATE_NULL);
    g_clear_object (&app_data.pipeline);
  }

  return status;
}