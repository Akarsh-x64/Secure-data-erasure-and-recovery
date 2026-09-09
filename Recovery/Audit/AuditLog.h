#pragma once

#include "AuditEvent.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace Recovery {
namespace Audit {

    /**
     * @brief In-memory, insertion-ordered audit events for one session.
     *
     * Phase 5A deliberately performs no persistence and no source writes.
     */
    class AuditLog {
    public:
        void Add(AuditEvent event) {
            m_events.push_back(std::move(event));
        }

        void Append(AuditEvent event) {
            Add(std::move(event));
        }

        void Clear() {
            m_events.clear();
        }

        bool Empty() const {
            return m_events.empty();
        }

        std::size_t Size() const {
            return m_events.size();
        }

        const std::vector<AuditEvent>& Events() const {
            return m_events;
        }

    private:
        std::vector<AuditEvent> m_events;
    };

} // namespace Audit
} // namespace Recovery
