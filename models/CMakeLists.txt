# Downloads model weights at configure time instead of committing them to
# git — the same file(DOWNLOAD ... EXPECTED_HASH ...) pattern vision.cpp's
# own models/CMakeLists.txt uses for its default models. Lands in the build
# directory (already gitignored), not the source tree.

# BiRefNet-lite: MIT, from ZhengPeng7/BiRefNet (github.com/zhengpeng7/birefnet).
# Converted to GGUF by Acly for vision.cpp: huggingface.co/Acly/BiRefNet-GGUF.
file(DOWNLOAD
  "https://huggingface.co/Acly/BiRefNet-GGUF/resolve/main/BiRefNet-lite-F16.gguf"
  "${CMAKE_CURRENT_BINARY_DIR}/BiRefNet-lite-F16.gguf"
  EXPECTED_HASH SHA256=7b5397a2c98d66677f8f74317774bbeac49dbb321b8a3dc744af913db71d4fa5
  SHOW_PROGRESS
)

# Real-ESRGAN (foolhardy_Remacri checkpoint): BSD-3-Clause, from
# xinntao/Real-ESRGAN. Converted to GGUF by Acly for vision.cpp:
# huggingface.co/Acly/Real-ESRGAN-GGUF. Picked over the repo's other
# checkpoints because esrgan_load_model doesn't support the "plus"/
# pixel-shuffle Real-ESRGAN variants (e.g. RealESRGAN-x4plus_anime-6B); this
# one is the plain RRDB architecture and general-purpose (not anime-specific).
file(DOWNLOAD
  "https://huggingface.co/Acly/Real-ESRGAN-GGUF/resolve/main/ESRGAN-4x-foolhardy_Remacri-F16.gguf"
  "${CMAKE_CURRENT_BINARY_DIR}/ESRGAN-4x-foolhardy_Remacri-F16.gguf"
  EXPECTED_HASH SHA256=843aa7c4bcf5919b7f5b72eef8d8cfd9df9949c1837002e3f6d9bf07c1b3af5a
  SHOW_PROGRESS
)
