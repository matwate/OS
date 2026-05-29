#include "mnist.h"

/* Weights are loaded here by kernel.c after calling ata_pio_read().
 * The pointer is defined in kernel.c and exported via mnist.h. */

/* Layer pointers — set by mnist_pointers() after disk load */
static const float *w1 = 0, *w2 = 0, *w3 = 0, *w4 = 0;
static const float *b1 = 0, *b2 = 0, *b3 = 0, *b4 = 0;

/* Parse the flat binary buffer into structured pointers */
void mnist_pointers(void) {
    b1 = (const float *)(weights_buf + OFF_LAYER1_BIAS);
    w1 = (const float *)(weights_buf + OFF_LAYER1_WEIGHTS);
    b2 = (const float *)(weights_buf + OFF_LAYER2_BIAS);
    w2 = (const float *)(weights_buf + OFF_LAYER2_WEIGHTS);
    b3 = (const float *)(weights_buf + OFF_LAYER3_BIAS);
    w3 = (const float *)(weights_buf + OFF_LAYER3_WEIGHTS);
    b4 = (const float *)(weights_buf + OFF_LAYER4_BIAS);
    w4 = (const float *)(weights_buf + OFF_LAYER4_WEIGHTS);
}

/* Linear layer: out[j] = sum_i(in[i] * w[i*out_dim+j]) + b[j]
 * w is stored flat as: [out0_w0, out1_w0, ..., outN_w0, out0_w1, ...] (row-major)
 * which is i*out_dim + j indexing. */
static void linear(const float *in, const float *w,
                   const float *b, float *out, int in_dim, int out_dim) {
    for (int j = 0; j < out_dim; j++) {
        float s = b[j];
        for (int i = 0; i < in_dim; i++) {
            s += in[i] * w[i * out_dim + j];
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

    linear(image, w1, b1, h1, MNIST_INPUT, MNIST_HID1);
    relu(h1, MNIST_HID1);

    linear(h1, w2, b2, h2, MNIST_HID1, MNIST_HID2);
    relu(h2, MNIST_HID2);

    linear(h2, w3, b3, h3, MNIST_HID2, MNIST_HID3);
    relu(h3, MNIST_HID3);

    linear(h3, w4, b4, out, MNIST_HID3, MNIST_OUT);

    return argmax(out, MNIST_OUT);
}