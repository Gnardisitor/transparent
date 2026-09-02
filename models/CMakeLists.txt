# Default models, fetched at configure time so a fresh build works offline on
# first launch (ModelManager::ensureDefaultsProvisioned copies them into
# AppDataLocation). SHA256s must match ModelCatalog.cpp.

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

# SCUNet real GAN (Apache-2.0): the default denoise model. Converted by the
# vision.cpp fork; hosted on the project's own Forgejo LFS repo (see PLAN.md,
# "Denoising").
file(DOWNLOAD
  "https://forge.db-serve.com/dbajan/transparent-models/media/branch/main/scunet-color-real-gan-F16.gguf"
  "${CMAKE_CURRENT_BINARY_DIR}/scunet-color-real-gan-F16.gguf"
  EXPECTED_HASH SHA256=692e7b7d24979a1fc2ca15f2984ded55ff7b6f7bf24882a7c9aa81a03a0c9a7e
  SHOW_PROGRESS
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/BiRefNet-lite-F16.gguf"
  "${CMAKE_CURRENT_BINARY_DIR}/ESRGAN-4x-foolhardy_Remacri-F16.gguf"
  "${CMAKE_CURRENT_BINARY_DIR}/scunet-color-real-gan-F16.gguf"
  DESTINATION share/transparent/models
)
