// Copyright (C) 2009-2020, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#include <anki/scene/PhysicsDebugNode.h>
#include <anki/scene/components/RenderComponent.h>
#include <anki/scene/components/SpatialComponent.h>
#include <anki/scene/SceneGraph.h>
#include <anki/resource/ResourceManager.h>

namespace anki
{

PhysicsDebugNode::~PhysicsDebugNode()
{
}

Error PhysicsDebugNode::init()
{
	ANKI_CHECK(m_dbgDrawer.init(&getSceneGraph().getResourceManager()));

	RenderComponent* rcomp = newComponent<RenderComponent>();
	rcomp->setFlags(RenderComponentFlag::NONE);
	rcomp->initRaster([](RenderQueueDrawContext& ctx,
						 ConstWeakArray<void*> userData) { static_cast<PhysicsDebugNode*>(userData[0])->draw(ctx); },
					  this, 0);

	SpatialComponent* scomp = newComponent<SpatialComponent>();
	scomp->setUpdateOctreeBounds(false); // Don't mess with the bounds
	scomp->setAabbWorldSpace(Aabb(getSceneGraph().getSceneMax(), getSceneGraph().getSceneMin()));
	scomp->setSpatialOrigin(Vec3(0.0f));

	return Error::NONE;
}

void PhysicsDebugNode::draw(RenderQueueDrawContext& ctx)
{
	if(ctx.m_debugDraw)
	{
		m_dbgDrawer.prepareFrame(&ctx);
		m_dbgDrawer.setViewProjectionMatrix(ctx.m_viewProjectionMatrix);
		m_dbgDrawer.setModelMatrix(Mat4::getIdentity());
		m_physDbgDrawer.drawWorld(getSceneGraph().getPhysicsWorld());
		m_dbgDrawer.finishFrame();
	}
}

} // end namespace anki
