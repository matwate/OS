#include "mnist_params.h"
#include "test_images.h"
#include <math.h>
#include <stdio.h>

#define INPUT 784
#define HID1 512
#define HID2 512
#define HID3 256
#define OUT 10

static void linear(const float *in, const float *w, const float *b, float *out,
                   int in_dim, int out_dim) {
  for (int j = 0; j < out_dim; j++) {
    float s = b[j];
    for (int i = 0; i < in_dim; i++)
      s += in[i] * w[i * out_dim + j];
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

int classify(const float image[INPUT]) {
  float h1[HID1], h2[HID2], h3[HID3], out[OUT];

  linear(image, layer1_kernel, layer1_bias, h1, INPUT, HID1);
  relu(h1, HID1);
  linear(h1, layer2_kernel, layer2_bias, h2, HID1, HID2);
  relu(h2, HID2);
  linear(h2, layer3_kernel, layer3_bias, h3, HID2, HID3);
  relu(h3, HID3);
  linear(h3, layer4_kernel, layer4_bias, out, HID3, OUT);

  return argmax(out, OUT);
}

int main(void) {
  int correct = 0;

  printf("Testing MNIST classifier on %d samples\n", NUM_TEST);
  printf("----------------------------------------\n");

  for (int i = 0; i < NUM_TEST; i++) {
    int pred = classify(test_images[i]);
    int label = test_labels[i];
    printf("Sample %d: label=%d  predicted=%d  %s\n", i, label, pred,
           pred == label ? "OK" : "WRONG");
    if (pred == label)
      correct++;
  }

  printf("----------------------------------------\n");
  printf("Accuracy: %d/%d (%.0f%%)\n", correct, NUM_TEST,
         100.0f * correct / NUM_TEST);

  return correct == NUM_TEST ? 0 : 1;
}
