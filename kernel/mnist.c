#include "mnist.h"

/* Weights are loaded here by kernel.c after calling ata_pio_read().
 * The array is defined in kernel.c to avoid duplicate symbol errors. */
extern uint8_t weights_buf[802360];

/* Layer pointers — set by mnist_pointers() after disk load */
static const float *scales = 0;
static const int8_t *w1 = 0, *w2 = 0, *w3 = 0, *w4 = 0;
static const float *b1 = 0, *b2 = 0, *b3 = 0, *b4 = 0;

/* Parse the flat binary buffer into structured pointers */
void mnist_pointers(void) {
    scales = (const float *)(weights_buf + OFF_SCALES);
    w1 = (const int8_t *)(weights_buf + OFF_LAYER1_WEIGHTS);
    w2 = (const int8_t *)(weights_buf + OFF_LAYER2_WEIGHTS);
    w3 = (const int8_t *)(weights_buf + OFF_LAYER3_WEIGHTS);
    w4 = (const int8_t *)(weights_buf + OFF_LAYER4_WEIGHTS);
    b1 = (const float *)(weights_buf + OFF_LAYER1_BIAS);
    b2 = (const float *)(weights_buf + OFF_LAYER2_BIAS);
    b3 = (const float *)(weights_buf + OFF_LAYER3_BIAS);
    b4 = (const float *)(weights_buf + OFF_LAYER4_BIAS);
}

/* Linear layer: out[j] = sum_i(in[i] * w[i*out_dim+j]) + b[j]
 * w is stored flat as: [out0_w0, out1_w0, ..., outN_w0, out0_w1, ...] (row-major)
 * which is i*out_dim + j indexing. */
static void linear(const float *in, const int8_t *w, float w_scale,
                   const float *b, float *out, int in_dim, int out_dim) {
    for (int j = 0; j < out_dim; j++) {
        float s = b[j];
        for (int i = 0; i < in_dim; i++) {
            s += in[i] * (float)w[i * out_dim + j] * w_scale;
        }
        out[j] = s;
    }
}

static void relu(float *x, int n) {
    for (int i = 0; i < n; i++)
        if (x[i] < 0.0f)
            x[i] = 0.0f;
}

static int argmax(const float *x, int n) {
    int best = 0;
    for (int i = 1; i < n; i++)
        if (x[i] > x[best])
            best = i;
    return best;
}

int mnist_classify(const float *image) {
    float h1[MNIST_HID1];
    float h2[MNIST_HID2];
    float h3[MNIST_HID3];
    float out[MNIST_OUT];

    linear(image, w1, scales[0], b1, h1, MNIST_INPUT, MNIST_HID1);
    relu(h1, MNIST_HID1);

    linear(h1, w2, scales[1], b2, h2, MNIST_HID1, MNIST_HID2);
    relu(h2, MNIST_HID2);

    linear(h2, w3, scales[2], b3, h3, MNIST_HID2, MNIST_HID3);
    relu(h3, MNIST_HID3);

    linear(h3, w4, scales[3], b4, out, MNIST_HID3, MNIST_OUT);

    return argmax(out, MNIST_OUT);
}