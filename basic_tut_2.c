#include "glib.h"
#include "gst/gstbin.h"
#include "gst/gstbus.h"
#include "gst/gstclock.h"
#include "gst/gstelement.h"
#include "gst/gstelementfactory.h"
#include "gst/gstmessage.h"
#include "gst/gstobject.h"
#include "gst/gstpipeline.h"
#include "gst/gstutils.h"
#include <gst/gst.h>

int tutorial_main(int argc, char **argv) {

  gst_init(&argc, &argv);

  GstElement *source = gst_element_factory_make("videotestsrc", "source");
  GstElement *sink = gst_element_factory_make("autovideosink", "sink");
  GstElement *filter = gst_element_factory_make("vertigotv", "filter");

  GstElement *pipeline = gst_pipeline_new("test-pipeline");

  if (!source || !sink || !pipeline || !filter) {
    g_printerr("All elemnts couldn't be created!");
    return -1;
  }

  gst_bin_add_many(GST_BIN(pipeline), source, filter, sink, NULL);
  if (gst_element_link(source, filter) != TRUE) {
    g_printerr("Source and filter couldn't be linked.\n");
    gst_object_unref(pipeline);
    return -1;
  }

  if (gst_element_link(filter, sink) != TRUE) {
    g_printerr("Filter and sink couldn't be linked.\n");
    gst_object_unref(pipeline);
    return -1;
  }

  g_object_set(source, "pattern", 0, NULL);
  g_object_set(filter, "speed", 0.1, NULL);

  GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr("Couldn't set to playing state!");
    gst_object_unref(pipeline);
    return -1;
  }

  GstBus *bus = gst_element_get_bus(pipeline);
  GstMessage *msg = gst_bus_timed_pop_filtered(
      bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  if (msg != NULL) {

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
      GError *err;
      gchar *debug_info;
      gst_message_parse_error(msg, &err, &debug_info);
      g_printerr("Error received from element %s: %s\n",
                 GST_OBJECT_NAME(msg->src), err->message);
      g_printerr("Debugging info: %s\n", debug_info ? debug_info : "none");
      g_clear_error(&err);
      g_free(debug_info);
      break;
    }
    case GST_MESSAGE_EOS:
      g_print("End of stream.\n");
      break;
    default:
      g_printerr("Unknown error received.\n");
      break;
    }

    gst_message_unref(msg);
  }

  gst_object_unref(bus);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);

  return 0;
}


int main(int argc, char **argv) {
    return tutorial_main(argc, argv);
}
