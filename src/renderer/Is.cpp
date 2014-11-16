// Copyright (C) 2014, Panagiotis Christopoulos Charitos.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#include "anki/renderer/Is.h"
#include "anki/renderer/Renderer.h"
#include "anki/scene/SceneGraph.h"
#include "anki/scene/Camera.h"
#include "anki/scene/Light.h"
#include "anki/core/Counters.h"
#include "anki/util/Logger.h"
#include "anki/misc/ConfigSet.h"

namespace anki {

//==============================================================================
// Misc                                                                        =
//==============================================================================

//==============================================================================
/// Clamp a value
template<typename T, typename Y>
void clamp(T& in, Y limit)
{
	in = std::min(in, (T)limit);
}

//==============================================================================
// Shader structs and block representations. All positions and directions in
// viewspace
// For documentation see the shaders

namespace shader {

class Light
{
public:
	Vec4 m_posRadius;
	Vec4 m_diffuseColorShadowmapId;
	Vec4 m_specularColorTexId;
};

class PointLight: public Light
{};

class SpotLight: public Light
{
public:
	Vec4 m_lightDir;
	Vec4 m_outerCosInnerCos;
	Array<Vec4, 4> m_extendPoints;
};

class SpotTexLight: public SpotLight
{
public:
	Mat4 m_texProjectionMat; ///< Texture projection matrix
};

class CommonUniforms
{
public:
	Vec4 m_projectionParams;
	Vec4 m_sceneAmbientColor;
	Vec4 m_groundLightDir;
};

} // end namespace shader

//==============================================================================
/// Write the lights to a client buffer
class WriteLightsJob: public Threadpool::Task
{
public:
	shader::PointLight* m_pointLights = nullptr;
	shader::SpotLight* m_spotLights = nullptr;
	shader::SpotTexLight* m_spotTexLights = nullptr;

	U8* m_tileBuffer = nullptr;

	VisibilityTestResults::Container::const_iterator m_lightsBegin;
	VisibilityTestResults::Container::const_iterator m_lightsEnd;

	AtomicU32* m_pointLightsCount = nullptr;
	AtomicU32* m_spotLightsCount = nullptr;
	AtomicU32* m_spotTexLightsCount = nullptr;
		
	Array2d<AtomicU32, 
		ANKI_RENDERER_MAX_TILES_Y, 
		ANKI_RENDERER_MAX_TILES_X>
		* m_tilePointLightsCount = nullptr,
		* m_tileSpotLightsCount = nullptr,
		* m_tileSpotTexLightsCount = nullptr;

	Tiler* m_tiler = nullptr;
	Is* m_is = nullptr;

	/// Bin lights on CPU path
	Bool m_binLights = true;

	Error operator()(U32 threadId, PtrSize threadsCount)
	{
		U ligthsCount = m_lightsEnd - m_lightsBegin;

		// Count job bounds
		PtrSize start, end;
		choseStartEnd(threadId, threadsCount, ligthsCount, start, end);

		// Run all lights
		for(U64 i = start; i < end; i++)
		{
			SceneNode* snode = (*(m_lightsBegin + i)).m_node;
			Light* light = staticCastPtr<Light*>(snode);

			switch(light->getLightType())
			{
			case Light::Type::POINT:
				{
					PointLight& l = 
						*staticCastPtr<PointLight*>(light);
					I pos = doLight(l);
					if(m_binLights && pos != -1)
					{
						binLight(l, pos);
					}
				}
				break;
			case Light::Type::SPOT:
				{
					SpotLight& l = *staticCastPtr<SpotLight*>(light);
					I pos = doLight(l);
					if(m_binLights && pos != -1)
					{
						binLight(l, pos);
					}
				}
				break;
			default:
				ANKI_ASSERT(0);
				break;
			}
		}

		return ErrorCode::NONE;
	}

	/// Copy CPU light to GPU buffer
	I doLight(const PointLight& light)
	{
		// Get GPU light
		I i = m_pointLightsCount->fetch_add(1);
		if(i >= (I)m_is->m_maxPointLights)
		{
			return -1;
		}

		shader::PointLight& slight = m_pointLights[i];

		const Camera* cam = m_is->m_cam;
		ANKI_ASSERT(cam);
	
		Vec4 pos = 
			cam->getViewMatrix() * light.getWorldTransform().getOrigin().xyz1();

		slight.m_posRadius = Vec4(pos.xyz(), -1.0 / light.getRadius());
		slight.m_diffuseColorShadowmapId = light.getDiffuseColor();
		slight.m_specularColorTexId = light.getSpecularColor();

		return i;
	}

	/// Copy CPU spot light to GPU buffer
	I doLight(const SpotLight& light)
	{
		const Camera* cam = m_is->m_cam;
		Bool isTexLight = light.getShadowEnabled();
		I i;
		shader::SpotLight* baseslight = nullptr;

		if(isTexLight)
		{
			// Spot tex light

			i = m_spotTexLightsCount->fetch_add(1);
			if(i >= (I)m_is->m_maxSpotTexLights)
			{
				return -1;
			}

			shader::SpotTexLight& slight = m_spotTexLights[i];
			baseslight = &slight;

			// Write matrix
			static const Mat4 biasMat4(
				0.5, 0.0, 0.0, 0.5, 
				0.0, 0.5, 0.0, 0.5, 
				0.0, 0.0, 0.5, 0.5, 
				0.0, 0.0, 0.0, 1.0);
			// bias * proj_l * view_l * world_c
			slight.m_texProjectionMat = biasMat4 * light.getProjectionMatrix() 
				* Mat4::combineTransformations(light.getViewMatrix(),
				Mat4(cam->getWorldTransform()));

			// Transpose because of driver bug
			slight.m_texProjectionMat.transpose();
		}
		else
		{
			// Spot light without texture

			i = m_spotLightsCount->fetch_add(1);
			if(i >= (I)m_is->m_maxSpotLights)
			{
				return -1;
			}

			shader::SpotLight& slight = m_spotLights[i];
			baseslight = &slight;
		}

		// Write common stuff
		ANKI_ASSERT(baseslight);

		// Pos & dist
		Vec4 pos = 
			cam->getViewMatrix() * light.getWorldTransform().getOrigin().xyz1();
		baseslight->m_posRadius = Vec4(pos.xyz(), -1.0 / light.getDistance());

		// Diff color and shadowmap ID now
		baseslight->m_diffuseColorShadowmapId = 
			Vec4(light.getDiffuseColor().xyz(), (F32)light.getShadowMapIndex());

		// Spec color
		baseslight->m_specularColorTexId = light.getSpecularColor();

		// Light dir
		Vec3 lightDir = -light.getWorldTransform().getRotation().getZAxis();
		lightDir = cam->getViewMatrix().getRotationPart() * lightDir;
		baseslight->m_lightDir = Vec4(lightDir, 0.0);
		
		// Angles
		baseslight->m_outerCosInnerCos = Vec4(
			light.getOuterAngleCos(),
			light.getInnerAngleCos(), 
			1.0, 
			1.0);

		// extend points
		const PerspectiveFrustum& frustum = light.getFrustum();

		for(U i = 0; i < 4; i++)
		{
			Vec4 extendPoint = light.getWorldTransform().getOrigin() 
				+ frustum.getLineSegments()[i].getDirection();

			extendPoint = cam->getViewMatrix() * extendPoint.xyz1();
			baseslight->m_extendPoints[i] = extendPoint;
		}

		return i;
	}

	// Bin point light
	void binLight(PointLight& light, U pos)
	{
		// Do the tests
		Tiler::Bitset bitset;
		m_tiler->test(light.getSpatialCollisionShape(), true, &bitset);

		// Bin to the correct tiles
		PtrSize tilesCount = 
			m_is->m_r->getTilesCount().x() * m_is->m_r->getTilesCount().y();
		for(U t = 0; t < tilesCount; t++)
		{
			// If not in tile bye
			if(!bitset.test(t))
			{
				continue;
			}

			U x = t % m_is->m_r->getTilesCount().x();
			U y = t / m_is->m_r->getTilesCount().x();

			U tilePos = (*m_tilePointLightsCount)[y][x].fetch_add(1);

			if(tilePos < m_is->m_maxPointLightsPerTile)
			{
				writeIndexToTileBuffer(0, pos, tilePos, t);
			}
		}
	}

	// Bin spot light
	void binLight(SpotLight& light, U pos)
	{
		// Do the tests
		Tiler::Bitset bitset;
		m_tiler->test(light.getSpatialCollisionShape(), true, &bitset);

		// Bin to the correct tiles
		PtrSize tilesCount = 
			m_is->m_r->getTilesCount().x() * m_is->m_r->getTilesCount().y();
		for(U t = 0; t < tilesCount; t++)
		{
			// If not in tile bye
			if(!bitset.test(t))
			{
				continue;
			}

			U x = t % m_is->m_r->getTilesCount().x();
			U y = t / m_is->m_r->getTilesCount().x();

			if(light.getShadowEnabled())
			{
				U tilePos = (*m_tileSpotTexLightsCount)[y][x].fetch_add(1);

				if(tilePos < m_is->m_maxSpotTexLightsPerTile)
				{
					writeIndexToTileBuffer(2, pos, tilePos, t);
				}
			}
			else
			{
				U tilePos = (*m_tileSpotLightsCount)[y][x].fetch_add(1);

				if(tilePos < m_is->m_maxSpotLightsPerTile)
				{
					writeIndexToTileBuffer(1, pos, tilePos, t);
				}
			}
		}
	}

	/// The "Tile" structure varies so this custom function writes to it
	void writeIndexToTileBuffer(
		U lightType, U lightIndex, U indexInTileArray, U tileIndex)
	{
		PtrSize offset = 
			tileIndex * m_is->calcTileSize() 
			+ sizeof(UVec4); // Tile header

		// Move to the correct light section
		switch(lightType)
		{
		case 0:
			break;
		case 1:
			offset += m_is->m_maxPointLightsPerTile * sizeof(U32);
			break;
		case 2:
			offset += 
				(m_is->m_maxSpotLightsPerTile + m_is->m_maxPointLightsPerTile)
				* sizeof(U32);
			break;
		default:
			ANKI_ASSERT(0);
		}

		// Move to the array offset
		offset += sizeof(U32) * indexInTileArray;

		// Write the lightIndex
		*((U32*)(m_tileBuffer + offset)) = lightIndex;
	}
};

//==============================================================================
Is::Is(Renderer* r)
:	RenderingPass(r), 
	m_sm(r)
{}

//==============================================================================
Is::~Is()
{}

//==============================================================================
Error Is::init(const ConfigSet& config)
{

	Error err = initInternal(config);
	
	if(err)
	{
		ANKI_LOGE("Failed to init IS");
	}

	return err;
}

//==============================================================================
Error Is::initInternal(const ConfigSet& config)
{
	Error err = ErrorCode::NONE;

	m_groundLightEnabled = config.get("is.groundLightEnabled");
	m_maxPointLights = config.get("is.maxPointLights");
	m_maxSpotLights = config.get("is.maxSpotLights");
	m_maxSpotTexLights = config.get("is.maxSpotTexLights");

	if(m_maxPointLights < 1 || m_maxSpotLights < 1 || m_maxSpotTexLights < 1)
	{
		ANKI_LOGE("Incorrect number of max lights");
		return ErrorCode::USER_DATA;
	}

	m_maxPointLightsPerTile = config.get("is.maxPointLightsPerTile");
	m_maxSpotLightsPerTile = config.get("is.maxSpotLightsPerTile");
	m_maxSpotTexLightsPerTile = config.get("is.maxSpotTexLightsPerTile");

	if(m_maxPointLightsPerTile < 1 
		|| m_maxSpotLightsPerTile < 1 
		|| m_maxSpotTexLightsPerTile < 1
		|| m_maxPointLightsPerTile % 4 != 0 
		|| m_maxSpotLightsPerTile % 4 != 0
		|| m_maxSpotTexLightsPerTile % 4 != 0)
	{
		ANKI_LOGE("Incorrect number of max lights per tile");
		return ErrorCode::USER_DATA;
	}

	m_tileSize = calcTileSize();

	//
	// Init the passes
	//
	err = m_sm.init(config);
	if(err) return err;

	//
	// Load the programs
	//
	String pps;
	String::ScopeDestroyer ppsd(&pps, getAllocator());

	err = pps.sprintf(
		getAllocator(),
		"\n#define TILES_X_COUNT %u\n"
		"#define TILES_Y_COUNT %u\n"
		"#define TILES_COUNT %u\n" 
		"#define RENDERER_WIDTH %u\n"
		"#define RENDERER_HEIGHT %u\n"
		"#define MAX_POINT_LIGHTS_PER_TILE %u\n"
		"#define MAX_SPOT_LIGHTS_PER_TILE %u\n"
		"#define MAX_SPOT_TEX_LIGHTS_PER_TILE %u\n" 
		"#define MAX_POINT_LIGHTS %u\n"
		"#define MAX_SPOT_LIGHTS %u\n"
		"#define MAX_SPOT_TEX_LIGHTS %u\n"
		"#define GROUND_LIGHT %u\n"
		"#define TILES_BLOCK_BINDING %u\n"
		"#define POISSON %u\n",
		m_r->getTilesCount().x(),
		m_r->getTilesCount().y(),
		(m_r->getTilesCount().x() * m_r->getTilesCount().y()),
		m_r->getWidth(),
		m_r->getHeight(),
		m_maxPointLightsPerTile,
		m_maxSpotLightsPerTile,
		m_maxSpotTexLightsPerTile,
		m_maxPointLights,
		m_maxSpotLights,
		m_maxSpotTexLights,
		m_groundLightEnabled,
		TILES_BLOCK_BINDING,
		m_sm.getPoissonEnabled());
	if(err) return err;

	// point light
	GlCommandBufferHandle cmdBuff;
	err = cmdBuff.create(&getGlDevice()); // Job for initialization
	if(err) return err;

	err = m_lightVert.loadToCache(&getResourceManager(),
		"shaders/IsLp.vert.glsl", pps.toCString(), "r_");
	if(err) return err;

	err = m_lightFrag.loadToCache(&getResourceManager(),
		"shaders/IsLp.frag.glsl", pps.toCString(), "r_");
	if(err) return err;

	err = m_lightPpline.create(cmdBuff, 
		{m_lightVert->getGlProgram(), m_lightFrag->getGlProgram()});
	if(err) return err;

	//
	// Create framebuffer
	//

	err = m_r->createRenderTarget(m_r->getWidth(), m_r->getHeight(), GL_RGB8,
			GL_RGB, GL_UNSIGNED_BYTE, 1, m_rt);
	if(err) return err;

	err = m_fb.create(cmdBuff, {{m_rt, GL_COLOR_ATTACHMENT0}});
	if(err) return err;

	//
	// Init the quad
	//
	static const F32 quadVertCoords[][2] = {{1.0, 1.0}, {0.0, 1.0},
		{1.0, 0.0}, {0.0, 0.0}};

	GlClientBufferHandle tempBuff;
	err = tempBuff.create(cmdBuff, sizeof(quadVertCoords), 
		(void*)&quadVertCoords[0][0]);
	if(err) return err;

	err = m_quadPositionsVertBuff.create(cmdBuff, GL_ARRAY_BUFFER, tempBuff, 0);
	if(err) return err;

	//
	// Create UBOs
	//
	const GLbitfield bufferBits = GL_DYNAMIC_STORAGE_BIT;

	err = m_commonBuff.create(cmdBuff, GL_UNIFORM_BUFFER, 
		sizeof(shader::CommonUniforms), bufferBits);
	if(err) return err;

	err = m_lightsBuff.create(cmdBuff, GL_SHADER_STORAGE_BUFFER, 
		calcLightsBufferSize(), bufferBits);
	if(err) return err;

	err = m_tilesBuff.create(cmdBuff, GL_SHADER_STORAGE_BUFFER,
		m_r->getTilesCount().x() * m_r->getTilesCount().y() * m_tileSize,
		bufferBits);
	if(err) return err;

	// Last thing to do
	cmdBuff.flush();

	return err;
}

//==============================================================================
Error Is::lightPass(GlCommandBufferHandle& cmdBuff)
{
	Error err = ErrorCode::NONE;
	Threadpool& threadPool = m_r->_getThreadpool();
	m_cam = &m_r->getSceneGraph().getActiveCamera();
	VisibilityTestResults& vi = m_cam->getVisibilityTestResults();

	//
	// Quickly get the lights
	//
	U visiblePointLightsCount = 0;
	U visibleSpotLightsCount = 0;
	U visibleSpotTexLightsCount = 0;
	Array<Light*, Sm::MAX_SHADOW_CASTERS> shadowCasters;

	auto it = vi.getLightsBegin();
	auto lend = vi.getLightsEnd();
	for(; it != lend; ++it)
	{
		Light* light = staticCastPtr<Light*>((*it).m_node);
		switch(light->getLightType())
		{
		case Light::Type::POINT:
			++visiblePointLightsCount;
			break;
		case Light::Type::SPOT:
			{
				SpotLight* slight = staticCastPtr<SpotLight*>(light);
				
				if(slight->getShadowEnabled())
				{
					shadowCasters[visibleSpotTexLightsCount++] = slight;
				}
				else
				{
					++visibleSpotLightsCount;
				}
				break;
			}
		default:
			ANKI_ASSERT(0);
			break;
		}
	}

	// Sanitize the counters
	clamp(visiblePointLightsCount, m_maxPointLights);
	clamp(visibleSpotLightsCount, m_maxSpotLights);
	clamp(visibleSpotTexLightsCount, m_maxSpotTexLights);

	U totalLightsCount = visiblePointLightsCount + visibleSpotLightsCount 
		+ visibleSpotTexLightsCount;

	ANKI_COUNTER_INC(RENDERER_LIGHTS_COUNT, U64(totalLightsCount));

	//
	// Do shadows pass
	//
	err = m_sm.run(&shadowCasters[0], visibleSpotTexLightsCount, cmdBuff);
	if(err) return err;

	//
	// Write the lights and tiles UBOs
	//
	U32 blockAlignment = 
		getGlDevice().getBufferOffsetAlignment(m_lightsBuff.getTarget());

	// Get the offsets and sizes of each uniform block
	PtrSize pointLightsOffset = 0;
	PtrSize pointLightsSize = getAlignedRoundUp(
		blockAlignment, sizeof(shader::PointLight) * visiblePointLightsCount);

	PtrSize spotLightsOffset = pointLightsSize;
	PtrSize spotLightsSize = getAlignedRoundUp(
		blockAlignment, sizeof(shader::SpotLight) * visibleSpotLightsCount);

	PtrSize spotTexLightsOffset = spotLightsOffset + spotLightsSize;
	PtrSize spotTexLightsSize = getAlignedRoundUp(blockAlignment, 
		sizeof(shader::SpotTexLight) * visibleSpotTexLightsCount);

	ANKI_ASSERT(
		spotTexLightsOffset + spotTexLightsSize <= calcLightsBufferSize());

	// Fire the super cmdBuff
	Array<WriteLightsJob, Threadpool::MAX_THREADS> tcmdBuff;

	GlClientBufferHandle lightsClientBuff;
	if(totalLightsCount > 0)
	{
		err = lightsClientBuff.create(
			cmdBuff, spotTexLightsOffset + spotTexLightsSize, nullptr);
		if(err) return err;
	}

	GlClientBufferHandle tilesClientBuff;
	err = tilesClientBuff.create(cmdBuff, m_tilesBuff.getSize(), nullptr);
	if(err) return err;

	AtomicU32 pointLightsAtomicCount(0);
	AtomicU32 spotLightsAtomicCount(0);
	AtomicU32 spotTexLightsAtomicCount(0);

	Array2d<AtomicU32, 
		ANKI_RENDERER_MAX_TILES_Y, 
		ANKI_RENDERER_MAX_TILES_X> 
		tilePointLightsCount,
		tileSpotLightsCount,
		tileSpotTexLightsCount;

	for(U y = 0; y < m_r->getTilesCount().y(); y++)
	{
		for(U x = 0; x < m_r->getTilesCount().x(); x++)
		{
			tilePointLightsCount[y][x].store(0);
			tileSpotLightsCount[y][x].store(0);
			tileSpotTexLightsCount[y][x].store(0);
		}
	}

	for(U i = 0; i < threadPool.getThreadsCount(); i++)
	{
		WriteLightsJob& job = tcmdBuff[i];

		if(i == 0)
		{
			if(totalLightsCount > 0)
			{
				job.m_pointLights = (shader::PointLight*)(
					(U8*)lightsClientBuff.getBaseAddress() 
					+ pointLightsOffset);
				job.m_spotLights = (shader::SpotLight*)(
					(U8*)lightsClientBuff.getBaseAddress() 
					+ spotLightsOffset);
				job.m_spotTexLights = (shader::SpotTexLight*)(
					(U8*)lightsClientBuff.getBaseAddress() 
					+ spotTexLightsOffset);
			}

			job.m_tileBuffer = (U8*)tilesClientBuff.getBaseAddress();

			job.m_lightsBegin = vi.getLightsBegin();
			job.m_lightsEnd = vi.getLightsEnd();

			job.m_pointLightsCount = &pointLightsAtomicCount;
			job.m_spotLightsCount = &spotLightsAtomicCount;
			job.m_spotTexLightsCount = &spotTexLightsAtomicCount;

			job.m_tilePointLightsCount = &tilePointLightsCount;
			job.m_tileSpotLightsCount = &tileSpotLightsCount;
			job.m_tileSpotTexLightsCount = &tileSpotTexLightsCount;

			job.m_tiler = &m_r->getTiler();
			job.m_is = this;
		}
		else
		{
			// Just copy from the first job. All cmdBuff have the same data

			job = tcmdBuff[0];
		}

		threadPool.assignNewTask(i, &job);
	}

	// In the meantime set the state
	setState(cmdBuff);

	// Sync
	err = threadPool.waitForAllThreadsToFinish();
	if(err) return err;

	// Write the light count for each tile
	for(U y = 0; y < m_r->getTilesCount().y(); y++)
	{
		for(U x = 0; x < m_r->getTilesCount().x(); x++)
		{
			UVec4* vec;

			vec = (UVec4*)(
				(U8*)tilesClientBuff.getBaseAddress() 
				+ (y * m_r->getTilesCount().x() + x) * m_tileSize);

			vec->x() = tilePointLightsCount[y][x].load();
			clamp(vec->x(), m_maxPointLightsPerTile);

			vec->y() = 0;

			vec->z() = tileSpotLightsCount[y][x].load();
			clamp(vec->z(), m_maxSpotLightsPerTile);

			vec->w() = tileSpotTexLightsCount[y][x].load();
			clamp(vec->w(), m_maxSpotTexLightsPerTile);
		}
	}

	// Write BOs
	if(totalLightsCount > 0)
	{
		m_lightsBuff.write(cmdBuff,
			lightsClientBuff, 0, 0, spotTexLightsOffset + spotTexLightsSize);
	}
	m_tilesBuff.write(cmdBuff, tilesClientBuff, 0, 0, 
		tilesClientBuff.getSize());

	//
	// Setup uniforms
	//

	// shader prog
	err = updateCommonBlock(cmdBuff);
	if(err) return err;

	m_commonBuff.bindShaderBuffer(cmdBuff, COMMON_UNIFORMS_BLOCK_BINDING);

	if(pointLightsSize > 0)
	{
		m_lightsBuff.bindShaderBuffer(cmdBuff, 
			pointLightsOffset, pointLightsSize, POINT_LIGHTS_BLOCK_BINDING);
	}

	if(spotLightsSize > 0)
	{
		m_lightsBuff.bindShaderBuffer(cmdBuff, 
			spotLightsOffset, spotLightsSize, SPOT_LIGHTS_BLOCK_BINDING);
	}

	if(spotTexLightsSize > 0)
	{
		m_lightsBuff.bindShaderBuffer(cmdBuff,
			spotTexLightsOffset, spotTexLightsSize, 
			SPOT_TEX_LIGHTS_BLOCK_BINDING);
	}

	m_tilesBuff.bindShaderBuffer(cmdBuff, TILES_BLOCK_BINDING); 

	// The binding points should much the shader
	Array<GlTextureHandle, 4> tarr = {{
		m_r->getMs()._getRt0(), 
		m_r->getMs()._getRt1(), 
		m_r->getMs()._getDepthRt(),
		m_sm.m_sm2DArrayTex}};

	cmdBuff.bindTextures(0, tarr.begin(), tarr.getSize());

	//
	// Draw
	//

	m_lightPpline.bind(cmdBuff);

	m_quadPositionsVertBuff.bindVertexBuffer(cmdBuff, 
		2, GL_FLOAT, false, 0, 0, 0);

	cmdBuff.drawArrays(GL_TRIANGLE_STRIP, 4, 
		m_r->getTilesCount().x() * m_r->getTilesCount().y());

	return err;
}

//==============================================================================
void Is::setState(GlCommandBufferHandle& cmdBuff)
{
	Bool drawToDefaultFbo = !m_r->getPps().getEnabled() 
		&& !m_r->getDbg().getEnabled() 
		&& !m_r->getIsOffscreen()
		&& m_r->getRenderingQuality() == 1.0;

	if(drawToDefaultFbo)
	{
		m_r->getDefaultFramebuffer().bind(cmdBuff, true);
		cmdBuff.setViewport(0, 0, 
			m_r->getDefaultFramebufferWidth(), 
			m_r->getDefaultFramebufferHeight());
	}
	else
	{
		m_fb.bind(cmdBuff, true);

		cmdBuff.setViewport(0, 0, m_r->getWidth(), m_r->getHeight());
	}
}

//==============================================================================
Error Is::run(GlCommandBufferHandle& cmdBuff)
{
	// Do the light pass including the shadow passes
	return lightPass(cmdBuff);
}

//==============================================================================
Error Is::updateCommonBlock(GlCommandBufferHandle& cmdBuff)
{
	SceneGraph& scene = m_r->getSceneGraph();

	GlClientBufferHandle cbuff;
	Error err = cbuff.create(cmdBuff, sizeof(shader::CommonUniforms), nullptr);
	if(err) return err;

	shader::CommonUniforms& blk = 
		*(shader::CommonUniforms*)cbuff.getBaseAddress();

	// Start writing
	blk.m_projectionParams = m_r->getProjectionParameters();

	blk.m_sceneAmbientColor = scene.getAmbientColor();

	Vec3 groundLightDir;
	if(m_groundLightEnabled)
	{
		blk.m_groundLightDir = 
			Vec4(-m_cam->getViewMatrix().getColumn(1).xyz(), 1.0);
	}

	m_commonBuff.write(cmdBuff, cbuff, 0, 0, cbuff.getSize());

	return err;
}

//==============================================================================
PtrSize Is::calcLightsBufferSize() const
{
	U32 buffAlignment = 
		getGlDevice().getBufferOffsetAlignment(GL_SHADER_STORAGE_BUFFER);
	PtrSize size;

	size = getAlignedRoundUp(
		buffAlignment,
		m_maxPointLights * sizeof(shader::PointLight));

	size += getAlignedRoundUp(
		buffAlignment,
		m_maxSpotLights * sizeof(shader::SpotLight));

	size += getAlignedRoundUp(
		buffAlignment,
		m_maxSpotTexLights * sizeof(shader::SpotTexLight));

	return size;
}

//==============================================================================
PtrSize Is::calcTileSize() const
{
	return 
		sizeof(U32) * 4 
		+ m_maxPointLightsPerTile * sizeof(U32)
		+ m_maxSpotLightsPerTile * sizeof(U32)
		+ m_maxSpotTexLightsPerTile * sizeof(U32);
}

} // end namespace anki
