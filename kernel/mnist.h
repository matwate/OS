#ifndef MNIST_H
#define MNIST_H

#include <stdint.h>

/* Architecture: 784 -> 512 -> 512 -> 256 -> 10 */
#define MNIST_INPUT  784
#define MNIST_HID1   512
#define MNIST_HID2   512
#define MNIST_HID3   256
#define MNIST_OUT    10

/* Binary layout of weights.bin (all float32, per-layer: bias then weights):
 *   [512 floats]  b1: layer1 bias
 *   [784*512 fl]  w1: layer1 weights
 *   [512 floats]  b2: layer2 bias
 *   [512*512 fl]  w2: layer2 weights
 *   [256 floats]  b3: layer3 bias
 *   [512*256 fl]  w3: layer3 weights
 *   [10 floats]   b4: layer4 bias
 *   [256*10 fl]   w4: layer4 weights
 *
 * Total: 3,193,896 bytes = 6,239 sectors
 */

/* Size of the weights file in sectors (ceiling division) */
#define MNIST_WEIGHTS_SECTORS ((3193896 + 511) / 512)  /* = 6239 */

/* Byte offsets into the weights file */
#define OFF_LAYER1_BIAS     0
#define OFF_LAYER1_WEIGHTS  (OFF_LAYER1_BIAS    + 512 * 4)                /* 2,048 */
#define OFF_LAYER2_BIAS     (OFF_LAYER1_WEIGHTS + 784 * 512 * 4)          /* 1,607,680 */
#define OFF_LAYER2_WEIGHTS  (OFF_LAYER2_BIAS    + 512 * 4)                /* 1,609,728 */
#define OFF_LAYER3_BIAS     (OFF_LAYER2_WEIGHTS + 512 * 512 * 4)          /* 2,658,304 */
#define OFF_LAYER3_WEIGHTS  (OFF_LAYER3_BIAS    + 256 * 4)                /* 2,659,328 */
#define OFF_LAYER4_BIAS     (OFF_LAYER3_WEIGHTS + 512 * 256 * 4)          /* 3,183,616 */
#define OFF_LAYER4_WEIGHTS  (OFF_LAYER4_BIAS    + 10 * 4)                 /* 3,183,656 */

/* Static buffer to hold weights after loading from disk.
 * Declared in kernel.c as a pointer to the 2MB physical mark. */
extern uint8_t *weights_buf;

/* Layer pointers — point into weights_buf after loading.
 * Must be called once after ata_pio_read completes. */
void mnist_pointers(void);

/* Classify a 784-pixel image (normalized 0..255 -> 0..1) */
int mnist_classify(const float *image);

#endif /* MNIST_H */