#include "TskImageBridge.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace Recovery {
namespace TSK {

TskImageBridge::TskImageBridge(Core::IReadOnlyStorage& storage)
    : m_storage(storage)
    , m_img(nullptr)
#if RECOVERY_HAS_LIBTSK
    , m_ctx{}
#endif
{
#if RECOVERY_HAS_LIBTSK
    std::memset(&m_ctx, 0, sizeof(m_ctx));
    m_ctx.owner = this;
#endif
}

TskImageBridge::~TskImageBridge() {
    Close();
}

bool TskImageBridge::Open() {
#if RECOVERY_HAS_LIBTSK
    if (m_img != nullptr) {
        return true;
    }

    const uint64_t storageSize = m_storage.GetSize();
    const uint32_t sectorSize = m_storage.GetSectorSize() != 0 ? m_storage.GetSectorSize() : 512;

    if (storageSize == 0) {
        return false;
    }

    std::memset(&m_ctx.imgInfo, 0, sizeof(m_ctx.imgInfo));
    m_ctx.owner = this;

    m_img = tsk_img_open_external(
        &m_ctx,
        static_cast<TSK_OFF_T>(storageSize),
        static_cast<unsigned int>(sectorSize),
        &TskImageBridge::ReadCallback,
        &TskImageBridge::CloseCallback,
        &TskImageBridge::ImgStatCallback);

    if (m_img == nullptr) {
        return false;
    }
    return true;
#else
    return false;
#endif
}

void TskImageBridge::Close() {
#if RECOVERY_HAS_LIBTSK
    if (m_img == nullptr) {
        return;
    }

    tsk_img_close(m_img);
    m_img = nullptr;
    std::memset(&m_ctx.imgInfo, 0, sizeof(m_ctx.imgInfo));
    m_ctx.owner = this;
#endif
}

bool TskImageBridge::IsOpen() const {
#if RECOVERY_HAS_LIBTSK
    return m_img != nullptr;
#else
    return false;
#endif
}

#if RECOVERY_HAS_LIBTSK
ssize_t TskImageBridge::ReadAt(uint64_t offset, char* buf, size_t len) const {
    if (buf == nullptr || len == 0) {
        return -1;
    }

    if (offset >= m_storage.GetSize()) {
        return 0;
    }

    uint64_t remainingInStorage = m_storage.GetSize() - offset;
    size_t toRead = static_cast<size_t>(std::min<uint64_t>(remainingInStorage, len));

    size_t totalRead = 0;
    while (totalRead < toRead) {
        uint32_t chunk = static_cast<uint32_t>(
            std::min<size_t>(toRead - totalRead,
                             static_cast<size_t>(std::numeric_limits<uint32_t>::max())));

        if (!m_storage.Read(offset + totalRead, chunk, buf + totalRead)) {
            return totalRead > 0 ? static_cast<ssize_t>(totalRead) : -1;
        }

        totalRead += chunk;
    }

    return static_cast<ssize_t>(totalRead);
}

ssize_t TskImageBridge::ReadCallback(TSK_IMG_INFO* img, TSK_OFF_T off, char* buf, size_t len) {
    if (img == nullptr || buf == nullptr || off < 0) {
        return -1;
    }

    ExternalImageContext* ctx = reinterpret_cast<ExternalImageContext*>(img);
    if (ctx->owner == nullptr) {
        return -1;
    }

    return ctx->owner->ReadAt(static_cast<uint64_t>(off), buf, len);
}

void TskImageBridge::CloseCallback(TSK_IMG_INFO* /*img*/) {
    // No-op: IReadOnlyStorage lifetime is managed by the Recovery module,
    // and no underlying write/close operation is delegated to TSK.
}

void TskImageBridge::ImgStatCallback(TSK_IMG_INFO* img, FILE* hFile) {
    if (img == nullptr || hFile == nullptr) {
        return;
    }

    std::fprintf(hFile, "Recovery IReadOnlyStorage-backed TSK image\n");
    std::fprintf(hFile, "Size: %" PRIdOFF " bytes\n", img->size);
    std::fprintf(hFile, "Sector size: %u bytes\n", img->sector_size);
}
#endif

} // namespace TSK
} // namespace Recovery
