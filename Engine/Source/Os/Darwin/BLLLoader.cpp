#include "Engine/Source/Os/BLLLoader.h"

// ʕ •ᴥ•ʔ✿ BLL images wrap Windows PE modules and cannot be mapped on macOS ✿ ʕ •ᴥ•ʔ
bool BLLLoader::Load(const void* data, uint32_t size) {
    return false;
}

void BLLLoader::Unload() {
    this->m_image = nullptr;
    this->m_imageSize = 0;
    this->m_loadedDlls = 0;
}
