// Copyright (C) 2009-2017, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#include <anki/renderer/Pps.h>
#include <anki/renderer/Renderer.h>
#include <anki/renderer/Bloom.h>
#include <anki/renderer/Taa.h>
#include <anki/renderer/Sslf.h>
#include <anki/renderer/Tm.h>
#include <anki/renderer/Is.h>
#include <anki/renderer/Ms.h>
#include <anki/renderer/Dbg.h>
#include <anki/renderer/Ssao.h>
#include <anki/renderer/DownscaleBlur.h>
#include <anki/util/Logger.h>
#include <anki/misc/ConfigSet.h>
#include <anki/scene/SceneNode.h>
#include <anki/scene/FrustumComponent.h>

namespace anki
{

const PixelFormat Pps::RT_PIXEL_FORMAT(ComponentFormat::R8G8B8, TransformFormat::UNORM);

Pps::Pps(Renderer* r)
	: RenderingPass(r)
{
}

Pps::~Pps()
{
}

Error Pps::initInternal(const ConfigSet& config)
{
	ANKI_ASSERT("Initializing PPS");

	ANKI_CHECK(loadColorGradingTexture("engine_data/DefaultLut.ankitex"));
	m_sharpenEnabled = config.getNumber("pps.sharpen");

	if(!m_r->getDrawToDefaultFramebuffer())
	{
		m_rt = m_r->createAndClearRenderTarget(m_r->create2DRenderTargetInitInfo(m_r->getWidth(),
			m_r->getHeight(),
			RT_PIXEL_FORMAT,
			TextureUsageBit::FRAMEBUFFER_ATTACHMENT_WRITE | TextureUsageBit::SAMPLED_FRAGMENT,
			SamplingFilter::LINEAR,
			1,
			"pps"));

		FramebufferInitInfo fbInit("pps");
		fbInit.m_colorAttachmentCount = 1;
		fbInit.m_colorAttachments[0].m_texture = m_rt;
		fbInit.m_colorAttachments[0].m_loadOperation = AttachmentLoadOperation::DONT_CARE;
		m_fb = getGrManager().newInstance<Framebuffer>(fbInit);
	}

	ANKI_CHECK(getResourceManager().loadResource("engine_data/BlueNoiseLdrRgb64x64.ankitex", m_blueNoise));

	// Progs
	ANKI_CHECK(getResourceManager().loadResource("programs/FinalComposite.ankiprog", m_prog));

	ShaderProgramResourceMutationInitList<4> mutations(m_prog);
	mutations.add("BLUE_NOISE", 1)
		.add("SHARPEN_ENABLED", m_sharpenEnabled)
		.add("BLOOM_ENABLED", 1)
		.add("DBG_ENABLED", 0);

	ShaderProgramResourceConstantValueInitList<2> consts(m_prog);
	consts.add("LUT_SIZE", U32(LUT_SIZE)).add("FB_SIZE", UVec2(m_r->getWidth(), m_r->getHeight()));

	const ShaderProgramResourceVariant* variant;
	m_prog->getOrCreateVariant(mutations.m_mutations, consts.m_constantValues, variant);
	m_grProgs[0] = variant->getProgram();

	mutations.m_mutations[3].m_value = 1;
	m_prog->getOrCreateVariant(mutations.m_mutations, consts.m_constantValues, variant);
	m_grProgs[1] = variant->getProgram();

	return ErrorCode::NONE;
}

Error Pps::init(const ConfigSet& config)
{
	Error err = initInternal(config);
	if(err)
	{
		ANKI_R_LOGE("Failed to init PPS");
	}

	return err;
}

Error Pps::loadColorGradingTexture(CString filename)
{
	m_lut.reset(nullptr);
	ANKI_CHECK(getResourceManager().loadResource(filename, m_lut));
	ANKI_ASSERT(m_lut->getWidth() == LUT_SIZE);
	ANKI_ASSERT(m_lut->getHeight() == LUT_SIZE);
	ANKI_ASSERT(m_lut->getDepth() == LUT_SIZE);

	return ErrorCode::NONE;
}

Error Pps::run(RenderingContext& ctx)
{
	CommandBufferPtr& cmdb = ctx.m_commandBuffer;

	// Get the drawing parameters
	Bool drawToDefaultFb = ctx.m_outFb.isCreated();
	Bool dbgEnabled = m_r->getDbg().getEnabled();

	// Bind stuff
	cmdb->bindTextureAndSampler(
		0, 0, m_r->getTaa().getRt(), (drawToDefaultFb) ? m_r->getNearestSampler() : m_r->getLinearSampler());
	cmdb->bindTexture(0, 1, m_r->getBloom().m_upscale.m_rt);
	cmdb->bindTexture(0, 2, m_lut->getGrTexture());
	cmdb->bindTexture(0, 3, m_blueNoise->getGrTexture());
	if(dbgEnabled)
	{
		cmdb->bindTexture(0, 5, m_r->getDbg().getRt());
	}

	cmdb->bindStorageBuffer(0, 0, m_r->getTm().m_luminanceBuff, 0, MAX_PTR_SIZE);

	Vec4* uniforms = allocateAndBindUniforms<Vec4*>(sizeof(Vec4), cmdb, 0, 0);
	uniforms->x() = F32(m_r->getFrameCount() % m_blueNoise->getLayerCount());

	// Get or create FB
	FramebufferPtr* fb = nullptr;
	U width, height;
	if(drawToDefaultFb)
	{
		fb = &ctx.m_outFb;
		width = ctx.m_outFbWidth;
		height = ctx.m_outFbHeight;
	}
	else
	{
		width = m_r->getWidth();
		height = m_r->getHeight();
		fb = &m_fb;
	}

	cmdb->beginRenderPass(*fb);
	cmdb->setViewport(0, 0, width, height);
	cmdb->bindShaderProgram(m_grProgs[dbgEnabled]);
	m_r->drawQuad(cmdb);
	cmdb->endRenderPass();

	if(!drawToDefaultFb)
	{
		cmdb->setTextureSurfaceBarrier(m_rt,
			TextureUsageBit::FRAMEBUFFER_ATTACHMENT_WRITE,
			TextureUsageBit::SAMPLED_FRAGMENT,
			TextureSurfaceInfo(0, 0, 0, 0));
	}

	return ErrorCode::NONE;
}

} // end namespace anki
