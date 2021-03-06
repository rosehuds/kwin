/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "drm_buffer.h"

#include "logging.h"
#include "drm_gpu.h"

// system
#include <sys/mman.h>
// c++
#include <cerrno>
// drm
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

namespace KWin
{

DrmBuffer:: DrmBuffer(DrmGpu *gpu, uint32_t format, uint64_t modifier)
    : m_gpu(gpu)
    , m_format(format)
    , m_modifier(modifier)
{
}

// DrmDumbBuffer
DrmDumbBuffer::DrmDumbBuffer(DrmGpu *gpu, const QSize &size)
    : DrmBuffer(gpu, DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR)
{
    m_size = size;
    drm_mode_create_dumb createArgs;
    memset(&createArgs, 0, sizeof createArgs);
    createArgs.bpp = 32;
    createArgs.width = size.width();
    createArgs.height = size.height();
    if (drmIoctl(m_gpu->fd(), DRM_IOCTL_MODE_CREATE_DUMB, &createArgs) != 0) {
        qCWarning(KWIN_DRM) << "DRM_IOCTL_MODE_CREATE_DUMB failed" << strerror(errno);
        return;
    }
    m_handle = createArgs.handle;
    m_bufferSize = createArgs.size;
    m_stride = createArgs.pitch;
    if (drmModeAddFB(m_gpu->fd(), size.width(), size.height(), 24, 32,
                     m_stride, createArgs.handle, &m_bufferId) != 0) {
        qCWarning(KWIN_DRM) << "drmModeAddFB failed!" << strerror(errno);
    }
}

DrmDumbBuffer::~DrmDumbBuffer()
{
    if (m_bufferId) {
        drmModeRmFB(m_gpu->fd(), m_bufferId);
    }

    delete m_image;
    if (m_memory) {
        munmap(m_memory, m_bufferSize);
    }
    if (m_handle) {
        drm_mode_destroy_dumb destroyArgs;
        destroyArgs.handle = m_handle;
        drmIoctl(m_gpu->fd(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroyArgs);
    }
}

bool DrmDumbBuffer::needsModeChange(DrmBuffer *b) const {
    if (DrmDumbBuffer *db = dynamic_cast<DrmDumbBuffer*>(b)) {
        return m_stride != db->stride();
    } else {
        return true;
    }
}

bool DrmDumbBuffer::map(QImage::Format format)
{
    if (!m_handle || !m_bufferId) {
        return false;
    }
    drm_mode_map_dumb mapArgs;
    memset(&mapArgs, 0, sizeof mapArgs);
    mapArgs.handle = m_handle;
    if (drmIoctl(m_gpu->fd(), DRM_IOCTL_MODE_MAP_DUMB, &mapArgs) != 0) {
        return false;
    }
    void *address = mmap(nullptr, m_bufferSize, PROT_WRITE, MAP_SHARED, m_gpu->fd(), mapArgs.offset);
    if (address == MAP_FAILED) {
        return false;
    }
    m_memory = address;
    m_image = new QImage((uchar*)m_memory, m_size.width(), m_size.height(), m_stride, format);
    return !m_image->isNull();
}

}
