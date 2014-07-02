// Copyright (C) 2014, Panagiotis Christopoulos Charitos.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#include "anki/renderer/Dbg.h"
#include "anki/renderer/Renderer.h"
#include "anki/resource/ProgramResource.h"
#include "anki/scene/SceneGraph.h"
#include "anki/scene/Camera.h"
#include "anki/scene/Light.h"
#include "anki/core/Logger.h"

namespace anki {

//==============================================================================
Dbg::~Dbg()
{}

//==============================================================================
void Dbg::init(const ConfigSet& initializer)
{
	m_enabled = initializer.get("dbg.enabled");
	enableBits(DF_ALL);

	try
	{
		GlManager& gl = GlManagerSingleton::get();
		GlJobChainHandle jobs(&gl);

		// Chose the correct color FAI
		if(m_r->getPps().getEnabled())
		{
			m_fb = GlFramebufferHandle(jobs, 
				{{m_r->getPps()._getRt(), GL_COLOR_ATTACHMENT0},
				{m_r->getMs()._getDepthRt(), GL_DEPTH_ATTACHMENT}});
		}
		else
		{
			m_fb = GlFramebufferHandle(jobs, 
				{{m_r->getIs()._getRt(), GL_COLOR_ATTACHMENT0},
				{m_r->getMs()._getDepthRt(), GL_DEPTH_ATTACHMENT}});
		}

		m_drawer.reset(new DebugDrawer);
		m_sceneDrawer.reset(new SceneDebugDrawer(m_drawer.get()));

		jobs.finish();
	}
	catch(std::exception& e)
	{
		throw ANKI_EXCEPTION("Cannot create debug FBO") << e;
	}
}

//==============================================================================
void Dbg::run(GlJobChainHandle& jobs)
{
	ANKI_ASSERT(m_enabled);

	SceneGraph& scene = m_r->getSceneGraph();

	m_fb.bind(jobs, false);
	jobs.enableDepthTest(m_depthTest);

	Camera& cam = scene.getActiveCamera();
	m_drawer->prepareDraw(jobs);
	m_drawer->setViewProjectionMatrix(cam.getViewProjectionMatrix());
	m_drawer->setModelMatrix(Mat4::getIdentity());
	//drawer->drawGrid();

	scene.iterateSceneNodes([&](SceneNode& node)
	{
		SpatialComponent* sp = node.tryGetComponent<SpatialComponent>();

		if(&cam.getComponent<SpatialComponent>() == sp)
		{
			return;
		}

		if(bitsEnabled(DF_SPATIAL) && sp)
		{
			m_sceneDrawer->draw(node);
		}
	});

	// Draw sectors
	for(const Sector* sector : scene.getSectorGroup().getSectors())
	{
		//if(sector->isVisible())
		{
			if(bitsEnabled(DF_SECTOR))
			{
				m_sceneDrawer->draw(*sector);
			}
		}
	}

	// Physics
	if(bitsEnabled(DF_PHYSICS))
	{
		//scene.getPhysics().debugDraw();
	}

	// XXX
#if 0
	if(0)
	{
		Vec3 tri[3] = {
			Vec3{10.0 , 7.0, -2.0}, 
			Vec3{-10.0 , 7.0, -2.0}, 
			Vec3{-10.0 , 0.0, -2.0}};

		Mat4 mvp = scene.getActiveCamera().getViewProjectionMatrix();

		Array<Vec4, 3> projtri = {{
			mvp * Vec4(tri[0], 1.0), 
			mvp * Vec4(tri[1], 1.0), 
			mvp * Vec4(tri[2], 1.0)}};

		Array<Vec4, 3> cliptri;

		for(int i = 0; i < 3; i++)
		{
			Vec4 proj = projtri[i];
			cliptri[i] = proj / fabs(proj.w());
		}

		Vec3 a = cliptri[1].xyz() - cliptri[0].xyz();
		Vec3 b = cliptri[2].xyz() - cliptri[1].xyz();
		//Vec2 c = a.xy() * b.yx();
		//Bool frontfacing = (c.x() - c.y()) > 0.0;
		Vec3 cross = a.cross(b);
		Bool frontfacing = cross.z() >= 0.0;

		if(frontfacing)
		{
			drawer->setColor(Vec4(1.0));
			drawer->setViewProjectionMatrix(
				scene.getActiveCamera().getViewProjectionMatrix());
			drawer->setModelMatrix(Mat4::getIdentity());

			drawer->pushBackVertex(tri[0]);
			drawer->pushBackVertex(tri[1]);
			drawer->pushBackVertex(tri[1]);
			drawer->pushBackVertex(tri[2]);
			drawer->pushBackVertex(tri[2]);
			drawer->pushBackVertex(tri[0]);
		}

		{
			for(Vec4& v : cliptri)
			{
				std::cout << v.toString() << "\n";
			}

			std::cout << std::endl;

			std::cout << a.toString() << ", " << b.toString() 
				//<< ", " << c.toString() << ", " 
				//<< (c.x() - c.y()) 
				<< ", ff:" << (U)frontfacing
				<< std::endl;

			std::cout << std::endl;
		}

		{
			drawer->setColor(Vec4(0.0, 1.0, 0.0, 1.0));
			drawer->setViewProjectionMatrix(Mat4::getIdentity());
			drawer->setModelMatrix(Mat4::getIdentity());

			F32 z = 0.0;

			drawer->pushBackVertex(Vec3(cliptri[0].xy(), z));
			drawer->pushBackVertex(Vec3(cliptri[1].xy(), z));
			drawer->pushBackVertex(Vec3(cliptri[1].xy(), z));
			drawer->pushBackVertex(Vec3(cliptri[2].xy(), z));
		}
	}
	// XXX
#endif

#if 1
	{
		Vec4 pos0 = scene.findSceneNode("shape0").
			getComponent<MoveComponent>().getWorldTransform().getOrigin();

		Vec4 pos1 = scene.findSceneNode("shape1").
			getComponent<MoveComponent>().getWorldTransform().getOrigin();
		Mat3x4 rot1 = scene.findSceneNode("shape1").
			getComponent<MoveComponent>().getWorldTransform().getRotation();


		Aabb s0(pos0 - Vec4(1.0, 1.0, 2.0, 0.0), pos0 + Vec4(1.0, 1.0, 2.0, 0.0));
		Obb s1(pos1, rot1, Vec4(1.0, 0.5, 2.5, 0.0));

		CollisionDebugDrawer dr(m_drawer.get());

		GjkEpa gjk;
		ContactPoint cp;

		Bool intersect = gjk.intersect(s0, s1, cp);


		if(intersect)
		{
			m_drawer->setColor(Vec4(1.0, 0.0, 0.0, 1.0));
		}
		else
		{
			m_drawer->setColor(Vec4(1.0, 1.0, 1.0, 1.0));
		}

		s0.accept(dr);
		s1.accept(dr);

		m_drawer->setModelMatrix(Mat4::getIdentity());
		m_drawer->setColor(Vec4(0.0, 1.0, 0.0, 1.0));
		m_drawer->begin();
		m_drawer->pushBackVertex(Vec3(0.0));
		m_drawer->pushBackVertex(cp.m_normal.xyz() * cp.m_depth);
		//m_drawer->pushBackVertex(cp.m_normal.xyz());
		//m_drawer->pushBackVertex(gjk.m_faces[gjk.m_faceCount - 3].m_normal.xyz());
		m_drawer->end();

		{
			m_drawer->setModelMatrix(Mat4::getIdentity());
			m_drawer->setColor(Vec4(1.0));

			m_drawer->begin();
			m_drawer->pushBackVertex(Vec3(-10, 0, 0));
			m_drawer->pushBackVertex(Vec3(10, 0, 0));
			m_drawer->pushBackVertex(Vec3(0, 0, -10));
			m_drawer->pushBackVertex(Vec3(0, 0, 10));
			m_drawer->end();
		}

		if(0)
		{
			m_drawer->setColor(Vec4(.0, 1.0, 1.0, 1.0));
			m_drawer->pushBackVertex(Vec3(0, 0, 0));
			m_drawer->pushBackVertex(gjk.m_simplexArr[0].m_v.xyz());
			std::cout << gjk.m_simplexArr[0].m_v.xyz().toString() << std::endl;
		}

		if(0)
		{
			m_drawer->setColor(Vec4(0.0, 1.0, 1.0, 1.0));

			Vec4 n = Vec4(0.294484, 0.239468, 0.925074, 0.000000);
			Vec4 p0 = Vec4(0.777393, 0.538571, 0.068147, 0);

			Vec4 p = Vec4(-0.296335, -0.886740, 1.415587, 0.0);

			m_drawer->pushBackVertex(Vec3(0, 0, 0));
			m_drawer->pushBackVertex(p.xyz());

			p = p - n.dot(p - p0) * n;
			m_drawer->pushBackVertex(Vec3(0, 0, 0));
			m_drawer->pushBackVertex(p.xyz());
		}

		if(0)
		{
			Mat4 m(Vec4(0.0, 0.0, 0.0, 1.0), Mat3::getIdentity(), 1.0);
			m_drawer->setModelMatrix(m);
			m_drawer->setColor(Vec4(1.0, 0.0, 1.0, 1.0));
			m_drawer->begin();
			Array<U, 12> idx = {{0, 1, 2, 1, 2, 3, 2, 3, 0, 3, 0, 1}};
			for(U i = 0; i < idx.size(); i += 3)
			{
				m_drawer->pushBackVertex(gjk.m_simplexArr[idx[i + 0]].m_v.xyz());
				m_drawer->pushBackVertex(gjk.m_simplexArr[idx[i + 1]].m_v.xyz());
				m_drawer->pushBackVertex(gjk.m_simplexArr[idx[i + 1]].m_v.xyz());
				m_drawer->pushBackVertex(gjk.m_simplexArr[idx[i + 2]].m_v.xyz());
				m_drawer->pushBackVertex(gjk.m_simplexArr[idx[i + 2]].m_v.xyz());
				m_drawer->pushBackVertex(gjk.m_simplexArr[idx[i + 0]].m_v.xyz());
			}
			m_drawer->end();
		}

		if(0)
		{
			Mat4 m(Vec4(0.0, 0.0, 0.0, 1.0), Mat3::getIdentity(), 1.02);
			m_drawer->setModelMatrix(m);
			m_drawer->setColor(Vec4(1.0, 1.0, 0.0, 1.0));
			m_drawer->begin();
			for(U i = 0; i < gjk.m_count - 1; i++)
			{
				m_drawer->pushBackVertex(gjk.m_simplexArr[i].m_v.xyz());
				m_drawer->pushBackVertex(gjk.m_simplexArr[i + 1].m_v.xyz());
			}
			m_drawer->end();
		}

		if(1)
		{
			Mat4 m(Vec4(0.0, 0.0, 0.0, 1.0), Mat3::getIdentity(), 1.01);
			m_drawer->setModelMatrix(m);
			m_drawer->setColor(Vec4(1.0, 0.0, 1.0, 1.0));
			m_drawer->begin();

			static U64 count = 0;

			++count;
			U offset = (count / 80) % gjk.m_faceCount;
			//for(U i = 0; i < 1; i++)
			for(U i = 0; i < gjk.m_faceCount; i++)
			{
				//auto idx = gjk.m_faces[offset].m_idx;
				auto idx = gjk.m_faces[i].m_idx;

				if(i % 2)
				{
					m = Mat4(Vec4(0.0, 0.0, 0.0, 1.0), Mat3::getIdentity(), 1.01);
				}
				else
				{
					m = Mat4(Vec4(0.0, 0.0, 0.0, 1.0), Mat3::getIdentity(), 1.0);
				}
				m_drawer->setModelMatrix(m);

				m_drawer->setColor(Vec4(1.0, 0.0, 1.0, 1.0) 
					* Vec4(F32(i + 1) / gjk.m_faceCount));

				#define WHAT(i_) gjk.m_simplexArr[idx[i_]].m_v.xyz()

				m_drawer->pushBackVertex(WHAT(0));
				m_drawer->pushBackVertex(WHAT(1));
				m_drawer->pushBackVertex(WHAT(1));
				m_drawer->pushBackVertex(WHAT(2));
				m_drawer->pushBackVertex(WHAT(2));
				m_drawer->pushBackVertex(WHAT(0));

				#undef WHAT
			}

			m_drawer->end();
		}


		/*m_drawer->setColor(Vec4(0.0, 1.0, 0.0, 1.0));
		m_drawer->setModelMatrix(Mat4::getIdentity());
		m_drawer->begin();
		m_drawer->pushBackVertex(gjk.m_a.xyz());
		m_drawer->pushBackVertex(gjk.m_b.xyz());
		m_drawer->pushBackVertex(gjk.m_b.xyz());
		m_drawer->pushBackVertex(gjk.m_c.xyz());
		m_drawer->end();*/
	}
#endif

	m_drawer->flush();
	m_drawer->finishDraw();

	jobs.enableDepthTest(false);
}

} // end namespace anki
