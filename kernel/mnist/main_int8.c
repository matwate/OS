/* Auto-generated int8 quantized MNIST inference */
#include "mnist_params_int8.h"
#include "test_images.h"
#include <stdint.h>

#define INPUT 784
#define HID1 512
#define HID2 512
#define HID3 256
#define OUT 10

/* Linear layer with int8 weights, float biases */
static void linear_int8(const float *in, const int8_t *w, float w_scale,
                        const float *b, float *out, int in_dim, int out_dim) {
    for (int j = 0; j < out_dim; j++) {
        float s = b[j];  /* Bias is float */
        for (int i = 0; i < in_dim; i++) {
            /* Dequantize weight on the fly */
            float w_float = w[i * out_dim + j] * w_scale;
            s += in[i] * w_float;
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

int classify_int8(const float image[INPUT]) {
    float h1[HID1], h2[HID2], h3[HID3], out[OUT];

    linear_int8(image, layer1_kernel_int8, layer1_weight_scale,
                layer1_bias, h1, INPUT, HID1);
    relu(h1, HID1);
    
    linear_int8(h1, layer2_kernel_int8, layer2_weight_scale,
                layer2_bias, h2, HID1, HID2);
    relu(h2, HID2);
    
    linear_int8(h2, layer3_kernel_int8, layer3_weight_scale,
                layer3_bias, h3, HID2, HID3);
    relu(h3, HID3);
    
    linear_int8(h3, layer4_kernel_int8, layer4_weight_scale,
                layer4_bias, out, HID3, OUT);

    return argmax(out, OUT);
}


