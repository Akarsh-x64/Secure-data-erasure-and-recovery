#pragma once

#include "TskFeature.h"
#include "../Core/IReadOnlyStorage.h"

#include <cstddef>
#include <cstdint>

namespace Recovery {
namespace TSK {

    /**
     * @brief Bridges IReadOnlyStorage to TSK image callbacks.
     *
     * This keeps TSK reading strictly routed through the Recovery read-only
     * abstraction and avoids opening paths directly in TSK.
     */
    class TskImageBridge {
    public:
        explicit TskImageBridge(Core::IReadOnlyStorage& storage);
        ~TskImageBridge();

        TskImageBridge(const TskImageBridge&) = delete;
        TskImageBridge& operator=(const TskImageBridge&) = delete;

        bool Open();
        void Close();

        bool IsOpen() const;

#if RECOVERY_HAS_LIBTSK
        TSK_IMG_INFO* GetImageHandle() const { return m_img; }
#else
        void* GetImageHandle() const { return nullptr; }
#endif

    private:
        Core::IReadOnlyStorage& m_storage;

#if RECOVERY_HAS_LIBTSK
        TSK_IMG_INFO* m_img;

        struct ExternalImageContext {
            TSK_IMG_INFO imgInfo;
            TskImageBridge* owner;
        };

        ExternalImageContext m_ctx;

        static ssize_t ReadCallback(TSK_IMG_INFO* img, TSK_OFF_T off, char* buf, size_t len);
        static void CloseCallback(TSK_IMG_INFO* img);
        static void ImgStatCallback(TSK_IMG_INFO* img, FILE* hFile);

        ssize_t ReadAt(uint64_t offset, char* buf, size_t len) const;
#else
        void* m_img;
#endif
    };

} // namespace TSK
} // namespace Recovery
