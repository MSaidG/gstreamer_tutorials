#include "glib-object.h"
#include "glib.h"
#include "gst/gstbin.h"
#include "gst/gstcaps.h"
#include "gst/gstelement.h"
#include "gst/gstelementfactory.h"
#include "gst/gstmessage.h"
#include "gst/gstobject.h"
#include "gst/gstpipeline.h"
#include "gst/gststructure.h"
#include "gst/gstutils.h"
#include <gst/gst.h>

typedef struct _CustomData {
  GstElement *pipeline;
  GstElement *source;
  GstElement *convert;
  GstElement *v_convert;
  GstElement *resample;
  GstElement *sink;
  GstElement *v_sink;
} CustomData;

static void pad_added_handler(GstElement *src, GstPad *pad, CustomData *data);

int main(int argc, char **argv) {

  gst_init(&argc, &argv);

  CustomData data = {
      .pipeline = gst_pipeline_new("test-pipeline"),
      .source = gst_element_factory_make("uridecodebin", "source"),
      .convert = gst_element_factory_make("audioconvert", "convert"),
      .v_convert = gst_element_factory_make("videoconvert", "v_convert"),
      .resample = gst_element_factory_make("audioresample", "resample"),
      .sink = gst_element_factory_make("autoaudiosink", "sink"),
      .v_sink = gst_element_factory_make("autovideosink", "v_sink"),
  };

  if (!data.pipeline || !data.source || !data.convert || !data.resample ||
      !data.sink || !data.v_convert || !data.v_sink) {
    g_printerr("Element creation failed!");
    return -1;
  }

  gst_bin_add_many(GST_BIN(data.pipeline), data.source, data.convert,
                   data.resample, data.sink, data.v_convert, data.v_sink, NULL);

  if (!gst_element_link_many(data.convert, data.resample, data.sink, NULL)) {
    g_printerr("Audio elements couldn't be linked.\n");
    gst_object_unref(data.pipeline);
    return -1;
  }

  if (!gst_element_link(data.v_convert, data.v_sink)) {
    g_printerr("Video elements couldn't be linked.\n");
    gst_object_unref(data.pipeline);
    return -1;
  }

  g_object_set(
      data.source, "uri",
      "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm",
      NULL);

  g_signal_connect(data.source, "pad_added", G_CALLBACK(pad_added_handler),
                   &data);

  GstStateChangeReturn ret =
      gst_element_set_state(data.pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr("Couldn't set to playing state!");
    return -1;
  }

  GstBus *bus = gst_element_get_bus(data.pipeline);
  GstMessage *msg;

  gboolean terminate = FALSE;
  while (!terminate) {
    msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                     GST_MESSAGE_STATE_CHANGED |
                                         GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    /* Parse message */
    if (msg != NULL) {
      GError *err;
      gchar *debug_info;

      switch (GST_MESSAGE_TYPE(msg)) {
      case GST_MESSAGE_ERROR:
        gst_message_parse_error(msg, &err, &debug_info);
        g_printerr("Error received from element %s: %s\n",
                   GST_OBJECT_NAME(msg->src), err->message);
        g_printerr("Debugging information: %s\n",
                   debug_info ? debug_info : "none");
        g_clear_error(&err);
        g_free(debug_info);
        terminate = TRUE;
        break;
      case GST_MESSAGE_EOS:
        g_print("End-Of-Stream reached.\n");
        terminate = TRUE;
        break;
      case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(data.pipeline)) {
          GstState old_state, new_state, pending_state;
          gst_message_parse_state_changed(msg, &old_state, &new_state,
                                          &pending_state);
          g_print("Pipeline state changed from %s to %s:\n",
                  gst_element_state_get_name(old_state),
                  gst_element_state_get_name(new_state));
        }
        break;
      default:
        g_printerr("Unexpected message received.\n");
        break;
      }
      gst_message_unref(msg);
    }
  }

  gst_object_unref(bus);
  gst_element_set_state(data.pipeline, GST_STATE_NULL);
  gst_object_unref(data.pipeline);
  return 0;
}

static void pad_added_handler(GstElement *src, GstPad *new_pad,
                              CustomData *data) {

  g_print("Received new pad '%s' from '%s':\n", GST_PAD_NAME(new_pad),
          GST_ELEMENT_NAME(src));

  GstPad *sink_pad = NULL;
  GstCaps *new_pad_caps = gst_pad_get_current_caps(new_pad);
  GstStructure *new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
  const gchar *new_pad_type = gst_structure_get_name(new_pad_struct);

  if (g_str_has_prefix(new_pad_type, "audio/x-raw")) {
    sink_pad = gst_element_get_static_pad(data->convert, "sink");
  } else if (g_str_has_prefix(new_pad_type, "video/x-raw")) {
    sink_pad = gst_element_get_static_pad(data->v_convert, "sink");
  } else {
    g_print("It has type '%s' which is not a supported format. Ignoring.\n",
            new_pad_type);

    if (new_pad_caps != NULL) {
      gst_caps_unref(new_pad_caps);
    }
    gst_object_unref(sink_pad);
    return;
  }

  if (gst_pad_is_linked(sink_pad)) {
    g_print("Already linked. Ignoring.\n");

    gst_object_unref(sink_pad);
    return;
  }

  GstPadLinkReturn ret = gst_pad_link(new_pad, sink_pad);
  if (GST_PAD_LINK_FAILED(ret)) {
    g_print("Link failed. (type '%s').\n", new_pad_type);
  } else {
    g_print("Link succeeded. (type '%s').\n", new_pad_type);
  }

  if (new_pad_caps != NULL) {
    gst_caps_unref(new_pad_caps);
  }
  gst_object_unref(sink_pad);
}
