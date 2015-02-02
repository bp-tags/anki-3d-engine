// Copyright (C) 2009-2015, Panagiotis Christopoulos Charitos.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#ifndef ANKI_GL_GL_FRAMEBUFFER_HANDLE_H
#define ANKI_GL_GL_FRAMEBUFFER_HANDLE_H

#include "anki/gl/GlContainerHandle.h"
#include "anki/gl/GlFramebuffer.h"

namespace anki {

// Forward
class GlCommandBufferHandle;

/// @addtogroup opengl_other
/// @{

/// Attachment.
struct GlAttachment
{
	GlTextureHandle m_texture;
	U32 m_layer = 0;
	GlAttachmentLoadOperation m_loadOp;
	GlAttachmentStoreOperation m_storeOp;
};

/// GlFramebuffer initializer.
struct GlFramebufferInitializer
{
	GlAttachment* m_colorAttachments = nullptr;
	U32 m_colorAttachmentsCount = 0;
	GlAttachment* m_depthStencilAttachment = nullptr;
};

/// Framebuffer handle
class GlFramebufferHandle: public GlContainerHandle<GlFramebuffer>
{
public:
	using Base = GlContainerHandle<GlFramebuffer>;
	using Attachment = GlFramebuffer::Attachment;
	using Initializer = GlFramebufferInitializer;

	GlFramebufferHandle();

	~GlFramebufferHandle();

	/// Create a framebuffer
	ANKI_USE_RESULT Error create(
		GlCommandBufferHandle& commands,
		const std::initializer_list<Attachment>& attachments);

	/// Bind it to the state
	/// @param commands The command buffer
	/// @param invalidate If true invalidate the FB after binding it
	void bind(GlCommandBufferHandle& commands, Bool invalidate);

	/// Blit another framebuffer to this
	/// @param[in, out] commands The command buffer
	/// @param[in] b The sorce framebuffer
	/// @param[in] sourceRect The source rectangle
	/// @param[in] destRect The destination rectangle
	/// @param attachmentMask The attachments to blit
	/// @param linear Perform linean filtering
	void blit(GlCommandBufferHandle& commands,
		const GlFramebufferHandle& b, 
		const Array<U32, 4>& sourceRect,
		const Array<U32, 4>& destRect, 
		GLbitfield attachmentMask,
		Bool linear);
};
/// @}

} // end namespace anki

#endif

