// Copyright (C) 2009-2015, Panagiotis Christopoulos Charitos.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#ifndef ANKI_GL_COMMON_H
#define ANKI_GL_COMMON_H

#include "anki/Config.h"
#include "anki/util/Allocator.h"
#include "anki/util/NonCopyable.h"
#include "anki/util/Assert.h"
#include "anki/util/Array.h"
#include "anki/util/Enum.h"
#include "anki/Math.h"

#if ANKI_GL == ANKI_GL_DESKTOP
#	if ANKI_OS == ANKI_OS_WINDOWS && !defined(GLEW_STATIC)
#		define GLEW_STATIC
#	endif
#	include <GL/glew.h>
#	if !defined(ANKI_GLEW_H)
#		error "Wrong GLEW included"
#	endif
#elif ANKI_GL == ANKI_GL_ES
#	include <GLES3/gl3.h>
#else
#	error "See file"
#endif

#define ANKI_QUEUE_DISABLE_ASYNC 0

namespace anki {

/// @addtogroup opengl_private
/// @{

/// The type of the allocator of GlCommandBuffer
template<typename T>
using GlCommandBufferAllocator = ChainAllocator<T>;

/// The type of the allocator for heap allocations
template<typename T>
using GlAllocator = HeapAllocator<T>;
/// @}

/// @addtogroup opengl_containers
/// @{

/// Attachment load.
enum class GlAttachmentLoadOperation: U8
{
	LOAD,
	CLEAR,
	DONT_CARE
};

/// Attachment store.
enum class GlAttachmentStoreOperation: U8
{
	STORE,
	DONT_CARE
};

/// Texture filtering method
enum class GlTextureFilter: U8
{
	NEAREST,
	LINEAR,
	TRILINEAR
};

/// Shader type
enum class ShaderType: U8
{
	VERTEX,
	TESSELLATION_CONTROL,
	TESSELLATION_EVALUATION,
	GEOMETRY,
	FRAGMENT,
	COMPUTE,
	COUNT
};
ANKI_ENUM_ALLOW_NUMERIC_OPERATIONS(ShaderType, inline)

/// Shader variable type.
enum class ShaderVariableDataType: U8
{
	NONE,
	FLOAT,
	VEC2,
	VEC3,
	VEC4,
	MAT3,
	MAT4,
	SAMPLER_2D,
	SAMPLER_3D,
	SAMPLER_2D_ARRAY,
	SAMPLER_CUBE,

	NUMERICS_FIRST = FLOAT,
	NUMERICS_LAST = MAT4,

	SAMPLERS_FIRST = SAMPLER_2D,
	SAMPLERS_LAST = SAMPLER_CUBE
};
ANKI_ENUM_ALLOW_NUMERIC_OPERATIONS(ShaderVariableDataType, inline)

/// Using an AnKi typename get the ShaderVariableDataType. Used for debugging.
template<typename T>
ShaderVariableDataType getShaderVariableTypeFromTypename();

#define ANKI_SPECIALIZE_SHADER_VAR_TYPE_GET(typename_, type_) \
	template<> \
	inline ShaderVariableDataType \
		getShaderVariableTypeFromTypename<typename_>() { \
		return ShaderVariableDataType::type_; \
	}

ANKI_SPECIALIZE_SHADER_VAR_TYPE_GET(F32, FLOAT)
ANKI_SPECIALIZE_SHADER_VAR_TYPE_GET(Vec2, VEC2)
ANKI_SPECIALIZE_SHADER_VAR_TYPE_GET(Vec3, VEC3)
ANKI_SPECIALIZE_SHADER_VAR_TYPE_GET(Vec4, VEC4)
ANKI_SPECIALIZE_SHADER_VAR_TYPE_GET(Mat3, MAT3)
ANKI_SPECIALIZE_SHADER_VAR_TYPE_GET(Mat4, MAT4)

#undef ANKI_SPECIALIZE_SHADER_VAR_TYPE_GET

/// Split the initializer for re-using parts of it
class GlTextureInitializerBase
{
public:
	U32 m_width = 0;
	U32 m_height = 0;
	U32 m_depth = 0; ///< Relevant only for 3D and 2DArray textures
	GLenum m_target = GL_TEXTURE_2D;
	GLenum m_internalFormat = GL_NONE;
	U32 m_mipmapsCount = 0;
	GlTextureFilter m_filterType = GlTextureFilter::NEAREST;
	Bool8 m_repeat = false;
	I32 m_anisotropyLevel = 0;
	U32 m_samples = 1;
};

/// Shader block information.
struct ShaderVariableBlockInfo
{
	I16 m_offset = -1; ///< Offset inside the block

	I16 m_arraySize = -1; ///< Number of elements.

	/// Stride between the each array element if the variable is array.
	I16 m_arrayStride = -1;

	/// Identifying the stride between columns of a column-major matrix or rows
	/// of a row-major matrix.
	I16 m_matrixStride = -1;
};

/// Populate the memory of a variable that is inside a shader block.
void writeShaderBlockMemory(
	ShaderVariableDataType type,
	const ShaderVariableBlockInfo& varBlkInfo,
	const void* elements, 
	U32 elementsCount,
	void* buffBegin,
	const void* buffEnd);
/// @}

/// @addtogroup opengl_other
/// @{

/// The draw indirect structure for index drawing, also the parameters of a 
/// regular drawcall
class GlDrawElementsIndirectInfo
{
public:
	GlDrawElementsIndirectInfo()
	{}

	GlDrawElementsIndirectInfo(
		U32 count, 
		U32 instanceCount, 
		U32 firstIndex, 
		U32 baseVertex, 
		U32 baseInstance)
	:	m_count(count), 
		m_instanceCount(instanceCount), 
		m_firstIndex(firstIndex), 
		m_baseVertex(baseVertex),
		m_baseInstance(baseInstance)
	{}

	U32 m_count = MAX_U32;
	U32 m_instanceCount = 1;
	U32 m_firstIndex = 0;
	U32 m_baseVertex = 0;
	U32 m_baseInstance = 0;
};

/// The draw indirect structure for arrays drawing, also the parameters of a 
/// regular drawcall
class GlDrawArraysIndirectInfo
{
public:
	GlDrawArraysIndirectInfo()
	{}

	GlDrawArraysIndirectInfo(
		U32 count, 
		U32 instanceCount, 
		U32 first, 
		U32 baseInstance)
	:	m_count(count), 
		m_instanceCount(instanceCount), 
		m_first(first), 
		m_baseInstance(baseInstance)
	{}

	U32 m_count = MAX_U32;
	U32 m_instanceCount = 1;
	U32 m_first = 0;
	U32 m_baseInstance = 0;	
};

/// A function that returns an index from a shader type GLenum
ShaderType computeShaderTypeIndex(const GLenum glType);

/// A function that returns a GLenum from an index
GLenum computeGlShaderType(const ShaderType idx, GLbitfield* bit = nullptr);

/// GL generic callback
using GlCallback = void(*)(void*);
using GlMakeCurrentCallback = void(*)(void*, void*);

/// Occlusion query result bit.
enum class GlOcclusionQueryResultBit: U8
{
	NOT_AVAILABLE = 1 << 0,
	VISIBLE = 1 << 1,
	NOT_VISIBLE = 1 << 2 
};
ANKI_ENUM_ALLOW_NUMERIC_OPERATIONS(GlOcclusionQueryResultBit, inline)

/// @}

} // end namespace anki

#endif
