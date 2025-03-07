/**
 * @file	unittest_sink.cpp
 * @date	29 June 2018
 * @brief	Unit test for tensor sink plugin
 * @see		https://github.com/nnsuite/nnstreamer
 * @see		https://github.sec.samsung.net/STAR/nnstreamer
 * @author	Jaeyun Jung <jy1210.jung@samsung.com>
 * @bug		No known bugs.
 */

#include <string.h>
#include <gtest/gtest.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <tensor_common.h>

/**
 * @brief Macro for debug mode.
 */
#ifndef DBG
#define DBG FALSE
#endif

/**
 * @brief Macro for debug message.
 */
#define _print_log(...) if (DBG) g_message (__VA_ARGS__)

/**
 * @brief Macro to check error case.
 */
#define _check_cond_err(cond) \
  if (!(cond)) { \
    _print_log ("test failed!! [line : %d]", __LINE__); \
    goto error; \
  }

/**
 * @brief Current status.
 */
typedef enum
{
  TEST_START, /**< start to setup pipeline */
  TEST_INIT, /**< init done */
  TEST_ERR_MESSAGE, /**< received error message */
  TEST_STREAM, /**< stream started */
  TEST_EOS /**< end of stream */
} TestStatus;

/**
 * @brief Test type.
 */
typedef enum
{
  TEST_TYPE_VIDEO_RGB, /**< pipeline for video (RGB) */
  TEST_TYPE_VIDEO_RGB_PADDING, /**< pipeline for video (RGB), remove padding */
  TEST_TYPE_VIDEO_RGB_3F, /**< pipeline for video (RGB) 3 frames */
  TEST_TYPE_VIDEO_BGRx, /**< pipeline for video (BGRx) */
  TEST_TYPE_VIDEO_BGRx_2F, /**< pipeline for video (BGRx) 2 frames */
  TEST_TYPE_VIDEO_GRAY8, /**< pipeline for video (GRAY8) */
  TEST_TYPE_VIDEO_GRAY8_PADDING, /**< pipeline for video (GRAY8), remove padding */
  TEST_TYPE_VIDEO_GRAY8_3F_PADDING, /**< pipeline for video (GRAY8) 3 frames, remove padding */
  TEST_TYPE_AUDIO_S8, /**< pipeline for audio (S8) */
  TEST_TYPE_AUDIO_U8_100F, /**< pipeline for audio (U8) 100 frames */
  TEST_TYPE_AUDIO_S16, /**< pipeline for audio (S16) */
  TEST_TYPE_AUDIO_U16_1000F, /**< pipeline for audio (U16) 1000 frames */
  TEST_TYPE_TEXT, /**< pipeline for text */
  TEST_TYPE_TEXT_3F, /**< pipeline for text 3 frames */
  TEST_TYPE_TENSORS, /**< pipeline for tensors with tensormux */
  TEST_TYPE_NEGO_FAILED, /**< pipeline to test caps negotiation */
  TEST_TYPE_VIDEO_RGB_AGGR, /**< pipeline to test tensor-aggregator */
  TEST_TYPE_AUDIO_S16_AGGR, /**< pipeline to test tensor-aggregator */
  TEST_TYPE_AUDIO_U16_AGGR, /**< pipeline to test tensor-aggregator */
  TEST_TYPE_TYPECAST, /**< pipeline for typecast with tensor-transform */
  TEST_TYPE_UNKNOWN /**< unknonwn */
} TestType;

/**
 * @brief Test options.
 */
typedef struct
{
  guint num_buffers; /**< count of buffers */
  TestType test_type; /**< test pipeline */
  tensor_type t_type; /**< tensor type */
} TestOption;

/**
 * @brief Data structure for test.
 */
typedef struct
{
  GMainLoop *loop; /**< main event loop */
  GstElement *pipeline; /**< gst pipeline for test */
  GstBus *bus; /**< gst bus for test */
  GstElement *sink; /**< tensor sink element */
  TestStatus status; /**< current status */
  TestType tc_type; /**< pipeline for testcase type */
  tensor_type t_type; /**< tensor type */
  guint received; /**< received buffer count */
  gsize received_size; /**< received buffer size */
  gboolean start; /**< stream started */
  gboolean end; /**< eos reached */
  gchar *caps_name; /**< negotiated caps name */
  GstTensorConfig tensor_config; /**< tensor config from negotiated caps */
  GstTensorsConfig tensors_config; /**< tensors config from negotiated caps */
} TestData;

/**
 * @brief Data for pipeline and test result.
 */
static TestData g_test_data;

/**
 * @brief Free resources in test data.
 */
static void
_free_test_data (void)
{
  if (g_test_data.loop) {
    g_main_loop_unref (g_test_data.loop);
    g_test_data.loop = NULL;
  }

  if (g_test_data.bus) {
    gst_bus_remove_signal_watch (g_test_data.bus);
    gst_object_unref (g_test_data.bus);
    g_test_data.bus = NULL;
  }

  if (g_test_data.sink) {
    gst_object_unref (g_test_data.sink);
    g_test_data.sink = NULL;
  }

  if (g_test_data.pipeline) {
    gst_object_unref (g_test_data.pipeline);
    g_test_data.pipeline = NULL;
  }
}

/**
 * @brief Callback for message.
 */
static void
_message_cb (GstBus * bus, GstMessage * message, gpointer user_data)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    case GST_MESSAGE_WARNING:
      _print_log ("received error message");
      g_test_data.status = TEST_ERR_MESSAGE;
      g_main_loop_quit (g_test_data.loop);
      break;

    case GST_MESSAGE_EOS:
      _print_log ("received eos message");
      g_test_data.status = TEST_EOS;
      g_main_loop_quit (g_test_data.loop);
      break;

    case GST_MESSAGE_STREAM_START:
      _print_log ("received start message");
      g_test_data.status = TEST_STREAM;
      break;

    default:
      break;
  }
}

/**
 * @brief Callback for signal new-data.
 */
static void
_new_data_cb (GstElement * element, GstBuffer * buffer, gpointer user_data)
{
  g_test_data.received++;
  g_test_data.received_size = gst_buffer_get_size (buffer);

  _print_log ("new data callback [%d] size [%zd]",
      g_test_data.received, g_test_data.received_size);

  if (DBG) {
    GstClockTime pts, dts;

    pts = GST_BUFFER_PTS (buffer);
    dts = GST_BUFFER_DTS (buffer);

    _print_log ("pts %" GST_TIME_FORMAT, GST_TIME_ARGS (pts));
    _print_log ("dts %" GST_TIME_FORMAT, GST_TIME_ARGS (dts));
    _print_log ("number of memory blocks %d", gst_buffer_n_memory (buffer));
  }

  if (g_test_data.caps_name == NULL) {
    GstPad *sink_pad;
    GstCaps *caps;
    GstStructure *structure;

    /** get negotiated caps */
    sink_pad = gst_element_get_static_pad (element, "sink");
    caps = gst_pad_get_current_caps (sink_pad);
    structure = gst_caps_get_structure (caps, 0);

    g_test_data.caps_name = (gchar *) gst_structure_get_name (structure);
    _print_log ("caps name [%s]", g_test_data.caps_name);

    if (g_str_equal (g_test_data.caps_name, "other/tensor")) {
      if (!gst_tensor_config_from_structure (&g_test_data.tensor_config,
              structure)) {
        _print_log ("failed to get tensor config from caps");
      }
    } else if (g_str_equal (g_test_data.caps_name, "other/tensors")) {
      if (!gst_tensors_config_from_structure (&g_test_data.tensors_config,
              structure)) {
        _print_log ("failed to get tensors config from caps");
      }
    }

    gst_caps_unref (caps);
  }
}

/**
 * @brief Callback for signal stream-start.
 */
static void
_stream_start_cb (GstElement * element, GstBuffer * buffer, gpointer user_data)
{
  g_test_data.start = TRUE;
  _print_log ("stream start callback");
}

/**
 * @brief Callback for signal eos.
 */
static void
_eos_cb (GstElement * element, GstBuffer * buffer, gpointer user_data)
{
  g_test_data.end = TRUE;
  _print_log ("eos callback");
}

/**
 * @brief Push text data to appsrc for text utf8 type.
 */
static gboolean
_push_text_data (const guint num_buffers)
{
  GstElement *appsrc;
  gboolean failed = FALSE;
  guint i;

  appsrc = gst_bin_get_by_name (GST_BIN (g_test_data.pipeline), "appsrc");

  for (i = 0; i < num_buffers; i++) {
    GstBuffer *buf = gst_buffer_new_allocate (NULL, 10, NULL);
    GstMapInfo info;

    gst_buffer_map (buf, &info, GST_MAP_WRITE);
    sprintf ((char *) info.data, "%d", i);
    gst_buffer_unmap (buf, &info);

    GST_BUFFER_PTS (buf) = (i + 1) * 10 * GST_MSECOND;
    GST_BUFFER_DTS (buf) = GST_BUFFER_PTS (buf);

    if (gst_app_src_push_buffer (GST_APP_SRC (appsrc), buf) != GST_FLOW_OK) {
      _print_log ("failed to push buffer [%d]", i);
      failed = TRUE;
      goto error;
    }
  }

  if (gst_app_src_end_of_stream (GST_APP_SRC (appsrc)) != GST_FLOW_OK) {
    _print_log ("failed to set eos");
    failed = TRUE;
    goto error;
  }

error:
  gst_object_unref (appsrc);
  return !failed;
}

/**
 * @brief Prepare test pipeline.
 */
static gboolean
_setup_pipeline (TestOption & option)
{
  gchar *str_pipeline;
  gulong handle_id;

  g_test_data.status = TEST_START;
  g_test_data.received = 0;
  g_test_data.received_size = 0;
  g_test_data.start = FALSE;
  g_test_data.end = FALSE;
  g_test_data.caps_name = NULL;
  g_test_data.tc_type = option.test_type;
  g_test_data.t_type = option.t_type;
  gst_tensor_config_init (&g_test_data.tensor_config);
  gst_tensors_config_init (&g_test_data.tensors_config);

  _print_log ("option num_buffers[%d] test_type[%d]",
      option.num_buffers, option.test_type);

  g_test_data.loop = g_main_loop_new (NULL, FALSE);
  _check_cond_err (g_test_data.loop != NULL);

  switch (option.test_type) {
    case TEST_TYPE_VIDEO_RGB:
      /** video 160x120 RGB */
      str_pipeline =
          g_strdup_printf
          ("videotestsrc num-buffers=%d ! videoconvert ! video/x-raw,width=160,height=120,format=RGB,framerate=(fraction)30/1 ! "
          "tensor_converter ! tensor_sink name=test_sink", option.num_buffers);
      break;
    case TEST_TYPE_VIDEO_RGB_PADDING:
      /** video 162x120 RGB, remove padding */
      str_pipeline =
          g_strdup_printf
          ("videotestsrc num-buffers=%d ! videoconvert ! video/x-raw,width=162,height=120,format=RGB,framerate=(fraction)30/1 ! "
          "tensor_converter ! tensor_sink name=test_sink", option.num_buffers);
      break;
    case TEST_TYPE_VIDEO_RGB_3F:
      /** video 160x120 RGB, 3 frames */
      str_pipeline =
          g_strdup_printf
          ("videotestsrc num-buffers=%d ! videoconvert ! video/x-raw,width=160,height=120,format=RGB,framerate=(fraction)30/1 ! "
          "tensor_converter frames-per-tensor=3 ! tensor_sink name=test_sink",
          option.num_buffers);
      break;
    case TEST_TYPE_VIDEO_BGRx:
      /** video 160x120 BGRx */
      str_pipeline =
          g_strdup_printf
          ("videotestsrc num-buffers=%d ! videoconvert ! video/x-raw,width=160,height=120,format=BGRx,framerate=(fraction)30/1 ! "
          "tensor_converter ! tensor_sink name=test_sink", option.num_buffers);
      break;
    case TEST_TYPE_VIDEO_BGRx_2F:
      /** video 160x120 BGRx, 2 frames */
      str_pipeline =
          g_strdup_printf
          ("videotestsrc num-buffers=%d ! videoconvert ! video/x-raw,width=160,height=120,format=BGRx,framerate=(fraction)30/1 ! "
          "tensor_converter frames-per-tensor=2 ! tensor_sink name=test_sink",
          option.num_buffers);
      break;
    case TEST_TYPE_VIDEO_GRAY8:
      /** video 160x120 GRAY8 */
      str_pipeline =
          g_strdup_printf
          ("videotestsrc num-buffers=%d ! videoconvert ! video/x-raw,width=160,height=120,format=GRAY8,framerate=(fraction)30/1 ! "
          "tensor_converter ! tensor_sink name=test_sink", option.num_buffers);
      break;
    case TEST_TYPE_VIDEO_GRAY8_PADDING:
      /** video 162x120 GRAY8, remove padding */
      str_pipeline =
          g_strdup_printf
          ("videotestsrc num-buffers=%d ! videoconvert ! video/x-raw,width=162,height=120,format=GRAY8,framerate=(fraction)30/1 ! "
          "tensor_converter ! tensor_sink name=test_sink", option.num_buffers);
      break;
    case TEST_TYPE_VIDEO_GRAY8_3F_PADDING:
      /** video 162x120 GRAY8, 3 frames, remove padding */
      str_pipeline =
          g_strdup_printf
          ("videotestsrc num-buffers=%d ! videoconvert ! video/x-raw,width=162,height=120,format=GRAY8,framerate=(fraction)30/1 ! "
          "tensor_converter frames-per-tensor=3 ! tensor_sink name=test_sink",
          option.num_buffers);
      break;
    case TEST_TYPE_AUDIO_S8:
      /** audio sample rate 16000 (8 bits, signed, little endian) */
      str_pipeline =
          g_strdup_printf
          ("audiotestsrc num-buffers=%d samplesperbuffer=500 ! audioconvert ! audio/x-raw,format=S8,rate=16000 ! "
          "tensor_converter frames-per-tensor=500 ! tensor_sink name=test_sink",
          option.num_buffers);
      break;
    case TEST_TYPE_AUDIO_U8_100F:
      /** audio sample rate 16000 (8 bits, unsigned, little endian), 100 frames */
      str_pipeline =
          g_strdup_printf
          ("audiotestsrc num-buffers=%d samplesperbuffer=500 ! audioconvert ! audio/x-raw,format=U8,rate=16000 ! "
          "tensor_converter frames-per-tensor=100 ! tensor_sink name=test_sink",
          option.num_buffers);
      break;
    case TEST_TYPE_AUDIO_S16:
      /** audio sample rate 16000 (16 bits, signed, little endian) */
      str_pipeline =
          g_strdup_printf
          ("audiotestsrc num-buffers=%d samplesperbuffer=500 ! audioconvert ! audio/x-raw,format=S16LE,rate=16000 ! "
          "tensor_converter frames-per-tensor=500 ! tensor_sink name=test_sink",
          option.num_buffers);
      break;
    case TEST_TYPE_AUDIO_U16_1000F:
      /** audio sample rate 16000 (16 bits, unsigned, little endian), 1000 frames */
      str_pipeline =
          g_strdup_printf
          ("audiotestsrc num-buffers=%d samplesperbuffer=500 ! audioconvert ! audio/x-raw,format=U16LE,rate=16000 ! "
          "tensor_converter frames-per-tensor=1000 ! tensor_sink name=test_sink",
          option.num_buffers);
      break;
    case TEST_TYPE_TEXT:
      /** text stream */
      str_pipeline =
          g_strdup_printf
          ("appsrc name=appsrc caps=text/x-raw,format=utf8 ! "
          "tensor_converter ! tensor_sink name=test_sink");
      break;
    case TEST_TYPE_TEXT_3F:
      /** text stream 3 frames */
      str_pipeline =
          g_strdup_printf
          ("appsrc name=appsrc caps=text/x-raw,format=utf8 ! "
          "tensor_converter frames-per-tensor=3 ! tensor_sink name=test_sink");
      break;
      break;
    case TEST_TYPE_TENSORS:
      /** other/tensors with tensormux */
      str_pipeline =
          g_strdup_printf
          ("tensormux name=mux ! tensor_sink name=test_sink "
          "videotestsrc num-buffers=%d ! video/x-raw,width=160,height=120,format=RGB,framerate=(fraction)30/1 ! tensor_converter ! mux.sink_0 "
          "videotestsrc num-buffers=%d ! video/x-raw,width=160,height=120,format=RGB,framerate=(fraction)30/1 ! tensor_converter ! mux.sink_1 ",
          option.num_buffers, option.num_buffers);
      break;
    case TEST_TYPE_NEGO_FAILED:
      /** caps negotiation failed */
      str_pipeline =
          g_strdup_printf
          ("videotestsrc num-buffers=%d ! videoconvert ! video/x-raw,width=160,height=120,format=RGB,framerate=(fraction)30/1 ! "
          "videoconvert ! tensor_sink name=test_sink", option.num_buffers);
      break;
    case TEST_TYPE_VIDEO_RGB_AGGR:
      /** video stream with tensor_aggregator */
      str_pipeline =
          g_strdup_printf
          ("videotestsrc num-buffers=%d ! videoconvert ! video/x-raw,width=160,height=120,format=RGB,framerate=(fraction)30/1 ! "
          "tensor_converter ! tensor_aggregator frames-out=10 frames-flush=5 frames-dim=3 ! tensor_sink name=test_sink",
          option.num_buffers);
      break;
    case TEST_TYPE_AUDIO_S16_AGGR:
      /** audio stream with tensor_aggregator, 4 buffers with 2000 frames */
      str_pipeline =
          g_strdup_printf
          ("audiotestsrc num-buffers=%d samplesperbuffer=500 ! audioconvert ! audio/x-raw,format=S16LE,rate=16000,channels=1 ! "
          "tensor_converter frames-per-tensor=500 ! tensor_aggregator frames-in=500 frames-out=2000 frames-dim=1 ! tensor_sink name=test_sink",
          option.num_buffers);
      break;
    case TEST_TYPE_AUDIO_U16_AGGR:
      /** audio stream with tensor_aggregator, divided into 5 buffers with 100 frames */
      str_pipeline =
          g_strdup_printf
          ("audiotestsrc num-buffers=%d samplesperbuffer=500 ! audioconvert ! audio/x-raw,format=U16LE,rate=16000,channels=1 ! "
          "tensor_converter frames-per-tensor=500 ! tensor_aggregator frames-in=500 frames-out=100 frames-dim=1 ! tensor_sink name=test_sink",
          option.num_buffers);
      break;
    case TEST_TYPE_TYPECAST:
      /** text stream to test typecast */
      str_pipeline =
          g_strdup_printf
          ("appsrc name=appsrc caps=text/x-raw,format=utf8 ! "
          "tensor_converter ! tensor_transform mode=typecast option=%s ! tensor_sink name=test_sink",
          tensor_element_typename[option.t_type]);
      break;
    default:
      goto error;
  }

  g_test_data.pipeline = gst_parse_launch (str_pipeline, NULL);
  g_free (str_pipeline);
  _check_cond_err (g_test_data.pipeline != NULL);

  g_test_data.bus = gst_element_get_bus (g_test_data.pipeline);
  _check_cond_err (g_test_data.bus != NULL);

  gst_bus_add_signal_watch (g_test_data.bus);
  handle_id = g_signal_connect (g_test_data.bus, "message",
      (GCallback) _message_cb, NULL);
  _check_cond_err (handle_id > 0);

  g_test_data.sink =
      gst_bin_get_by_name (GST_BIN (g_test_data.pipeline), "test_sink");
  _check_cond_err (g_test_data.sink != NULL);

  if (DBG) {
    /** print logs */
    g_object_set (g_test_data.sink, "silent", (gboolean) FALSE, NULL);
  }

  /** signal for new data */
  handle_id = g_signal_connect (g_test_data.sink, "new-data",
      (GCallback) _new_data_cb, NULL);
  _check_cond_err (handle_id > 0);

  g_test_data.status = TEST_INIT;
  return TRUE;

error:
  _free_test_data ();
  return FALSE;
}

/**
 * @brief Test for tensor sink properties.
 */
TEST (tensor_sink_test, properties)
{
  guint rate, res_rate;
  gint64 lateness, res_lateness;
  gboolean silent, res_silent;
  gboolean emit, res_emit;
  gboolean sync, res_sync;
  gboolean qos, res_qos;
  TestOption option = { 1, TEST_TYPE_VIDEO_RGB };

  ASSERT_TRUE (_setup_pipeline (option));

  /** default signal-rate is 0 */
  g_object_get (g_test_data.sink, "signal-rate", &rate, NULL);
  EXPECT_EQ (rate, 0);

  rate += 10;
  g_object_set (g_test_data.sink, "signal-rate", rate, NULL);
  g_object_get (g_test_data.sink, "signal-rate", &res_rate, NULL);
  EXPECT_EQ (res_rate, rate);

  /** default emit-signal is TRUE */
  g_object_get (g_test_data.sink, "emit-signal", &emit, NULL);
  EXPECT_EQ (emit, TRUE);

  g_object_set (g_test_data.sink, "emit-signal", !emit, NULL);
  g_object_get (g_test_data.sink, "emit-signal", &res_emit, NULL);
  EXPECT_EQ (res_emit, !emit);

  /** default silent is TRUE */
  g_object_get (g_test_data.sink, "silent", &silent, NULL);
  EXPECT_EQ (silent, (DBG) ? FALSE : TRUE);

  g_object_set (g_test_data.sink, "silent", !silent, NULL);
  g_object_get (g_test_data.sink, "silent", &res_silent, NULL);
  EXPECT_EQ (res_silent, !silent);

  /** GstBaseSink:sync TRUE */
  g_object_get (g_test_data.sink, "sync", &sync, NULL);
  EXPECT_EQ (sync, TRUE);

  g_object_set (g_test_data.sink, "sync", !sync, NULL);
  g_object_get (g_test_data.sink, "sync", &res_sync, NULL);
  EXPECT_EQ (res_sync, !sync);

  /** GstBaseSink:max-lateness -1 (unlimited time) */
  g_object_get (g_test_data.sink, "max-lateness", &lateness, NULL);
  EXPECT_EQ (lateness, -1);

  lateness = 30 * GST_MSECOND;
  g_object_set (g_test_data.sink, "max-lateness", lateness, NULL);
  g_object_get (g_test_data.sink, "max-lateness", &res_lateness, NULL);
  EXPECT_EQ (res_lateness, lateness);

  /** GstBaseSink:qos TRUE */
  g_object_get (g_test_data.sink, "qos", &qos, NULL);
  EXPECT_EQ (qos, TRUE);

  g_object_set (g_test_data.sink, "qos", !qos, NULL);
  g_object_get (g_test_data.sink, "qos", &res_qos, NULL);
  EXPECT_EQ (res_qos, !qos);

  _free_test_data ();
}

/**
 * @brief Test for tensor sink signals.
 */
TEST (tensor_sink_test, signals)
{
  const guint num_buffers = 5;
  gulong handle_id;
  TestOption option = { num_buffers, TEST_TYPE_VIDEO_RGB };

  ASSERT_TRUE (_setup_pipeline (option));

  /** tensor sink signals */
  handle_id = g_signal_connect (g_test_data.sink, "stream-start",
      (GCallback) _stream_start_cb, NULL);
  EXPECT_TRUE (handle_id > 0);

  handle_id = g_signal_connect (g_test_data.sink, "eos",
      (GCallback) _eos_cb, NULL);
  EXPECT_TRUE (handle_id > 0);

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers and signals */
  EXPECT_EQ (g_test_data.received, num_buffers);
  EXPECT_EQ (g_test_data.start, TRUE);
  EXPECT_EQ (g_test_data.end, TRUE);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  _free_test_data ();
}

/**
 * @brief Test for tensor sink signal-rate.
 */
TEST (tensor_sink_test, signal_rate)
{
  const guint num_buffers = 6;
  TestOption option = { num_buffers, TEST_TYPE_VIDEO_RGB };

  ASSERT_TRUE (_setup_pipeline (option));

  /** set signal-rate */
  g_object_set (g_test_data.sink, "signal-rate", (guint) 15, NULL);

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers */
  EXPECT_TRUE (g_test_data.received < num_buffers);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  _free_test_data ();
}

/**
 * @brief Test for caps negotiation failed.
 */
TEST (tensor_sink_test, caps_error)
{
  const guint num_buffers = 5;
  TestOption option = { num_buffers, TEST_TYPE_NEGO_FAILED };

  /** failed : cannot link videoconvert and tensor_sink */
  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check error message */
  EXPECT_EQ (g_test_data.status, TEST_ERR_MESSAGE);

  /** check received buffers */
  EXPECT_EQ (g_test_data.received, 0);

  _free_test_data ();
}

/**
 * @brief Test for other/tensors caps negotiation.
 */
TEST (tensor_sink_test, caps_tensors)
{
  const guint num_buffers = 5;
  TestOption option = { num_buffers, TEST_TYPE_TENSORS };
  guint i;

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers */
  EXPECT_EQ (g_test_data.received, num_buffers);
  EXPECT_EQ (g_test_data.received_size, 115200);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensors"));

  /** check tensors config for video */
  EXPECT_TRUE (gst_tensors_config_validate (&g_test_data.tensors_config));
  EXPECT_EQ (g_test_data.tensors_config.info.num_tensors, 2);

  for (i = 0; i < g_test_data.tensors_config.info.num_tensors; i++) {
    EXPECT_EQ (g_test_data.tensors_config.info.info[i].type, _NNS_UINT8);
    EXPECT_EQ (g_test_data.tensors_config.info.info[i].dimension[0], 3);
    EXPECT_EQ (g_test_data.tensors_config.info.info[i].dimension[1], 160);
    EXPECT_EQ (g_test_data.tensors_config.info.info[i].dimension[2], 120);
    EXPECT_EQ (g_test_data.tensors_config.info.info[i].dimension[3], 1);
  }

  EXPECT_EQ (g_test_data.tensors_config.rate_n, 30);
  EXPECT_EQ (g_test_data.tensors_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for video format RGB.
 */
TEST (tensor_stream_test, video_rgb)
{
  const guint num_buffers = 5;
  TestOption option = { num_buffers, TEST_TYPE_VIDEO_RGB };

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers and signals */
  EXPECT_EQ (g_test_data.received, num_buffers);
  EXPECT_EQ (g_test_data.received_size, 57600);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for video */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, _NNS_UINT8);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0], 3);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 160);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 120);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 30);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for video format RGB, remove padding.
 */
TEST (tensor_stream_test, video_rgb_padding)
{
  const guint num_buffers = 5;
  TestOption option = { num_buffers, TEST_TYPE_VIDEO_RGB_PADDING };

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers and signals */
  EXPECT_EQ (g_test_data.received, num_buffers);
  EXPECT_EQ (g_test_data.received_size, 58320);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for video */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, _NNS_UINT8);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0], 3);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 162);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 120);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 30);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for video format RGB, 3 frames from tensor_converter.
 */
TEST (tensor_stream_test, video_rgb_3f)
{
  const guint num_buffers = 7;
  TestOption option = { num_buffers, TEST_TYPE_VIDEO_RGB_3F };

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers and signals */
  EXPECT_EQ (g_test_data.received, num_buffers / 3);
  EXPECT_EQ (g_test_data.received_size, 57600 * 3);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for video */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, _NNS_UINT8);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0], 3);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 160);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 120);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 3);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 30);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for video format BGRx.
 */
TEST (tensor_stream_test, video_bgrx)
{
  const guint num_buffers = 5;
  TestOption option = { num_buffers, TEST_TYPE_VIDEO_BGRx };

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers and signals */
  EXPECT_EQ (g_test_data.received, num_buffers);
  EXPECT_EQ (g_test_data.received_size, 76800);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for video */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, _NNS_UINT8);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0], 4);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 160);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 120);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 30);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for video format BGRx, 2 frames from tensor_converter.
 */
TEST (tensor_stream_test, video_bgrx_2f)
{
  const guint num_buffers = 6;
  TestOption option = { num_buffers, TEST_TYPE_VIDEO_BGRx_2F };

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers and signals */
  EXPECT_EQ (g_test_data.received, num_buffers / 2);
  EXPECT_EQ (g_test_data.received_size, 76800 * 2);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for video */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, _NNS_UINT8);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0], 4);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 160);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 120);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 2);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 30);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for video format GRAY8.
 */
TEST (tensor_stream_test, video_gray8)
{
  const guint num_buffers = 5;
  TestOption option = { num_buffers, TEST_TYPE_VIDEO_GRAY8 };

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers and signals */
  EXPECT_EQ (g_test_data.received, num_buffers);
  EXPECT_EQ (g_test_data.received_size, 19200);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for video */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, _NNS_UINT8);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 160);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 120);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 30);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for video format GRAY8, remove padding.
 */
TEST (tensor_stream_test, video_gray8_padding)
{
  const guint num_buffers = 5;
  TestOption option = { num_buffers, TEST_TYPE_VIDEO_GRAY8_PADDING };

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers and signals */
  EXPECT_EQ (g_test_data.received, num_buffers);
  EXPECT_EQ (g_test_data.received_size, 19440);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for video */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, _NNS_UINT8);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 162);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 120);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 30);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for video format GRAY8, 3 frames from tensor_converter, remove padding.
 */
TEST (tensor_stream_test, video_gray8_3f_padding)
{
  const guint num_buffers = 6;
  TestOption option = { num_buffers, TEST_TYPE_VIDEO_GRAY8_3F_PADDING };

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers and signals */
  EXPECT_EQ (g_test_data.received, num_buffers / 3);
  EXPECT_EQ (g_test_data.received_size, 19440 * 3);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for video */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, _NNS_UINT8);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 162);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 120);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 3);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 30);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for audio format S8.
 */
TEST (tensor_stream_test, audio_s8)
{
  const guint num_buffers = 5; /** 5 * 500 frames */
  TestOption option = { num_buffers, TEST_TYPE_AUDIO_S8 };

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers */
  EXPECT_EQ (g_test_data.received, num_buffers);
  EXPECT_EQ (g_test_data.received_size, 500);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for audio */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, _NNS_INT8);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 500);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 16000);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for audio format U8, 100 frames from tensor_converter.
 */
TEST (tensor_stream_test, audio_u8_100F)
{
  const guint num_buffers = 5; /** 5 * 500 frames */
  TestOption option = { num_buffers, TEST_TYPE_AUDIO_U8_100F };

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers */
  EXPECT_EQ (g_test_data.received, num_buffers * 5);
  EXPECT_EQ (g_test_data.received_size, 100);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for audio */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, _NNS_UINT8);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 100);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 16000);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for audio format S16.
 */
TEST (tensor_stream_test, audio_s16)
{
  const guint num_buffers = 5; /** 5 * 500 frames */
  TestOption option = { num_buffers, TEST_TYPE_AUDIO_S16 };

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers */
  EXPECT_EQ (g_test_data.received, num_buffers);
  EXPECT_EQ (g_test_data.received_size, 500 * 2);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for audio */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, _NNS_INT16);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 500);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 16000);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for audio format U16, 1000 frames from tensor_converter.
 */
TEST (tensor_stream_test, audio_u16_1000f)
{
  const guint num_buffers = 5; /** 5 * 500 frames */
  TestOption option = { num_buffers, TEST_TYPE_AUDIO_U16_1000F };

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers */
  EXPECT_EQ (g_test_data.received, num_buffers / 2);
  EXPECT_EQ (g_test_data.received_size, 500 * 2 * 2);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for audio */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, _NNS_UINT16);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 1000);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 16000);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for text format utf8.
 */
TEST (tensor_stream_test, text_utf8)
{
  const guint num_buffers = 10;
  TestOption option = { num_buffers, TEST_TYPE_TEXT };

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);

  _push_text_data (num_buffers);

  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers */
  EXPECT_EQ (g_test_data.received, num_buffers);
  EXPECT_EQ (g_test_data.received_size, GST_TENSOR_STRING_SIZE);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for text */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, _NNS_INT8);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0],
      GST_TENSOR_STRING_SIZE);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 0);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for text format utf8, 3 frames from tensor_converter.
 */
TEST (tensor_stream_test, text_utf8_3f)
{
  const guint num_buffers = 10;
  TestOption option = { num_buffers, TEST_TYPE_TEXT_3F };

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);

  _push_text_data (num_buffers);

  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers */
  EXPECT_EQ (g_test_data.received, num_buffers / 3);
  EXPECT_EQ (g_test_data.received_size, GST_TENSOR_STRING_SIZE * 3);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for text */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, _NNS_INT8);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0],
      GST_TENSOR_STRING_SIZE);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 3);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 0);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for typecast to int32 using tensor_transform.
 */
TEST (tensor_stream_test, typecast_int32)
{
  const guint num_buffers = 2;
  const tensor_type t_type = _NNS_INT32;
  TestOption option = { num_buffers, TEST_TYPE_TYPECAST, t_type };
  unsigned int t_size = tensor_element_size[t_type];

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);

  _push_text_data (num_buffers);

  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers */
  EXPECT_EQ (g_test_data.received, num_buffers);
  EXPECT_EQ (g_test_data.received_size, GST_TENSOR_STRING_SIZE * t_size);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for text */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, t_type);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0],
      GST_TENSOR_STRING_SIZE);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 0);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for typecast to uint32 using tensor_transform.
 */
TEST (tensor_stream_test, typecast_uint32)
{
  const guint num_buffers = 2;
  const tensor_type t_type = _NNS_UINT32;
  TestOption option = { num_buffers, TEST_TYPE_TYPECAST, t_type };
  unsigned int t_size = tensor_element_size[t_type];

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);

  _push_text_data (num_buffers);

  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers */
  EXPECT_EQ (g_test_data.received, num_buffers);
  EXPECT_EQ (g_test_data.received_size, GST_TENSOR_STRING_SIZE * t_size);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for text */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, t_type);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0],
      GST_TENSOR_STRING_SIZE);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 0);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for typecast to int16 using tensor_transform.
 */
TEST (tensor_stream_test, typecast_int16)
{
  const guint num_buffers = 2;
  const tensor_type t_type = _NNS_INT16;
  TestOption option = { num_buffers, TEST_TYPE_TYPECAST, t_type };
  unsigned int t_size = tensor_element_size[t_type];

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);

  _push_text_data (num_buffers);

  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers */
  EXPECT_EQ (g_test_data.received, num_buffers);
  EXPECT_EQ (g_test_data.received_size, GST_TENSOR_STRING_SIZE * t_size);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for text */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, t_type);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0],
      GST_TENSOR_STRING_SIZE);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 0);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for typecast to uint16 using tensor_transform.
 */
TEST (tensor_stream_test, typecast_uint16)
{
  const guint num_buffers = 2;
  const tensor_type t_type = _NNS_UINT16;
  TestOption option = { num_buffers, TEST_TYPE_TYPECAST, t_type };
  unsigned int t_size = tensor_element_size[t_type];

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);

  _push_text_data (num_buffers);

  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers */
  EXPECT_EQ (g_test_data.received, num_buffers);
  EXPECT_EQ (g_test_data.received_size, GST_TENSOR_STRING_SIZE * t_size);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for text */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, t_type);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0],
      GST_TENSOR_STRING_SIZE);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 0);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for typecast to float64 using tensor_transform.
 */
TEST (tensor_stream_test, typecast_float64)
{
  const guint num_buffers = 2;
  const tensor_type t_type = _NNS_FLOAT64;
  TestOption option = { num_buffers, TEST_TYPE_TYPECAST, t_type };
  unsigned int t_size = tensor_element_size[t_type];

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);

  _push_text_data (num_buffers);

  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers */
  EXPECT_EQ (g_test_data.received, num_buffers);
  EXPECT_EQ (g_test_data.received_size, GST_TENSOR_STRING_SIZE * t_size);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for text */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, t_type);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0],
      GST_TENSOR_STRING_SIZE);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 0);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for typecast to float32 using tensor_transform.
 */
TEST (tensor_stream_test, typecast_float32)
{
  const guint num_buffers = 2;
  const tensor_type t_type = _NNS_FLOAT32;
  TestOption option = { num_buffers, TEST_TYPE_TYPECAST, t_type };
  unsigned int t_size = tensor_element_size[t_type];

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);

  _push_text_data (num_buffers);

  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers */
  EXPECT_EQ (g_test_data.received, num_buffers);
  EXPECT_EQ (g_test_data.received_size, GST_TENSOR_STRING_SIZE * t_size);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for text */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, t_type);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0],
      GST_TENSOR_STRING_SIZE);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 0);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for typecast to int64 using tensor_transform.
 */
TEST (tensor_stream_test, typecast_int64)
{
  const guint num_buffers = 2;
  const tensor_type t_type = _NNS_INT64;
  TestOption option = { num_buffers, TEST_TYPE_TYPECAST, t_type };
  unsigned int t_size = tensor_element_size[t_type];

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);

  _push_text_data (num_buffers);

  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers */
  EXPECT_EQ (g_test_data.received, num_buffers);
  EXPECT_EQ (g_test_data.received_size, GST_TENSOR_STRING_SIZE * t_size);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for text */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, t_type);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0],
      GST_TENSOR_STRING_SIZE);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 0);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for typecast to uint64 using tensor_transform.
 */
TEST (tensor_stream_test, typecast_uint64)
{
  const guint num_buffers = 2;
  const tensor_type t_type = _NNS_UINT64;
  TestOption option = { num_buffers, TEST_TYPE_TYPECAST, t_type };
  unsigned int t_size = tensor_element_size[t_type];

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);

  _push_text_data (num_buffers);

  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers */
  EXPECT_EQ (g_test_data.received, num_buffers);
  EXPECT_EQ (g_test_data.received_size, GST_TENSOR_STRING_SIZE * t_size);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for text */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, t_type);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0],
      GST_TENSOR_STRING_SIZE);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 0);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for video stream with tensor_aggregator.
 */
TEST (tensor_stream_test, video_aggregate)
{
  const guint num_buffers = 35;
  TestOption option = { num_buffers, TEST_TYPE_VIDEO_RGB_AGGR };

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers */
  EXPECT_EQ (g_test_data.received, (num_buffers - 10) / 5 + 1);
  EXPECT_EQ (g_test_data.received_size, 57600 * 10);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for video */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, _NNS_UINT8);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0], 3);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 160);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 120);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 10);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 30);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for audio stream with tensor_aggregator.
 */
TEST (tensor_stream_test, audio_aggregate_s16)
{
  const guint num_buffers = 21;
  TestOption option = { num_buffers, TEST_TYPE_AUDIO_S16_AGGR };

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers */
  EXPECT_EQ (g_test_data.received, num_buffers / 4);
  EXPECT_EQ (g_test_data.received_size, 500 * 2 * 4);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for audio */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, _NNS_INT16);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 2000);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 16000);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Test for audio stream with tensor_aggregator.
 */
TEST (tensor_stream_test, audio_aggregate_u16)
{
  const guint num_buffers = 10;
  TestOption option = { num_buffers, TEST_TYPE_AUDIO_U16_AGGR };

  ASSERT_TRUE (_setup_pipeline (option));

  gst_element_set_state (g_test_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (g_test_data.loop);
  gst_element_set_state (g_test_data.pipeline, GST_STATE_NULL);

  /** check eos message */
  EXPECT_EQ (g_test_data.status, TEST_EOS);

  /** check received buffers */
  EXPECT_EQ (g_test_data.received, num_buffers * 5);
  EXPECT_EQ (g_test_data.received_size, 500 * 2 / 5);

  /** check caps name */
  EXPECT_TRUE (g_str_equal (g_test_data.caps_name, "other/tensor"));

  /** check tensor config for audio */
  EXPECT_TRUE (gst_tensor_config_validate (&g_test_data.tensor_config));
  EXPECT_EQ (g_test_data.tensor_config.info.type, _NNS_UINT16);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[0], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[1], 100);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[2], 1);
  EXPECT_EQ (g_test_data.tensor_config.info.dimension[3], 1);
  EXPECT_EQ (g_test_data.tensor_config.rate_n, 16000);
  EXPECT_EQ (g_test_data.tensor_config.rate_d, 1);

  _free_test_data ();
}

/**
 * @brief Main function for unit test.
 */
int
main (int argc, char **argv)
{
  testing::InitGoogleTest (&argc, argv);

  gst_init (&argc, &argv);

  return RUN_ALL_TESTS ();
}
