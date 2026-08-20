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
