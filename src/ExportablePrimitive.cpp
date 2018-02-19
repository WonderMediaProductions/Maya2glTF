#include "externals.h"
#include "MeshRenderables.h"
#include "ExportablePrimitive.h"
#include "ExportableResources.h"
#include "ExportableMaterial.h"
#include "Spans.h"
#include "Arguments.h"

using namespace GLTF::Constants;

namespace Semantic
{
	inline std::string glTFattributeName(const Kind s, const int setIndex)
	{
		// NOTE: Although Maya has multiple tangent sets, glTW only accepts one. 
		// Need to dig deeper to understand this correctly.
		switch (s)
		{
		case POSITION:	return std::string("POSITION");
		case NORMAL:	return std::string("NORMAL");
		case TANGENT:	return std::string("TANGENT");
		case COLOR:		return std::string("COLOR_") + std::to_string(setIndex);
		case TEXCOORD:	return std::string("TEXCOORD_") + std::to_string(setIndex);
		default: assert(false); return "UNKNOWN";
		}
	}
}

ExportablePrimitive::ExportablePrimitive(
	const VertexBuffer& vertexBuffer,
	ExportableResources& resources)
{
	glPrimitive.mode = GLTF::Primitive::TRIANGLES;

	auto& vertexIndices = vertexBuffer.indices;

	if (resources.arguments().force32bitIndices ||
		vertexBuffer.maxIndex() > std::numeric_limits<uint16>::max())
	{
		// Use 32-bit indices
		auto spanIndices = reinterpret_span<uint8>(span(vertexIndices));
		m_data.insert(m_data.end(), spanIndices.begin(), spanIndices.end());

		glIndices = std::make_unique<GLTF::Accessor>(
			GLTF::Accessor::Type::SCALAR, WebGL::UNSIGNED_INT,
			&m_data[0], static_cast<int>(vertexIndices.size()),
			WebGL::ELEMENT_ARRAY_BUFFER);

		glPrimitive.indices = glIndices.get();
	}
	else
	{
		// Use 16-bit indices
		std::vector<uint16> shortIndices(vertexIndices.size());
		std::copy(vertexIndices.begin(), vertexIndices.end(), shortIndices.begin());

		auto spanIndices = reinterpret_span<uint8>(span(shortIndices));
		m_data.insert(m_data.end(), spanIndices.begin(), spanIndices.end());


		glIndices = std::make_unique<GLTF::Accessor>(
			GLTF::Accessor::Type::SCALAR, WebGL::UNSIGNED_SHORT,
			&m_data[0], static_cast<int>(vertexIndices.size()),
			WebGL::ELEMENT_ARRAY_BUFFER);

		glPrimitive.indices = glIndices.get();
	}

	// Extract main shape vertices
	// TODO: Derived blend shape deltas!
	for (auto && pair: vertexBuffer.componentsMap)
	{
		const auto& slot = pair.first;
		if (slot.shapeIndex == 0)
		{
			const auto& components = pair.second;
			const int offset = static_cast<int>(m_data.size());
			const int count = static_cast<int>(components.size() / slot.dimension());
			auto spanComponents = reinterpret_span<uint8>(span(components));
			m_data.insert(m_data.end(), spanComponents.begin(), spanComponents.end());

			auto accessor = createAccessor(slot.semantic, offset, count);
			glPrimitive.attributes[glTFattributeName(slot.semantic, slot.setIndex)] = accessor.get();
			glAccessorTable[slot.semantic].emplace_back(move(accessor));
		}
	}
}

ExportablePrimitive::~ExportablePrimitive()
{
}

std::unique_ptr<GLTF::Accessor> ExportablePrimitive::createAccessor(const Semantic::Kind semantic, const int offset, const int count)
{
	auto data = &m_data[offset];

	GLTF::Accessor::Type type;

	switch (semantic)
	{
	case Semantic::POSITION:
		type = GLTF::Accessor::Type::VEC3;
		break;
	case Semantic::NORMAL:
		type = GLTF::Accessor::Type::VEC3;
		break;
	case Semantic::TEXCOORD:
		type = GLTF::Accessor::Type::VEC2;
		break;
	case Semantic::TANGENT:
		// TODO: For exporting morph targets, use VEC3
		type = GLTF::Accessor::Type::VEC4;
		break;
	case Semantic::COLOR:
		type = GLTF::Accessor::Type::VEC4;
		break;
	default:
		assert(false);
		return nullptr;
	}

	return std::make_unique<GLTF::Accessor>(type, WebGL::FLOAT, data, count, WebGL::ARRAY_BUFFER);
}

