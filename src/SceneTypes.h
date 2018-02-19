#pragma once
#include "BasicTypes.h"

// <0 means an invalid index
#if defined(_WIN32) || defined(_WIN64)
	typedef unsigned __int32 Index;
#else
	#include <stdint.h>
	typedef uint32_t Index;
#endif

/** Maya uses strings to identify color and texture-coordinate sets. We use indices */
typedef int SetIndex;

typedef Float3 Position;
typedef Float3 Normal;
typedef Float2 TexCoord;
typedef Float4 Tangent;
typedef Float4 Color;

typedef std::vector<Position> PositionVector;
typedef std::vector<Normal> NormalVector;
typedef std::vector<TexCoord> TexCoordVector;
typedef std::vector<Tangent> TangentVector;
typedef std::vector<Color> ColorVector;
typedef std::vector<Index> IndexVector;

typedef gsl::span<Index> IndexSpan;

/** Index of a Maya shape instance */
typedef int InstanceIndex;

/** Index of a Maya material */
typedef int ShaderIndex;

typedef size_t Hash;

enum PrimitiveKind
{
	// TODO: Support other primitives
	TRIANGLE_LIST
};

enum Limitations
{
	MAX_TEXTURES_PER_VERTEX = 4,
	MAX_SKIN_INFLUENCES_PER_VERTEX = 4,
};

namespace Semantic
{
	enum Kind
	{
		INVALID = -1,
		POSITION,
		NORMAL,
		COLOR,
		TEXCOORD,
		TANGENT,
		COUNT,
		MORPH_COUNT = 2, // POSITION & NORMAL only currently
	};

	inline Kind from(int s)
	{
		assert(s >= 0 && s < COUNT);
		return static_cast<Kind>(s);
	}

	// Get the number of float components per semantic 
	inline int dimension(const Kind s)
	{
		switch (s)
		{
		case POSITION:	return array_size<Position>::size;
		case NORMAL:	return array_size<Normal>::size;
		case COLOR:		return array_size<Color>::size;
		case TEXCOORD:	return array_size<TexCoord>::size;
		case TANGENT:	return array_size<Tangent>::size;
		default: assert(false); return 0;
		}
	}

	inline const char* name(const Kind s)
	{
		switch (s)
		{
		case POSITION:	return "POSITION";
		case NORMAL:	return "NORMAL";
		case COLOR:		return "COLOR";
		case TEXCOORD:	return "TEXCOORD";
		case TANGENT:	return "TANGENT";
		default: assert(false); return "UNKNOWN";
		}
	}

	template<typename T>
	size_t totalSetCount(const T& table) 
	{
		size_t count = 0;

		for (int semanticIndex = 0; semanticIndex < Semantic::COUNT; ++semanticIndex)
		{
			count += table.at(semanticIndex).size();
		}

		return count;
	}
}

