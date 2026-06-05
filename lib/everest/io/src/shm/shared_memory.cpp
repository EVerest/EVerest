// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include <fmt/core.h>

#include <everest/io/shm/shared_memory.hpp>

namespace everest::lib::io::shm {

shared_memory::shared_memory(const std::string& name, std::size_t size, bool create) :
    m_name(name), m_size(size), m_fd(-1), m_ptr(MAP_FAILED) {

    int flags = O_RDWR;
    if (create) {
        flags |= O_CREAT;
    }

    this->m_fd = shm_open(name.c_str(), flags, 0666);
    if (this->m_fd == -1) {
        throw std::runtime_error(
            fmt::format("Could not open shared memory segment '{}': {}", name, std::strerror(errno)));
    }

    if (create) {
        if (ftruncate(this->m_fd, static_cast<off_t>(size)) == -1) {
            ::close(this->m_fd);
            this->m_fd = -1;
            throw std::runtime_error(fmt::format("Could not truncate shared memory segment '{}' to size {}: {}", name,
                                                 size, std::strerror(errno)));
        }
    }

    this->m_ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, this->m_fd, 0);
    if (this->m_ptr == MAP_FAILED) {
        ::close(this->m_fd);
        this->m_fd = -1;
        throw std::runtime_error(
            fmt::format("Could not mmap shared memory segment '{}': {}", name, std::strerror(errno)));
    }
}

shared_memory::~shared_memory() {
    cleanup();
}

shared_memory::shared_memory(shared_memory&& other) noexcept :
    m_name(std::move(other.m_name)), m_size(other.m_size), m_fd(other.m_fd), m_ptr(other.m_ptr) {
    other.m_size = 0;
    other.m_fd = -1;
    other.m_ptr = MAP_FAILED;
}

shared_memory& shared_memory::operator=(shared_memory&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_name = std::move(other.m_name);
        m_size = other.m_size;
        m_fd = other.m_fd;
        m_ptr = other.m_ptr;
        other.m_size = 0;
        other.m_fd = -1;
        other.m_ptr = MAP_FAILED;
    }
    return *this;
}

void* shared_memory::get_ptr() const {
    return m_ptr;
}

std::size_t shared_memory::get_size() const {
    return m_size;
}

const std::string& shared_memory::get_name() const {
    return m_name;
}

void shared_memory::unlink() {
    if (!m_name.empty()) {
        shm_unlink(m_name.c_str());
    }
}

void shared_memory::cleanup() {
    if (m_ptr != MAP_FAILED) {
        munmap(m_ptr, m_size);
        m_ptr = MAP_FAILED;
    }
    if (m_fd != -1) {
        ::close(m_fd);
        m_fd = -1;
    }
}

} // namespace everest::lib::io::shm
