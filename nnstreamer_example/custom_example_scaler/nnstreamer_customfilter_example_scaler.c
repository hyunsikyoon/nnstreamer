/**
 * NNStreamer Custom Filter Example 3. Scaler
 * Copyright (C) 2018 MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * LICENSE: LGPL-2.1
 *
 * @file  nnstreamer_customfilter_example_scaler.c
 * @date  22 Jun 2018
 * @brief  Custom NNStreamer Filter Example 3. "Scaler"
 * @author  MyungJoo Ham <myungjoo.ham@samsung.com>
 * @bug  No known bugs
 *
 * This scales a tensor of [N][y][x][M] to [N][new-y][new-x][M]
 *
 * The custom property is to be given as, "custom=[new-x]x[new-y]", where new-x and new-y are unsigned integers.
 * E.g., custom=640x480
 *
 * Output[y'][x'] = Input[ y' * y / new-y ][ x' * x / new-x ]. Yeah This is Way too Simple. But this is just an example :D
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <glib.h>
#include <tensor_filter_custom.h>

/**
 * @brief Custom filter's private data.
 */
typedef struct _pt_data
{
  uint32_t id; /***< Just for testing */
  char *property; /***< The string given as "custom" property of tensor_filter element */
  uint32_t new_y;
  uint32_t new_x;
} pt_data;

/**
 * @brief strdup() is not C89 compatible. Define it here.
 */
static char *
_strdup (const char *src)
{
  size_t len = strlen (src) + 1;
  char *dest = (char *) malloc (sizeof (char) * len);
  strncpy (dest, src, len - 1);
  dest[len - 1] = '\0';
  return dest;
}

/**
 * @brief tensor_filter_custom::NNS_custom_init_func
 */
static void *
pt_init (const GstTensorFilterProperties * prop)
{
  pt_data *data = (pt_data *) malloc (sizeof (pt_data));

  if (prop->custom_properties && strlen (prop->custom_properties) > 0)
    data->property = _strdup (prop->custom_properties);
  else
    data->property = NULL;
  data->new_x = 0;
  data->new_y = 0;

  /* Parse property and set new_x, new_y */
  if (data->property) {
    const char s[7] = "xX:_/ ";
    gchar **strv = g_strsplit_set (data->property, s, 3);
    if (strv[0] != NULL) {
      data->new_x = (uint32_t) g_ascii_strtoll (strv[0], NULL, 10);
      if (data->new_x < 0)
        data->new_x = 0;
    } else {
      data->new_x = 0;
    }
    if (strv[1] != NULL) {
      data->new_y = (uint32_t) g_ascii_strtoll (strv[1], NULL, 10);
      if (data->new_y < 0)
        data->new_y = 0;
    } else {
      data->new_y = 0;
    }
    g_strfreev (strv);
  }

  data->id = 0;
  return data;
}

/**
 * @brief tensor_filter_custom::NNS_custom_exit_func
 */
static void
pt_exit (void *private_data, const GstTensorFilterProperties * prop)
{
  pt_data *data = private_data;
  assert (data);
  if (data->property)
    free (data->property);
  free (data);
}

/**
 * @brief tensor_filter_custom::NNS_custom_set_input_dimension
 */
static int
set_inputDim (void *private_data, const GstTensorFilterProperties * prop,
    const GstTensorsInfo * in_info, GstTensorsInfo * out_info)
{
  int i;
  pt_data *data = private_data;

  assert (data);
  assert (in_info);
  assert (out_info);

  out_info->num_tensors = 1;

  for (i = 0; i < NNS_TENSOR_RANK_LIMIT; i++)
    out_info->info[0].dimension[i] = in_info->info[0].dimension[i];

  /* Update [1] and [2] oDim with new-x, new-y */
  if (data->new_x > 0)
    out_info->info[0].dimension[1] = data->new_x;
  if (data->new_y > 0)
    out_info->info[0].dimension[2] = data->new_y;

  out_info->info[0].type = in_info->info[0].type;
  return 0;
}

/**
 * @brief tensor_filter_custom::NNS_custom_invoke
 */
static int
pt_invoke (void *private_data, const GstTensorFilterProperties * prop,
    const uint8_t * inptr, uint8_t * outptr)
{
  pt_data *data = private_data;
  uint32_t ox, oy, x, y, z, elementsize;
  uint32_t oidx0, oidx1, oidx2;
  uint32_t iidx0, iidx1, iidx2;

  assert (data);
  assert (inptr);
  assert (outptr);

  /* This assumes the limit is 4 */
  assert (NNS_TENSOR_RANK_LIMIT == 4);

  assert (prop->input_meta.info[0].dimension[0] ==
      prop->output_meta.info[0].dimension[0]);
  assert (prop->input_meta.info[0].dimension[3] ==
      prop->output_meta.info[0].dimension[3]);
  assert (prop->input_meta.info[0].type == prop->output_meta.info[0].type);

  elementsize = tensor_element_size[prop->input_meta.info[0].type];

  ox = (data->new_x > 0) ? data->new_x : prop->output_meta.info[0].dimension[1];
  oy = (data->new_y > 0) ? data->new_y : prop->output_meta.info[0].dimension[2];

  oidx0 = prop->output_meta.info[0].dimension[0];
  oidx1 = oidx0 * prop->output_meta.info[0].dimension[1];
  oidx2 = oidx1 * prop->output_meta.info[0].dimension[2];

  iidx0 = prop->input_meta.info[0].dimension[0];
  iidx1 = iidx0 * prop->input_meta.info[0].dimension[1];
  iidx2 = iidx1 * prop->input_meta.info[0].dimension[2];

  for (z = 0; z < prop->input_meta.info[0].dimension[3]; z++) {
    for (y = 0; y < oy; y++) {
      for (x = 0; x < ox; x++) {
        unsigned int c;
        for (c = 0; c < prop->input_meta.info[0].dimension[0]; c++) {
          int sz;
          /* Output[y'][x'] = Input[ y' * y / new-y ][ x' * x / new-x ]. Yeah This is Way too Simple. But this is just an example :D */
          unsigned ix, iy;

          ix = x * prop->input_meta.info[0].dimension[1] / ox;
          iy = y * prop->input_meta.info[0].dimension[2] / oy;

          assert (ix >= 0 && iy >= 0
              && ix < prop->input_meta.info[0].dimension[1]
              && iy < prop->input_meta.info[0].dimension[2]);

          /* outptr[z][y][x][c] = inptr[z][iy][ix][c]; */
          for (sz = 0; sz < elementsize; sz++)
            *(outptr + elementsize * (c + x * oidx0 + y * oidx1 + z * oidx2) +
                sz) =
                *(inptr + elementsize * (c + ix * iidx0 + iy * iidx1 +
                    z * iidx2) + sz);
        }
      }
    }
  }

  assert (inptr != outptr);

  return 0;
}

static NNStreamer_custom_class NNStreamer_custom_body = {
  .initfunc = pt_init,
  .exitfunc = pt_exit,
  .setInputDim = set_inputDim,
  .invoke = pt_invoke,
};

/* The dyn-loaded object */
NNStreamer_custom_class *NNStreamer_custom = &NNStreamer_custom_body;
