// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstddef>
#include <string>

namespace everest::lib::io::shm {

/// \brief A RAII wrapper for Linux Shared Memory segments.
class shared_memory {
public:
    /// \brief Opens or creates a shared memory segment.
    /// \param name The name of the segment (should start with /).
    /// \param size The size of the segment in bytes.
    /// \param create If true, create the segment if it doesn't exist.
    shared_memory(const std::string& name, std::size_t size, bool create = false);

    /// \brief Closes and unmaps the shared memory segment.
    ~shared_memory();

    // Disable copy
    shared_memory(const shared_memory&) = delete;
    shared_memory& operator=(const shared_memory&) = delete;

    // Enable move
    shared_memory(shared_memory&& other) noexcept;
    shared_memory& operator=(shared_memory&& other) noexcept;

    /// \returns A pointer to the start of the shared memory segment.
    void* get_ptr() const;

    /// \returns The size of the shared memory segment.
    std::size_t get_size() const;

    /// \returns The name of the shared memory segment.
    const std::string& get_name() const;

    /// \brief Unlinks the shared memory segment from the system.
    /// This means the segment will be deleted once all processes have closed it.
    void unlink();

private:
    std::string m_name;
    std::size_t m_size;
    int m_fd;
    void* m_ptr;

    void cleanup();
};

} // namespace everest::lib::io::shm
