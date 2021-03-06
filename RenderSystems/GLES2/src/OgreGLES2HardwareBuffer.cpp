/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2014 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/

#include "OgreGLES2HardwareBuffer.h"
#include "OgreRoot.h"
#include "OgreGLES2RenderSystem.h"
#include "OgreGLES2StateCacheManager.h"

namespace Ogre {
    GLES2HardwareBuffer::GLES2HardwareBuffer(GLenum target, size_t sizeInBytes, GLenum usage)
        : mTarget(target), mSizeInBytes(sizeInBytes), mUsage(usage)
    {
        mRenderSystem = static_cast<GLES2RenderSystem*>(Root::getSingleton().getRenderSystem());
        createBuffer();
    }

    GLES2HardwareBuffer::~GLES2HardwareBuffer()
    {
        destroyBuffer();
    }

    void GLES2HardwareBuffer::createBuffer()
    {
        OGRE_CHECK_GL_ERROR(glGenBuffers(1, &mBufferId));

        if (!mBufferId)
        {
            OGRE_EXCEPT(Exception::ERR_INTERNAL_ERROR,
                        "Cannot create GL ES buffer",
                        "GLES2HardwareBuffer::createBuffer");
        }
        
        mRenderSystem->_getStateCacheManager()->bindGLBuffer(mTarget, mBufferId);

        if(mRenderSystem->getCapabilities()->hasCapability(RSC_DEBUG))
        {
            OGRE_CHECK_GL_ERROR(glLabelObjectEXT(GL_BUFFER_OBJECT_EXT, mBufferId, 0, ("Buffer #" + StringConverter::toString(mBufferId)).c_str()));
        }

        OGRE_CHECK_GL_ERROR(glBufferData(mTarget, mSizeInBytes, NULL, getGLUsage(mUsage)));
    }

    void GLES2HardwareBuffer::destroyBuffer()
    {
        // Delete the cached value
        if(GLES2StateCacheManager* stateCacheManager = mRenderSystem->_getStateCacheManager())
            stateCacheManager->deleteGLBuffer(mTarget, mBufferId);
    }

    void* GLES2HardwareBuffer::lockImpl(size_t offset, size_t length,
                                        HardwareBuffer::LockOptions options)
    {
        GLenum access = 0;

        // Use glMapBuffer
        mRenderSystem->_getStateCacheManager()->bindGLBuffer(mTarget, mBufferId);

        void* pBuffer = NULL;
        if(!OGRE_NO_GLES3_SUPPORT || mRenderSystem->checkExtension("GL_EXT_map_buffer_range"))
        {
            if (mUsage & HardwareBuffer::HBU_WRITE_ONLY)
            {
                access = GL_MAP_WRITE_BIT_EXT;
                access |= GL_MAP_FLUSH_EXPLICIT_BIT_EXT;
                if (options == HardwareBuffer::HBL_DISCARD ||
                    options == HardwareBuffer::HBL_NO_OVERWRITE)
                {
                    // Discard the buffer
                    access |= GL_MAP_INVALIDATE_RANGE_BIT_EXT;
                }
            }
            else if (options == HardwareBuffer::HBL_READ_ONLY)
                access = GL_MAP_READ_BIT_EXT;
            else
                access = GL_MAP_READ_BIT_EXT | GL_MAP_WRITE_BIT_EXT;

            OGRE_CHECK_GL_ERROR(pBuffer = glMapBufferRangeEXT(mTarget, offset, length, access));
            // pBuffer is already offsetted in glMapBufferRange
            offset = 0;
        }
#if OGRE_NO_GLES3_SUPPORT == 1
        else if(mRenderSystem->checkExtension("GL_OES_mapbuffer"))
        {
            // Use glMapBuffer
            if (options == HardwareBuffer::HBL_DISCARD ||
                options == HardwareBuffer::HBL_NO_OVERWRITE)
            {
                // Discard the buffer
                OGRE_CHECK_GL_ERROR(
                    glBufferData(mTarget, (GLsizeiptr)mSizeInBytes, NULL, getGLUsage(mUsage)));
            }
            if (mUsage & HardwareBuffer::HBU_WRITE_ONLY)
                access = GL_WRITE_ONLY_OES;

            OGRE_CHECK_GL_ERROR(pBuffer = glMapBufferOES(mTarget, access));
        }
#endif

        if (!pBuffer)
        {
            OGRE_EXCEPT(Exception::ERR_INTERNAL_ERROR, "Buffer: Out of memory",
                        "GLES2HardwareBuffer::lock");
        }

        // return offsetted
        return static_cast<uint8*>(pBuffer) + offset;
    }

    void GLES2HardwareBuffer::unlockImpl(size_t lockSize)
    {
        mRenderSystem->_getStateCacheManager()->bindGLBuffer(mTarget, mBufferId);

        bool hasMapBufferRange = !OGRE_NO_GLES3_SUPPORT || mRenderSystem->checkExtension("GL_EXT_map_buffer_range");
        if ((mUsage & HardwareBuffer::HBU_WRITE_ONLY) && hasMapBufferRange)
        {
            OGRE_CHECK_GL_ERROR(glFlushMappedBufferRangeEXT(mTarget, 0, lockSize));
        }

        if(hasMapBufferRange || mRenderSystem->checkExtension("GL_OES_mapbuffer")) {
            GLboolean mapped;
            OGRE_CHECK_GL_ERROR(mapped = glUnmapBufferOES(mTarget));
            if(!mapped)
            {
                OGRE_EXCEPT(Exception::ERR_INTERNAL_ERROR, "Buffer data corrupted, please reload",
                            "GLES2HardwareBuffer::unlock");
            }
        }
    }

    void GLES2HardwareBuffer::readData(size_t offset, size_t length, void* pDest)
    {
        if(!OGRE_NO_GLES3_SUPPORT || mRenderSystem->checkExtension("GL_EXT_map_buffer_range"))
        {
            // Map the buffer range then copy out of it into our destination buffer
            void* srcData;
            OGRE_CHECK_GL_ERROR(srcData = glMapBufferRangeEXT(mTarget, offset, length, GL_MAP_READ_BIT_EXT));
            memcpy(pDest, srcData, length);

            // Unmap the buffer since we are done.
            GLboolean mapped;
            OGRE_CHECK_GL_ERROR(mapped = glUnmapBufferOES(mTarget));
            if(!mapped)
            {
                OGRE_EXCEPT(Exception::ERR_INTERNAL_ERROR, "Buffer data corrupted, please reload",
                            "GLES2HardwareBuffer::readData");
            }
        }
        else
        {
            OGRE_EXCEPT(Exception::ERR_INTERNAL_ERROR, "Read hardware buffer is not supported",
                        "GLES2HardwareBuffer::readData");
        }
    }

    void GLES2HardwareBuffer::writeData(size_t offset, size_t length, const void* pSource,
                                        bool discardWholeBuffer)
    {
        mRenderSystem->_getStateCacheManager()->bindGLBuffer(mTarget, mBufferId);

        if (offset == 0 && length == mSizeInBytes)
        {
            OGRE_CHECK_GL_ERROR(glBufferData(mTarget, mSizeInBytes, pSource, getGLUsage(mUsage)));
        }
        else
        {
            if(discardWholeBuffer)
            {
                OGRE_CHECK_GL_ERROR(glBufferData(mTarget, mSizeInBytes, NULL, getGLUsage(mUsage)));
            }

            OGRE_CHECK_GL_ERROR(glBufferSubData(mTarget, offset, length, pSource));
        }
    }

    void GLES2HardwareBuffer::copyData(GLuint srcBufferId, size_t srcOffset, size_t dstOffset,
                                       size_t length, bool discardWholeBuffer)
    {
#if OGRE_NO_GLES3_SUPPORT == 0
        // Zero out this(destination) buffer
        OGRE_CHECK_GL_ERROR(glBindBuffer(mTarget, mBufferId));
        OGRE_CHECK_GL_ERROR(glBufferData(mTarget, length, 0, getGLUsage(mUsage)));
        OGRE_CHECK_GL_ERROR(glBindBuffer(mTarget, 0));

        // Do it the fast way.
        OGRE_CHECK_GL_ERROR(glBindBuffer(GL_COPY_READ_BUFFER, srcBufferId));
        OGRE_CHECK_GL_ERROR(glBindBuffer(GL_COPY_WRITE_BUFFER, mBufferId));

        OGRE_CHECK_GL_ERROR(glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, srcOffset, dstOffset, length));

        OGRE_CHECK_GL_ERROR(glBindBuffer(GL_COPY_READ_BUFFER, 0));
        OGRE_CHECK_GL_ERROR(glBindBuffer(GL_COPY_WRITE_BUFFER, 0));
#else
        OgreAssert(false, "GLES3 needed");
#endif
    }

    GLenum GLES2HardwareBuffer::getGLUsage(unsigned int usage)
    {
        return  (usage & HardwareBuffer::HBU_DISCARDABLE) ? GL_STREAM_DRAW :
                (usage & HardwareBuffer::HBU_STATIC) ? GL_STATIC_DRAW :
                GL_DYNAMIC_DRAW;
    }
}
