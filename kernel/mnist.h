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
 *   [4 floats]           scales: layer1, layer2, layer3, layer4
 *   [797184 int8]        weights: layer1, layer2, layer3, layer4 (flat)
 *   [1290 floats]        biases:  layer1(512), layer2(512), layer3(256), layer4(10)
 *
 * Total: 802,360 bytes = 1,568 sectors
 */

/* Size of the weights file in sectors (ceiling division) */
#define MNIST_WEIGHTS_SECTORS ((802360 + 511) / 512)  /* = 1569 */

/* Byte offsets into the weights file */
#define OFF_SCALES          0
#define OFF_LAYER1_WEIGHTS  16
#define OFF_LAYER2_WEIGHTS  (OFF_LAYER1_WEIGHTS + 784 * 512)   /* 401,424 */
#define OFF_LAYER3_WEIGHTS  (OFF_LAYER2_WEIGHTS + 512 * 512)   /* 663,568 */
#define OFF_LAYER4_WEIGHTS  (OFF_LAYER3_WEIGHTS + 512 * 256)   /* 794,640 */
#define OFF_LAYER1_BIAS     (OFF_LAYER4_WEIGHTS + 256 * 10)    /* 797,200 */
#define OFF_LAYER2_BIAS     (OFF_LAYER1_BIAS + 512 * 4)        /* 799,248 */
#define OFF_LAYER3_BIAS     (OFF_LAYER2_BIAS + 512 * 4)        /* 801,296 */
#define OFF_LAYER4_BIAS     (OFF_LAYER3_BIAS + 256 * 4)        /* 802,320 */

/* Static buffer to hold weights after loading from disk.
 * Declared in mnist.c, referenced by mnist.c and kernel.c */
extern uint8_t weights_buf[802360];

/* Layer pointers — point into weights_buf after loading.
 * Must be called once after ata_pio_read completes. */
void mnist_pointers(void);

/* Classify a 784-pixel image (normalized 0..255 -> 0..1) */
int mnist_classify(const float *image);

#endif /* MNIST_H */