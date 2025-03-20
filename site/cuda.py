from numba import cuda
import numpy as np

# CUDA kernel function
@cuda.jit
def gpu_add(a, b, c):
    i = cuda.grid(1)  # Get thread index
    if i < a.size:
        c[i] = a[i] + b[i]

# Define array size
N = 1000000

a = np.random.rand(N).astype(np.float32)
b = np.random.rand(N).astype(np.float32)
c = np.zeros(N, dtype=np.float32)

# Allocate GPU memory
d_a = cuda.to_device(a)
d_b = cuda.to_device(b)
d_c = cuda.device_array(N, dtype=np.float32)

# Define number of threads per block and blocks per grid
threads_per_block = 256
blocks_per_grid = (N + threads_per_block - 1) // threads_per_block

# Launch kernel
gpu_add[blocks_per_grid, threads_per_block](d_a, d_b, d_c)  # Now works correctly

# Copy result back to host
c = d_c.copy_to_host()

# Verify the result
np.testing.assert_allclose(c, a + b, rtol=1e-5)
print("CUDA addition successful!")
