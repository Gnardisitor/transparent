file(DOWNLOAD
  "https://huggingface.co/Acly/BiRefNet-GGUF/resolve/main/BiRefNet-lite-F16.gguf"
  "${CMAKE_CURRENT_BINARY_DIR}/BiRefNet-lite-F16.gguf"
  EXPECTED_HASH SHA256=7b5397a2c98d66677f8f74317774bbeac49dbb321b8a3dc744af913db71d4fa5
  SHOW_PROGRESS
)

file(DOWNLOAD
  "https://huggingface.co/Acly/Real-ESRGAN-GGUF/resolve/main/ESRGAN-4x-foolhardy_Remacri-F16.gguf"
  "${CMAKE_CURRENT_BINARY_DIR}/ESRGAN-4x-foolhardy_Remacri-F16.gguf"
  EXPECTED_HASH SHA256=843aa7c4bcf5919b7f5b72eef8d8cfd9df9949c1837002e3f6d9bf07c1b3af5a
  SHOW_PROGRESS
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/BiRefNet-lite-F16.gguf"
  "${CMAKE_CURRENT_BINARY_DIR}/ESRGAN-4x-foolhardy_Remacri-F16.gguf"
  DESTINATION share/transparent/models
)
