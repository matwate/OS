#!/usr/bin/env python3
"""
Quantize MNIST model weights from float32 to int8.
Reduces model size by ~4x (3.2MB -> ~0.8MB).
Pure Python - no numpy required.
"""

import struct
import re

# Model architecture
INPUT = 784
HID1 = 512
HID2 = 512
HID3 = 256
OUT = 10

# Layer dimensions (in_dim, out_dim) for weights
LAYERS = [
    (INPUT, HID1),   # layer1
    (HID1, HID2),    # layer2
    (HID2, HID3),    # layer3
    (HID3, OUT),     # layer4
]

BIAS_SIZES = [HID1, HID2, HID3, OUT]


def read_weights_bin(path):
    """Read float32 weights from binary file."""
    with open(path, 'rb') as f:
        data = f.read()
    # Unpack as float32 little-endian
    num_floats = len(data) // 4
    floats = struct.unpack('<' + 'f' * num_floats, data)
    return list(floats)


def max_abs(values):
    """Find max absolute value."""
    return max(abs(v) for v in values)


def quantize_tensor_symmetric(values):
    """
    Symmetric per-tensor quantization to int8.
    scale = max(abs(w)) / 127
    w_int8 = round(w / scale) clamped to [-127, 127]
    """
    max_val = max_abs(values)
    if max_val < 1e-8:
        scale = 1.0
        return [0] * len(values), scale
    
    scale = max_val / 127.0
    int8_values = []
    for v in values:
        q = int(round(v / scale))
        # Clamp to int8 range [-128, 127]
        q = max(-128, min(127, q))
        int8_values.append(q)
    
    return int8_values, scale


def linear_float(input_vec, weight_matrix, bias_vec):
    """Linear layer: out = input @ W + b."""
    in_dim = len(input_vec)
    out_dim = len(bias_vec)
    
    result = [0.0] * out_dim
    for i in range(in_dim):
        inp = input_vec[i]
        for j in range(out_dim):
            result[j] += inp * weight_matrix[i][j]
    
    for j in range(out_dim):
        result[j] += bias_vec[j]
    
    return result


def linear_int8(input_vec, weight_int8, weight_scale, bias_float):
    """
    Linear layer with int8 weights but float bias.
    Dequantize weights on the fly during computation.
    """
    in_dim = len(input_vec)
    out_dim = len(bias_float)
    
    result = [0.0] * out_dim
    for i in range(in_dim):
        inp = input_vec[i]
        for j in range(out_dim):
            # Dequantize weight on the fly
            w_float = weight_int8[i][j] * weight_scale
            result[j] += inp * w_float
    
    # Add bias (kept in float for accuracy)
    for j in range(out_dim):
        result[j] += bias_float[j]
    
    return result


def relu(vec):
    """ReLU activation."""
    return [max(0.0, v) for v in vec]


def argmax(vec):
    """Index of max value."""
    max_idx = 0
    max_val = vec[0]
    for i, v in enumerate(vec):
        if v > max_val:
            max_val = v
            max_idx = i
    return max_idx


def classify_float(image, weights_list, biases_list):
    """Classify using float weights."""
    x = image[:]
    for i, (w, b) in enumerate(zip(weights_list, biases_list)):
        x = linear_float(x, w, b)
        if i < len(weights_list) - 1:
            x = relu(x)
    return argmax(x)


def classify_int8(image, weights_int8_list, weight_scales, biases_list):
    """Classify using int8 weights with float biases."""
    x = image[:]
    for i, (w_int8, w_scale, b) in enumerate(
        zip(weights_int8_list, weight_scales, biases_list)):
        x = linear_int8(x, w_int8, w_scale, b)
        if i < len(weights_int8_list) - 1:
            x = relu(x)
    return argmax(x)


def main():
    print("=" * 60)
    print("MNIST Int8 Quantization")
    print("=" * 60)
    
    # Read raw float32 weights
    print("\n[1] Reading weights.bin...")
    all_weights = read_weights_bin('weights.bin')
    print(f"    Total params: {len(all_weights):,} floats")
    print(f"    Original size: {len(all_weights) * 4:,} bytes ({len(all_weights) * 4 / 1024 / 1024:.2f} MB)")
    
    # Parse into layers - binary order is: bias, kernel, bias, kernel, ...
    idx = 0
    weight_list = []
    bias_list = []
    
    for (in_dim, out_dim), bias_dim in zip(LAYERS, BIAS_SIZES):
        # Extract bias FIRST (matches binary layout)
        b = all_weights[idx:idx + bias_dim]
        bias_list.append(b)
        idx += bias_dim
        
        # Extract weights (stored as (in_dim, out_dim))
        w_size = in_dim * out_dim
        w_flat = all_weights[idx:idx + w_size]
        # Reshape to (in_dim, out_dim)
        w = [w_flat[i * out_dim:(i + 1) * out_dim] for i in range(in_dim)]
        weight_list.append(w)
        idx += w_size
        
        print(f"    Layer: {in_dim} -> {out_dim}, bias: {bias_dim}, weights: {w_size:,}")
    
    # Quantize weights only (keep biases as float for accuracy)
    print("\n[2] Quantizing weights to int8 (symmetric per-tensor)...")
    weights_int8 = []
    weight_scales = []
    
    for i, w in enumerate(weight_list):
        # Flatten weights for quantization
        w_flat = [w[row][col] for row in range(len(w)) for col in range(len(w[0]))]
        
        w_int8_flat, w_scale = quantize_tensor_symmetric(w_flat)
        # Reshape back
        in_dim, out_dim = LAYERS[i]
        w_int8 = [w_int8_flat[row * out_dim:(row + 1) * out_dim] for row in range(in_dim)]
        weights_int8.append(w_int8)
        weight_scales.append(w_scale)
        
        max_w = max_abs(w_flat)
        # Calculate quantization error
        w_dequant = [v * w_scale for v in w_int8_flat]
        mse = sum((a - b) ** 2 for a, b in zip(w_flat, w_dequant)) / len(w_flat)
        print(f"    Layer {i+1}: max_weight={max_w:.4f}, scale={w_scale:.8f}, MSE={mse:.2e}")
    
    # Calculate quantized model size
    total_int8 = sum(len(w) * len(w[0]) for w in weights_int8)
    bias_floats = sum(len(b) for b in bias_list)
    quantized_size = total_int8 + bias_floats * 4  # int8 weights + float32 biases
    print(f"\n    Quantized size: {total_int8:,} bytes (weights) + {bias_floats * 4:,} bytes (biases)")
    print(f"    Total: {quantized_size:,} bytes ({quantized_size / 1024 / 1024:.2f} MB)")
    print(f"    Compression ratio: {len(all_weights) * 4 / quantized_size:.2f}x")
    
    # Save quantized weights to binary
    print("\n[3] Saving quantized weights to weights_int8.bin...")
    with open('weights_int8.bin', 'wb') as f:
        # Write scales first (4 floats)
        for scale in weight_scales:
            f.write(struct.pack('f', scale))
        # Write weights (int8)
        for w in weights_int8:
            for row in w:
                for val in row:
                    f.write(struct.pack('b', val))  # signed char / int8
        # Write biases (float32)
        for b in bias_list:
            for val in b:
                f.write(struct.pack('f', val))
    
    # Save scales for reference
    with open('quantization_scales.txt', 'w') as f:
        for i, ws in enumerate(weight_scales):
            f.write(f"layer{i+1}_weight_scale: {ws:.8f}\n")
    print("    Scales saved to quantization_scales.txt")
    
    # Test on simple dummy input to verify correctness
    print("\n[4] Verifying quantization with dummy input...")
    test_input = [0.1] * 784  # Simple test vector
    
    # Float forward pass
    h1_f = relu(linear_float(test_input, weight_list[0], bias_list[0]))
    h2_f = relu(linear_float(h1_f, weight_list[1], bias_list[1]))
    h3_f = relu(linear_float(h2_f, weight_list[2], bias_list[2]))
    out_f = linear_float(h3_f, weight_list[3], bias_list[3])
    
    # Int8 forward pass
    h1_i = relu(linear_int8(test_input, weights_int8[0], weight_scales[0], bias_list[0]))
    h2_i = relu(linear_int8(h1_i, weights_int8[1], weight_scales[1], bias_list[1]))
    h3_i = relu(linear_int8(h2_i, weights_int8[2], weight_scales[2], bias_list[2]))
    out_i = linear_int8(h3_i, weights_int8[3], weight_scales[3], bias_list[3])
    
    print(f"    Float output[0:5]: {[f'{x:.4f}' for x in out_f[:5]]}")
    print(f"    Int8  output[0:5]: {[f'{x:.4f}' for x in out_i[:5]]}")
    print(f"    Max output diff: {max(abs(a - b) for a, b in zip(out_f, out_i)):.4f}")
    
    # Print weight sample to verify layout
    print("\n    Weight layout verification:")
    print(f"    Layer1 kernel[0][0:5] (float): {[f'{x:.6f}' for x in weight_list[0][0][:5]]}")
    print(f"    Layer1 kernel[1][0:5] (float): {[f'{x:.6f}' for x in weight_list[0][1][:5]]}")
    print(f"    Layer1 int8[0][0:5]: {weights_int8[0][0][:5]}")
    print(f"    Layer1 bias[0:5]: {[f'{x:.6f}' for x in bias_list[0][:5]]}")
    
    # Generate C header for int8 weights
    print("\n[5] Generating C header mnist_params_int8.h...")
    with open('mnist_params_int8.h', 'w') as f:
        f.write("/* Auto-generated int8 quantized MNIST model weights */\n")
        f.write("/* Weights: int8, Biases: float32 (for accuracy) */\n")
        f.write("#ifndef MNIST_PARAMS_INT8_H\n")
        f.write("#define MNIST_PARAMS_INT8_H\n\n")
        f.write("#include <stdint.h>\n\n")
        
        # Write scales as constants
        for i, ws in enumerate(weight_scales):
            f.write(f"static const float layer{i+1}_weight_scale = {ws:.8f}f;\n")
        f.write("\n")
        
        # Write biases as float arrays
        for i, b in enumerate(bias_list):
            f.write(f"static const float layer{i+1}_bias[{len(b)}] = {{\n")
            for j in range(0, len(b), 8):
                vals = ", ".join(f"{x:10.6f}f" for x in b[j:j+8])
                f.write(f"    {vals},\n")
            f.write("};\n\n")
        
        # Write weights as int8 arrays
        for i, w in enumerate(weights_int8):
            rows = len(w)
            cols = len(w[0]) if rows > 0 else 0
            f.write(f"/* Layer {i+1}: {rows} x {cols} */\n")
            f.write(f"static const int8_t layer{i+1}_kernel_int8[{rows * cols}] = {{\n")
            flat = [w[r][c] for r in range(rows) for c in range(cols)]
            for j in range(0, len(flat), 16):
                vals = ", ".join(f"{int(x):4d}" for x in flat[j:j+16])
                f.write(f"    {vals},\n")
            f.write("};\n\n")
        
        f.write("#endif /* MNIST_PARAMS_INT8_H */\n")
    
    print("    Generated mnist_params_int8.h")
    
    # Generate main_int8.c that uses the quantized weights
    print("\n[6] Generating main_int8.c (C inference with int8 weights)...")
    with open('main_int8.c', 'w') as f:
        f.write("""/* Auto-generated int8 quantized MNIST inference */
#include "mnist_params_int8.h"
#include "test_images.h"
#include <stdint.h>
#include <stdio.h>

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

int main(void) {
    int correct = 0;

    printf("Testing INT8 quantized MNIST classifier\\n");
    printf("----------------------------------------\\n");

    for (int i = 0; i < NUM_TEST; i++) {
        int pred = classify_int8(test_images[i]);
        int label = test_labels[i];
        printf("Sample %d: label=%d  predicted=%d  %s\\n", i, label, pred,
               pred == label ? "OK" : "WRONG");
        if (pred == label)
            correct++;
    }

    printf("----------------------------------------\\n");
    printf("Accuracy: %d/%d (%.0f%%)\\n", correct, NUM_TEST,
           100.0f * correct / NUM_TEST);

    return correct == NUM_TEST ? 0 : 1;
}
""")
    
    print("    Generated main_int8.c")
    print("\nDone!")
    print("\nTo compile and run:")
    print("  gcc -O2 -o mnist_int8 main_int8.c && ./mnist_int8")


if __name__ == "__main__":
    main()
