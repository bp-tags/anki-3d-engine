// Copyright (C) 2009-2020, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#pragma once

#include <anki/scene/SceneNode.h>
#include <anki/resource/Forward.h>
#include <anki/physics/Forward.h>

namespace anki
{

/// @addtogroup scene
/// @{

/// Node that interacts with physics.
class StaticCollisionNode : public SceneNode
{
public:
	StaticCollisionNode(SceneGraph* scene, CString name);

	~StaticCollisionNode();

	ANKI_USE_RESULT Error init();
};
/// @}

} // end namespace anki
