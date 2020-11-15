// Copyright (C) 2009-2020, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#include <anki/importer/GltfImporter.h>
#include <anki/resource/ImageLoader.h>

namespace anki
{

const char* MATERIAL_TEMPLATE = R"(<!-- This file is auto generated by ImporterMaterial.cpp -->
<material shaderProgram="anki/shaders/GBufferGeneric.ankiprog">
	<mutation>
		<mutator name="DIFFUSE_TEX" value="%diffTexMutator%"/>
		<mutator name="SPECULAR_TEX" value="%specTexMutator%"/>
		<mutator name="ROUGHNESS_TEX" value="%roughnessTexMutator%"/>
		<mutator name="METAL_TEX" value="%metalTexMutator%"/>
		<mutator name="NORMAL_TEX" value="%normalTexMutator%"/>
		<mutator name="PARALLAX" value="%parallaxMutator%"/>
		<mutator name="EMISSIVE_TEX" value="%emissiveTexMutator%"/>
	</mutation>

	<inputs>
		%parallaxInput%

		%diff%
		%spec%
		%roughness%
		%metallic%
		%normal%
		%emission%
		%subsurface%
		%height%
	</inputs>
</material>
)";

const char* RT_MATERIAL_TEMPLATE = R"(
<rtMaterial>
	<rayType type="shadows" shaderProgram="anki/shaders/RtShadowsHit.ankiprog">
		<mutation>
			<mutator name="ALPHA_TEXTURE" value="0"/>
		</mutation>
	</rayType>
</rtMaterial>)";

static CString getTextureUri(const cgltf_texture_view& view)
{
	ANKI_ASSERT(view.texture);
	ANKI_ASSERT(view.texture->image);
	ANKI_ASSERT(view.texture->image->uri);
	return view.texture->image->uri;
}

/// Read the texture and find out if
static Error identifyMetallicRoughnessTexture(CString fname, F32& constantMetalines, F32& constantRoughness,
											  GenericMemoryPoolAllocator<U8>& alloc)
{
	ImageLoader iloader(alloc);
	ANKI_CHECK(iloader.load(fname));
	ANKI_ASSERT(iloader.getColorFormat() == ImageLoaderColorFormat::RGBA8);
	ANKI_ASSERT(iloader.getCompression() == ImageLoaderDataCompression::RAW);

	const U8Vec4* data = reinterpret_cast<const U8Vec4*>(&iloader.getSurface(0, 0, 0).m_data[0]);

	const F32 epsilon = 1.0f / 255.0f;
	for(U32 y = 0; y < iloader.getWidth(); ++y)
	{
		for(U32 x = 0; x < iloader.getHeight(); ++x)
		{
			const U8Vec4& pixel = *(data + y * iloader.getWidth() + x);
			const F32 m = F32(pixel.z()) / 255.0f;
			const F32 r = F32(pixel.y()) / 255.0f;

			if(x == 0 && y == 0)
			{
				// Initialize
				constantMetalines = m;
				constantRoughness = r;
			}
			else
			{
				if(constantMetalines < 0.0f || absolute(m - constantMetalines) > epsilon)
				{
					constantMetalines = -1.0f;
				}

				if(constantRoughness < 0.0f || absolute(r - constantRoughness) > epsilon)
				{
					constantRoughness = -1.0f;
				}
			}
		}
	}

	return Error::NONE;
}

Error GltfImporter::writeMaterial(const cgltf_material& mtl)
{
	StringAuto fname(m_alloc);
	fname.sprintf("%s%s.ankimtl", m_outDir.cstr(), mtl.name);
	ANKI_GLTF_LOGI("Importing material %s", fname.cstr());

	if(!mtl.has_pbr_metallic_roughness)
	{
		ANKI_GLTF_LOGE("Expecting PBR metallic roughness");
		return Error::USER_DATA;
	}

	HashMapAuto<CString, StringAuto> extras(m_alloc);
	ANKI_CHECK(getExtras(mtl.extras, extras));

	StringAuto xml(m_alloc);
	xml.append(XML_HEADER);
	xml.append("\n");
	xml.append(MATERIAL_TEMPLATE);
	xml.append(RT_MATERIAL_TEMPLATE);

	// Diffuse
	if(mtl.pbr_metallic_roughness.base_color_texture.texture)
	{
		StringAuto uri(m_alloc);
		uri.sprintf("%s%s", m_texrpath.cstr(), getTextureUri(mtl.pbr_metallic_roughness.base_color_texture).cstr());

		xml.replaceAll("%diff%",
					   StringAuto{m_alloc}.sprintf("<input shaderVar=\"u_diffTex\" value=\"%s\"/>", uri.cstr()));
		xml.replaceAll("%diffTexMutator%", "1");
	}
	else
	{
		const F32* diffCol = &mtl.pbr_metallic_roughness.base_color_factor[0];

		xml.replaceAll("%diff%", StringAuto{m_alloc}.sprintf("<input shaderVar=\"m_diffColor\" value=\"%f %f %f\"/>",
															 diffCol[0], diffCol[1], diffCol[2]));

		xml.replaceAll("%diffTexMutator%", "0");
	}

	// Specular color (freshnel)
	{
		Vec3 specular;
		auto it = extras.find("specular");
		if(it != extras.getEnd())
		{
			StringListAuto tokens(m_alloc);
			tokens.splitString(it->toCString(), ' ');
			if(tokens.getSize() != 3)
			{
				ANKI_GLTF_LOGE("Wrong specular: %s", it->cstr());
				return Error::USER_DATA;
			}

			auto token = tokens.getBegin();
			ANKI_CHECK(token->toNumber(specular.x()));
			++token;
			ANKI_CHECK(token->toNumber(specular.y()));
			++token;
			ANKI_CHECK(token->toNumber(specular.z()));
		}
		else
		{
			specular = Vec3(0.04f);
		}

		xml.replaceAll("%spec%", StringAuto{m_alloc}.sprintf("<input shaderVar=\"m_specColor\" value=\"%f %f %f\"/>",
															 specular.x(), specular.y(), specular.z()));

		xml.replaceAll("%specTexMutator%", "0");
	}

	// Identify metallic/roughness texture
	F32 constantMetaliness = -1.0f, constantRoughness = -1.0f;
	if(mtl.pbr_metallic_roughness.metallic_roughness_texture.texture)
	{
		const CString fname = getTextureUri(mtl.pbr_metallic_roughness.metallic_roughness_texture);

		ANKI_CHECK(identifyMetallicRoughnessTexture(fname, constantMetaliness, constantRoughness, m_alloc));
	}

	// Roughness
	if(mtl.pbr_metallic_roughness.metallic_roughness_texture.texture && constantRoughness < 0.0f)
	{
		StringAuto uri(m_alloc);
		uri.sprintf("%s%s", m_texrpath.cstr(),
					getTextureUri(mtl.pbr_metallic_roughness.metallic_roughness_texture).cstr());

		xml.replaceAll("%roughness%",
					   StringAuto{m_alloc}.sprintf("<input shaderVar=\"u_roughnessTex\" value=\"%s\"/>", uri.cstr()));

		xml.replaceAll("%roughnessTexMutator%", "1");
	}
	else
	{
		const F32 roughness = (constantRoughness >= 0.0f)
								  ? constantRoughness * mtl.pbr_metallic_roughness.roughness_factor
								  : mtl.pbr_metallic_roughness.roughness_factor;

		xml.replaceAll("%roughness%",
					   StringAuto{m_alloc}.sprintf("<input shaderVar=\"m_roughness\" value=\"%f\"/>", roughness));

		xml.replaceAll("%roughnessTexMutator%", "0");
	}

	// Metallic
	if(mtl.pbr_metallic_roughness.metallic_roughness_texture.texture && constantMetaliness < 0.0f)
	{
		StringAuto uri(m_alloc);
		uri.sprintf("%s%s", m_texrpath.cstr(),
					getTextureUri(mtl.pbr_metallic_roughness.metallic_roughness_texture).cstr());

		xml.replaceAll("%metallic%",
					   StringAuto{m_alloc}.sprintf("<input shaderVar=\"u_metallicTex\" value=\"%s\"/>", uri.cstr()));

		xml.replaceAll("%metalTexMutator%", "1");
	}
	else
	{
		const F32 metalines = (constantMetaliness >= 0.0f)
								  ? constantMetaliness * mtl.pbr_metallic_roughness.metallic_factor
								  : mtl.pbr_metallic_roughness.metallic_factor;

		xml.replaceAll("%metallic%",
					   StringAuto{m_alloc}.sprintf("<input shaderVar=\"m_metallic\" value=\"%f\"/>", metalines));

		xml.replaceAll("%metalTexMutator%", "0");
	}

	// Normal texture
	if(mtl.normal_texture.texture)
	{
		StringAuto uri(m_alloc);
		uri.sprintf("%s%s", m_texrpath.cstr(), getTextureUri(mtl.normal_texture).cstr());

		xml.replaceAll("%normal%",
					   StringAuto{m_alloc}.sprintf("<input shaderVar=\"u_normalTex\" value=\"%s\"/>", uri.cstr()));

		xml.replaceAll("%normalTexMutator%", "1");
	}
	else
	{
		xml.replaceAll("%normal%", "");
		xml.replaceAll("%normalTexMutator%", "0");
	}

	// Emissive texture
	if(mtl.emissive_texture.texture)
	{
		StringAuto uri(m_alloc);
		uri.sprintf("%s%s", m_texrpath.cstr(), getTextureUri(mtl.emissive_texture).cstr());

		xml.replaceAll("%emission%",
					   StringAuto{m_alloc}.sprintf("<input shaderVar=\"u_emissiveTex\" value=\"%s\"/>", uri.cstr()));

		xml.replaceAll("%emissiveTexMutator%", "1");
	}
	else
	{
		const F32* emissionCol = &mtl.emissive_factor[0];

		xml.replaceAll("%emission%", StringAuto{m_alloc}.sprintf("<input shaderVar=\"m_emission\" value=\"%f %f %f\"/>",
																 emissionCol[0], emissionCol[1], emissionCol[2]));

		xml.replaceAll("%emissiveTexMutator%", "0");
	}

	// Subsurface
	{
		F32 subsurface;
		auto it = extras.find("subsurface");
		if(it != extras.getEnd())
		{
			ANKI_CHECK(it->toNumber(subsurface));
		}
		else
		{
			subsurface = 0.0f;
		}

		xml.replaceAll("%subsurface%",
					   StringAuto{m_alloc}.sprintf("<input shaderVar=\"m_subsurface\" value=\"%f\"/>", subsurface));
	}

	// Height texture
	auto it = extras.find("height_map");
	if(it != extras.getEnd())
	{
		StringAuto uri(m_alloc);
		uri.sprintf("%s%s", m_texrpath.cstr(), it->cstr());

		xml.replaceAll("%height%",
					   StringAuto{m_alloc}.sprintf("<input shaderVar=\"u_heightTex\" value=\"%s\" \"/>\n"
												   "\t\t<input shaderVar=\"m_heightmapScale\" value=\"0.05\"/>",
												   uri.cstr()));

		xml.replaceAll("%parallaxMutator%", "1");
	}
	else
	{
		xml.replaceAll("%height%", "");
		xml.replaceAll("%parallaxInput%", "");
		xml.replaceAll("%parallaxMutator%", "0");
	}

	// Replace texture extensions with .anki
	xml.replaceAll(".tga", ".ankitex");
	xml.replaceAll(".png", ".ankitex");
	xml.replaceAll(".jpg", ".ankitex");
	xml.replaceAll(".jpeg", ".ankitex");

	// Write file
	File file;
	ANKI_CHECK(file.open(fname.toCString(), FileOpenFlag::WRITE));
	ANKI_CHECK(file.writeText("%s", xml.cstr()));

	return Error::NONE;
}

} // end namespace anki