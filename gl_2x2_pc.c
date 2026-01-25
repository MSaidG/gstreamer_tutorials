#include <gst/gst.h>
#include <stdio.h>

static void on_pad_added(GstElement *element, GstPad *pad, gpointer data) {
  GstCaps *pad_caps = gst_pad_query_caps(pad, NULL);
  GstStructure *pad_struct = gst_caps_get_structure(pad_caps, 0);
  const gchar *name = gst_structure_get_name(pad_struct);

  if (g_str_has_prefix(name, "video/x-h264")) {
    GstElement *parser = GST_ELEMENT(data);
    GstPad *sinkpad = gst_element_get_static_pad(parser, "sink");
    if (!gst_pad_is_linked(sinkpad)) {
      gst_pad_link(pad, sinkpad);
    }
    gst_object_unref(sinkpad);
  }
  gst_caps_unref(pad_caps);
}

void add_stream_to_gl_mixer(GstElement *pipeline, GstElement *mixer,
                            const char *filename, int xpos, int ypos, int width,
                            int height) {
  GstElement *filesrc, *demuxer, *parser, *decoder, *glupload, *glconvert,
      *queue;

  filesrc = gst_element_factory_make("filesrc", NULL);
  demuxer = gst_element_factory_make("qtdemux", NULL);
  parser = gst_element_factory_make("h264parse", NULL);
  decoder = gst_element_factory_make("avdec_h264", NULL);
  glupload = gst_element_factory_make("glupload", NULL);
  glconvert = gst_element_factory_make("glcolorconvert", NULL);
  queue = gst_element_factory_make("queue", NULL);

  if (!filesrc || !demuxer || !parser || !decoder || !glupload || !glconvert ||
      !queue) {
    g_printerr("Failed to create elements for file: %s\n", filename);
    return;
  }

  g_object_set(filesrc, "location", filename, NULL);

  gst_bin_add_many(GST_BIN(pipeline), filesrc, demuxer, parser, decoder,
                   glupload, glconvert, queue, NULL);

  gst_element_link(filesrc, demuxer);
  gst_element_link_many(parser, decoder, glupload, glconvert, queue, NULL);

  g_signal_connect(demuxer, "pad-added", G_CALLBACK(on_pad_added), parser);

  GstPad *mixer_sink_pad = gst_element_request_pad_simple(mixer, "sink_%u");
  g_object_set(mixer_sink_pad, "xpos", xpos, "ypos", ypos, "width", width,
               "height", height, NULL);

  GstPad *queue_src_pad = gst_element_get_static_pad(queue, "src");
  gst_pad_link(queue_src_pad, mixer_sink_pad);

  gst_object_unref(queue_src_pad);
  gst_object_unref(mixer_sink_pad);
}

static void on_fps(GstElement *fpssink, gdouble fps, gdouble droprate,
                   gdouble avgfps, gpointer user_data) {
  g_print("FPS: %6.2f | AVG: %6.2f | DROP: %5.2f %%\n", fps, avgfps,
          droprate * 100.0);
}

int main(int argc, char *argv[]) {
  gst_init(&argc, &argv);

  GstElement *pipeline = gst_pipeline_new("gl-optimized-pipeline");
  GstElement *mixer = gst_element_factory_make("glvideomixer", "mixer");

  // 1. GPU Color Conversion
  // We perform the heavy conversion here using the GPU
  GstElement *gl_convert_final =
      gst_element_factory_make("glcolorconvert", "final-gl-conv");

  // 2. Download to System Memory
  // This performs a raw copy (fast) rather than a conversion (slow)
  GstElement *download = gst_element_factory_make("gldownload", "download");

  // 3. Enforce Output Format
  // We force RGBA or BGRA here to ensure 'glcolorconvert' did the job
  // and we aren't relying on CPU conversion later.
  GstElement *capsfilter =
      gst_element_factory_make("capsfilter", "format-fixer");

  // 4. Sink
  GstElement *sink = gst_element_factory_make("fpsdisplaysink", "sink");
  GstElement *video_sink = gst_element_factory_make("kmssink", "video_sink");

  if (!pipeline || !mixer || !gl_convert_final || !download || !capsfilter ||
      !sink) {
    g_printerr("Critical elements creation failed!\n");
    return -1;
  }

  // Turn off sync for max throughput
  g_object_set(sink, "sync", TRUE, "video-sink", video_sink, "text-overlay",
               FALSE, "signal-fps-measurements", TRUE, NULL);
  g_signal_connect(sink, "fps-measurements", G_CALLBACK(on_fps), NULL);

  // We explicitly ask for standard video/x-raw (System Memory)
  // but we restrict formats to what typical GPU/KMS combos handle well
  // (RGBA/BGRA)
  GstCaps *caps = gst_caps_from_string(
      "video/x-raw, format={ (string)BGRA, (string)RGBA, (string)BGRx }");
  g_object_set(capsfilter, "caps", caps, NULL);
  gst_caps_unref(caps);

  gst_bin_add_many(GST_BIN(pipeline), mixer, gl_convert_final, download,
                   capsfilter, sink, NULL);

  // Link: Mixer -> GLConvert -> Download -> CapsFilter -> KMSSink
  if (!gst_element_link_many(mixer, gl_convert_final, download, capsfilter,
                             sink, NULL)) {
    g_printerr("Failed to link the output chain.\n");
    gst_object_unref(pipeline);
    return -1;
  }

  add_stream_to_gl_mixer(pipeline, mixer, "videos/animals.mp4", 0, 0, 960, 540);
  add_stream_to_gl_mixer(pipeline, mixer, "videos/earth1.mp4", 960, 0, 960, 540);
  add_stream_to_gl_mixer(pipeline, mixer, "videos/ocean.mp4", 0, 540, 960, 540);
  add_stream_to_gl_mixer(pipeline, mixer, "videos/galaxy.mp4", 960, 540, 960,
                         540);

  g_print("Starting Optimized One-Copy Pipeline...\n");
  gst_element_set_state(pipeline, GST_STATE_PLAYING);

  GstBus *bus = gst_element_get_bus(pipeline);
  GstMessage *msg = gst_bus_timed_pop_filtered(
      bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  if (msg != NULL) {
    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
      GError *err;
      gchar *debug;
      gst_message_parse_error(msg, &err, &debug);
      g_printerr("Error: %s\n", err->message);
      g_printerr("Debug: %s\n", debug);
      g_error_free(err);
      g_free(debug);
    }
    gst_message_unref(msg);
  }

  gst_object_unref(bus);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);

  return 0;
}
