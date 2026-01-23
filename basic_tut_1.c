#include "glib.h"
#include "gst/gstbus.h"
#include "gst/gstclock.h"
#include "gst/gstelement.h"
#include "gst/gstmessage.h"
#include "gst/gstobject.h"
#include "gst/gstparse.h"
#include <gst/gst.h>

int tutorial_main(int argc, char **argv) {

  gst_init(&argc, &argv);

  GstElement *pipeline =
      gst_parse_launch("playbin "
                       "uri=https://gstreamer.freedesktop.org/data/media/"
                       "sintel_trailer-480p.webm",
                       NULL);

  gst_element_set_state(pipeline, GST_STATE_PLAYING);

  GstBus *bus = gst_element_get_bus(pipeline);
  GstMessage *msg = gst_bus_timed_pop_filtered(
      bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
    g_printerr("ERROR OCCURED! RE-RUN WITH GST_DEBUG=*WARN !\n");
  }

  gst_message_unref(msg);
  gst_object_unref(bus);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
  return 0;
}

int main(int argc, char **argv) { return tutorial_main(argc, argv); }