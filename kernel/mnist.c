#include "mnist.h"

#define Q_SHIFT 14
#define Q_ONE   (1 << Q_SHIFT)

/* Weights are loaded by kernel.c after calling ata_pio_read(). */
extern uint8_t weights_buf[MNIST_WEIGHTS_BUFSIZE];

static const int8_t *w1 = 0, *w2 = 0, *w3 = 0, *w4 = 0;
static const uint8_t *b1_raw = 0, *b2_raw = 0, *b3_raw = 0, *b4_raw = 0;

static int32_t scale_q[4];
static int32_t b1_q[MNIST_HID1];
static int32_t b2_q[MNIST_HID2];
static int32_t b3_q[MNIST_HID3];
static int32_t b4_q[MNIST_OUT];

static uint32_t read_u32_le(const uint8_t *p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int32_t float_bits_to_q(uint32_t bits) {
    uint32_t exp = (bits >> 23) & 0xff;
    uint32_t frac = bits & 0x7fffff;
    uint32_t mant;
    int shift;
    int32_t q;

    if (exp == 0)
        return 0;

    mant = (1u << 23) | frac;
    shift = (int)exp - 127 - 23 + Q_SHIFT;

    if (shift >= 0) {
        if (shift > 7)
            q = 0x7fffffff;
        else
            q = (int32_t)(mant << shift);
    } else {
        int rshift = -shift;
        if (rshift >= 31)
            q = 0;
        else
            q = (int32_t)((mant + (1u << (rshift - 1))) >> rshift);
    }

    return (bits & 0x80000000u) ? -q : q;
}

static void convert_biases(const uint8_t *src, int32_t *dst, int n) {
    for (int i = 0; i < n; i++)
        dst[i] = float_bits_to_q(read_u32_le(src + i * 4));
}

/* Parse the flat int8 binary buffer into structured pointers. */
void mnist_pointers(void) {
    const uint8_t *scales = weights_buf + OFF_WEIGHT_SCALES;

    scale_q[0] = float_bits_to_q(read_u32_le(scales + 0 * 4));
    scale_q[1] = float_bits_to_q(read_u32_le(scales + 1 * 4));
    scale_q[2] = float_bits_to_q(read_u32_le(scales + 2 * 4));
    scale_q[3] = float_bits_to_q(read_u32_le(scales + 3 * 4));

    w1 = (const int8_t *)(weights_buf + OFF_LAYER1_WEIGHTS);
    w2 = (const int8_t *)(weights_buf + OFF_LAYER2_WEIGHTS);
    w3 = (const int8_t *)(weights_buf + OFF_LAYER3_WEIGHTS);
    w4 = (const int8_t *)(weights_buf + OFF_LAYER4_WEIGHTS);

    b1_raw = weights_buf + OFF_LAYER1_BIAS;
    b2_raw = weights_buf + OFF_LAYER2_BIAS;
    b3_raw = weights_buf + OFF_LAYER3_BIAS;
    b4_raw = weights_buf + OFF_LAYER4_BIAS;

    convert_biases(b1_raw, b1_q, MNIST_HID1);
    convert_biases(b2_raw, b2_q, MNIST_HID2);
    convert_biases(b3_raw, b3_q, MNIST_HID3);
    convert_biases(b4_raw, b4_q, MNIST_OUT);
}

static void linear_input_int8(const uint8_t *in, const int8_t *w,
                              int32_t weight_scale, const int32_t *b,
                              int32_t *out, int in_dim, int out_dim) {
    for (int j = 0; j < out_dim; j++) {
        int32_t s = b[j];
        for (int i = 0; i < in_dim; i++) {
            if (in[i])
                s += (int32_t)w[i * out_dim + j] * weight_scale;
        }
        out[j] = s > 0 ? s : 0;
    }
}

static void linear_q_int8(const int32_t *in, const int8_t *w,
                          int32_t weight_scale, const int32_t *b,
                          int32_t *out, int in_dim, int out_dim,
                          int apply_relu) {
    for (int j = 0; j < out_dim; j++) {
        int32_t s = b[j];
        for (int i = 0; i < in_dim; i++) {
            int32_t weight_q = (int32_t)w[i * out_dim + j] * weight_scale;
            int64_t product = (int64_t)in[i] * (int64_t)weight_q;
            s += (int32_t)(product >> Q_SHIFT);
        }
        out[j] = (apply_relu && s < 0) ? 0 : s;
    }
}

static int argmax_q(const int32_t *x, int n) {
    int best = 0;
    for (int i = 1; i < n; i++)
        if (x[i] > x[best])
            best = i;
    return best;
}

int mnist_classify(const uint8_t *image) {
    int32_t h1[MNIST_HID1];
    int32_t h2[MNIST_HID2];
    int32_t h3[MNIST_HID3];
    int32_t out[MNIST_OUT];

    (void)Q_ONE;
    linear_input_int8(image, w1, scale_q[0], b1_q, h1, MNIST_INPUT, MNIST_HID1);
    linear_q_int8(h1, w2, scale_q[1], b2_q, h2, MNIST_HID1, MNIST_HID2, 1);
    linear_q_int8(h2, w3, scale_q[2], b3_q, h3, MNIST_HID2, MNIST_HID3, 1);
    linear_q_int8(h3, w4, scale_q[3], b4_q, out, MNIST_HID3, MNIST_OUT, 0);

    return argmax_q(out, MNIST_OUT);
}
