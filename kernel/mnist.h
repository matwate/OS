#ifndef MNIST_H
#define MNIST_H

#include <stdint.h>

/* Architecture: 784 -> 512 -> 512 -> 256 -> 10 */
#define MNIST_INPUT  784
#define MNIST_HID1   512
#define MNIST_HID2   512
#define MNIST_HID3   256
#define MNIST_OUT    10

/* Binary layout of weights_int8.bin:
 *   [4 float32]      per-layer int8 weight scales
 *   [784*512 int8]   w1: layer1 weights
 *   [512*512 int8]   w2: layer2 weights
 *   [512*256 int8]   w3: layer3 weights
 *   [256*10 int8]    w4: layer4 weights
 *   [512 float32]    b1: layer1 bias
 *   [512 float32]    b2: layer2 bias
 *   [256 float32]    b3: layer3 bias
 *   [10 float32]     b4: layer4 bias
 *
 * Total: 802,360 bytes = 1,568 sectors after padding.
 * Float32 values are converted to fixed point with integer bit parsing so the
 * kernel does not emit or depend on x87 floating-point instructions.
 */

/* Size of the weights file in sectors (ceiling division) */
#define MNIST_WEIGHTS_BYTES   802360
#define MNIST_WEIGHTS_SECTORS ((MNIST_WEIGHTS_BYTES + 511) / 512)  /* = 1568 */
#define MNIST_WEIGHTS_BUFSIZE (MNIST_WEIGHTS_SECTORS * 512)

/* Byte offsets into the weights file */
#define OFF_WEIGHT_SCALES    0
#define OFF_LAYER1_WEIGHTS   (OFF_WEIGHT_SCALES  + 4 * 4)                /* 16 */
#define OFF_LAYER2_WEIGHTS   (OFF_LAYER1_WEIGHTS + 784 * 512)            /* 401,424 */
#define OFF_LAYER3_WEIGHTS   (OFF_LAYER2_WEIGHTS + 512 * 512)            /* 663,568 */
#define OFF_LAYER4_WEIGHTS   (OFF_LAYER3_WEIGHTS + 512 * 256)            /* 794,640 */
#define OFF_LAYER1_BIAS      (OFF_LAYER4_WEIGHTS + 256 * 10)             /* 797,200 */
#define OFF_LAYER2_BIAS      (OFF_LAYER1_BIAS    + 512 * 4)              /* 799,248 */
#define OFF_LAYER3_BIAS      (OFF_LAYER2_BIAS    + 512 * 4)              /* 801,296 */
#define OFF_LAYER4_BIAS      (OFF_LAYER3_BIAS    + 256 * 4)              /* 802,320 */

/* Static buffer to hold weights after loading from disk.
 * Declared in kernel.c, referenced by mnist.c and kernel.c. */
extern uint8_t weights_buf[MNIST_WEIGHTS_BUFSIZE];

/* Parse int8 weights and convert float32 scales/biases to fixed point.
 * Must be called once after ata_pio_read completes. */
void mnist_pointers(void);

/* Classify a 784-pixel binary image using fixed-point int8 inference. */
int mnist_classify(const uint8_t *image);

#endif /* MNIST_H */
