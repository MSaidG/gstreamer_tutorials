#include <glib.h>
#include <gst/gst.h>
#include <stdio.h>

static void on_pad_added(GstElement *element, GstPad *pad, gpointer data) {

  GstCaps *caps = gst_pad_query_caps(pad, NULL);
  GstStructure *str = gst_caps_get_structure(caps, 0);
  const gchar *name = gst_structure_get_name(str);

  if (g_str_has_prefix(name, "video/")) {
    g_print("It has type '%s' from element '%s'.\n", name,
            GST_ELEMENT_NAME(element));
    GstPad *sinkpad = gst_element_get_static_pad(data, "sink");

    if (!gst_pad_is_linked(sinkpad)) {
      if (gst_pad_link(pad, sinkpad) != GST_PAD_LINK_OK) {
        g_printerr("Failed to link demuxer to parser.\n");
      } else {
        g_print("Linked demuxer pad: %s\n", name);
      }
    }
    gst_object_unref(sinkpad);
  }
  gst_caps_unref(caps);
}

void add_stream_to_compositor(GstElement *pipeline, GstElement *compositor,
                              const char *filename, int xpos, int ypos) {

  GstElement *filesrc, *demuxer, *parser, *decoder, *scaler, *capsfilter,
      *queue;

  filesrc = gst_element_factory_make("filesrc", NULL);
  demuxer = gst_element_factory_make("qtdemux", NULL);
  parser = gst_element_factory_make("h264parse", NULL);
  decoder = gst_element_factory_make("omxh264dec", NULL); // v4l2h264dec
  scaler = gst_element_factory_make("videoscale", NULL);
  capsfilter = gst_element_factory_make("capsfilter", NULL);
  queue = gst_element_factory_make("queue", NULL);

  if (!filesrc || !demuxer || !parser || !decoder || !scaler || !capsfilter ||
      !queue) {
    g_printerr("Not all elements could be created for file: %s\n", filename);
    return;
  }

  g_object_set(filesrc, "location", filename, NULL);
  
  g_object_set(scaler, "method", 0, NULL);
  g_object_set(scaler, "add-borders", FALSE, NULL);
  g_object_set(scaler, "dither", 0, NULL);
  g_object_set(scaler, "chroma-resampler", 0, NULL);

  GstCaps *caps = gst_caps_new_simple(
      "video/x-raw", "width", G_TYPE_INT, 960, "height", G_TYPE_INT, 540,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, 1,
      1, // MIGHT REMOVE ASPECT RATIO ACCORDING TO PERFORMANCE RESULT
      NULL);
  g_object_set(capsfilter, "caps", caps, NULL);
  gst_caps_unref(caps);

  gst_bin_add_many(GST_BIN(pipeline), filesrc, demuxer, parser, decoder, scaler,
                   capsfilter, queue, NULL);

  gst_element_link(filesrc, demuxer);
  gst_element_link_many(parser, decoder, scaler, capsfilter, queue, NULL);
  g_signal_connect(demuxer, "pad-added", G_CALLBACK(on_pad_added), parser);

  GstPad *comp_sink_pad = gst_element_request_pad_simple(compositor, "sink_%u");
  g_print("Obtained request pad %s for file %s\n",
          gst_pad_get_name(comp_sink_pad), filename);

  g_object_set(comp_sink_pad, "xpos", xpos, "ypos", ypos, NULL);

  // 7. Link Queue -> Compositor Pad
  GstPad *queue_src_pad = gst_element_get_static_pad(queue, "src");
  if (gst_pad_link(queue_src_pad, comp_sink_pad) != GST_PAD_LINK_OK) {
    g_printerr("Failed to link queue to compositor.\n");
  }

  gst_object_unref(queue_src_pad);
  gst_object_unref(comp_sink_pad);
}

static void on_fps(GstElement *fpssink, gdouble fps, gdouble droprate,
                   gdouble avgfps, gpointer user_data) {
  g_print("FPS: %6.2f | AVG: %6.2f | DROP: %5.2f %%\n", fps, avgfps,
          droprate * 100.0);
}

int main(int argc, char *argv[]) {

  gst_debug_set_threshold_for_name("fpsdisplaysink", GST_LEVEL_TRACE);
  gst_init(&argc, &argv);

  GstElement *pipeline = gst_pipeline_new("video-grid-pipeline");
  GstElement *compositor = gst_element_factory_make("compositor", "comp");
  GstElement *sink = gst_element_factory_make("fpsdisplaysink", "sink");
  GstElement *video_sink = gst_element_factory_make("kmssink", "sink");

  if (!pipeline || !compositor || !sink) {
    g_printerr("Not all top-level elements could be created.\n");
    return -1;
  }

  g_object_set(video_sink, "qos", TRUE, NULL);
  g_object_set(sink, "sync", TRUE, "video-sink", video_sink, "text-overlay",
               FALSE, "signal-fps-measurements", TRUE, NULL);
  g_signal_connect(sink, "fps-measurements", G_CALLBACK(on_fps), NULL);

  gst_bin_add_many(GST_BIN(pipeline), compositor, sink, NULL);

  if (!gst_element_link(compositor, sink)) {
    g_printerr("Compositor could not be linked to sink.\n");
    gst_object_unref(pipeline);
    return -1;
  }

  add_stream_to_compositor(pipeline, compositor, "videos/animals.mp4", 0, 0);
  add_stream_to_compositor(pipeline, compositor, "videos/earth.mp4", 960, 0);
  add_stream_to_compositor(pipeline, compositor, "videos/ocean.mp4", 0, 540);
  add_stream_to_compositor(pipeline, compositor, "videos/galaxy.mp4", 960, 540);

  g_print("Starting pipeline...\n");
  GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr("Unable to set the pipeline to the playing state.\n");
    gst_object_unref(pipeline);
    return -1;
  }

  GstBus *bus = gst_element_get_bus(pipeline);
  GstMessage *msg = gst_bus_timed_pop_filtered(
      bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

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
      break;
    case GST_MESSAGE_EOS:
      g_print("End-Of-Stream reached.\n");
      break;
    case GST_MESSAGE_QOS:
      g_print("QoS drop at sink (Display bottlneck)\n");
    default:
      break;
    }
    gst_message_unref(msg);
  }

  gst_object_unref(bus);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);

  return 0;
}
