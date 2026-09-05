#pragma once

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

#include "infrastructure/stick_backup/archive_file.hpp"

namespace seabass::infrastructure::stick_backup
{

// A shared "time source" for fault injection across several
// InMemoryArchiveFiles (the archive and its journal are two files, and a
// crash hits both at the same instant). Every barrier() on any file
// sharing a clock advances it by one tick; a crash is described as "after
// `tick` barriers had completed, anywhere".
struct FaultClock
{
    std::size_t ticks = 0;
};

// ArchiveFile held in memory, for tests. Besides behaving like a file it
// keeps a log of every mutation and of every barrier, so a test can ask
// "what would this file look like on disk if the process had died at
// this point?" without touching a filesystem -- the exhaustive truncation
// and write-reordering tests in tests/backup_archive_truncation_fuzz_test
// .cpp are built on this.
//
// Durability model (the one fsync actually gives you): a mutation is
// durable at crash time T iff *this file* had a barrier() at tick b with
// mutation.tick <= b < T. Anything else is in flight and may or may not
// have reached the medium -- the test decides per mutation via a
// predicate, which is how both "lost tail" and "reordered writes" cases
// get generated from one log.
class InMemoryArchiveFile final : public ArchiveFile
{
public:
    explicit InMemoryArchiveFile(std::shared_ptr<FaultClock> clock = std::make_shared<FaultClock>())
        : m_clock(std::move(clock))
    {
    }

    InMemoryArchiveFile(std::vector<std::byte> initial, std::shared_ptr<FaultClock> clock = std::make_shared<FaultClock>())
        : m_bytes(std::move(initial)), m_clock(std::move(clock))
    {
    }

    std::uint64_t size() const override { return m_bytes.size(); }

    void append(std::span<const std::byte> bytes) override
    {
        Mutation m;
        m.kind = Mutation::Kind::Append;
        m.offset = m_bytes.size();
        m.bytes.assign(bytes.begin(), bytes.end());
        m.tick = m_clock->ticks;
        m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
        m_log.push_back(std::move(m));
    }

    void readAt(std::uint64_t offset, std::span<std::byte> out) const override
    {
        if (offset + out.size() > m_bytes.size()) {
            throw ArchiveIoError("read past end of in-memory archive");
        }
        if (!out.empty()) {
            std::memcpy(out.data(), m_bytes.data() + offset, out.size());
        }
    }

    void truncate(std::uint64_t newSize) override
    {
        if (newSize > m_bytes.size()) {
            throw ArchiveIoError("truncate beyond end of in-memory archive");
        }
        Mutation m;
        m.kind = Mutation::Kind::Truncate;
        m.offset = newSize;
        m.tick = m_clock->ticks;
        m_bytes.resize(newSize);
        m_log.push_back(std::move(m));
    }

    void barrier() override
    {
        m_barrierTicks.push_back(m_clock->ticks);
        ++m_clock->ticks;
    }

    // ---- Introspection for tests ----

    const std::vector<std::byte> &bytes() const { return m_bytes; }
    const std::shared_ptr<FaultClock> &clock() const { return m_clock; }
    std::size_t mutationCount() const { return m_log.size(); }
    std::size_t barrierCount() const { return m_barrierTicks.size(); }

    // The file cut off at `length` bytes -- the plain "power went out
    // mid-write" image, independent of barriers.
    std::vector<std::byte> truncatedImage(std::uint64_t length) const
    {
        std::vector<std::byte> image(m_bytes.begin(), m_bytes.begin() + static_cast<std::ptrdiff_t>(std::min<std::uint64_t>(length, m_bytes.size())));
        return image;
    }

    // The file as it may be on disk if the process died after `crashTick`
    // barriers (across all files sharing the clock) had completed. Durable
    // mutations are always applied; each in-flight mutation is applied iff
    // `keepInFlight(mutationIndex)` says so. Replaying in log order means a
    // lost append between two kept ones reads as zeros, exactly what a
    // real filesystem shows for an unflushed hole.
    std::vector<std::byte> crashImage(std::size_t crashTick, const std::function<bool(std::size_t)> &keepInFlight) const
    {
        std::vector<std::byte> image;
        for (std::size_t i = 0; i < m_log.size(); ++i) {
            const Mutation &m = m_log[i];
            bool durable = false;
            for (std::size_t b : m_barrierTicks) {
                if (m.tick <= b && b < crashTick) {
                    durable = true;
                    break;
                }
            }
            if (!durable && !keepInFlight(i)) {
                continue;
            }
            if (m.kind == Mutation::Kind::Truncate) {
                image.resize(static_cast<std::size_t>(m.offset));
            } else {
                std::size_t end = static_cast<std::size_t>(m.offset) + m.bytes.size();
                if (image.size() < end) {
                    image.resize(end);  // zero-filled gap if a preceding append was lost
                }
                std::memcpy(image.data() + m.offset, m.bytes.data(), m.bytes.size());
            }
        }
        return image;
    }

private:
    struct Mutation
    {
        enum class Kind
        {
            Append,
            Truncate
        };
        Kind kind = Kind::Append;
        std::uint64_t offset = 0;
        std::vector<std::byte> bytes;  // Append only
        std::size_t tick = 0;
    };

    std::vector<std::byte> m_bytes;
    std::shared_ptr<FaultClock> m_clock;
    std::vector<Mutation> m_log;
    std::vector<std::size_t> m_barrierTicks;
};

}  // namespace seabass::infrastructure::stick_backup
