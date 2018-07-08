#include "externals.h"
#include "ExportableMaterial.h"
#include "ExportableTexture.h"
#include "DagHelper.h"
#include "MayaException.h"
#include "ExportableResources.h"
#include "Arguments.h"

using namespace std::experimental::filesystem;

ExportableMaterial::ExportableMaterial() = default;
ExportableMaterial::~ExportableMaterial() = default;

std::unique_ptr<ExportableMaterial> ExportableMaterial::from(ExportableResources& resources, const MFnDependencyNode& shaderNode)
{
	if (shaderNode.typeName() == "GLSLShader" || !resources.arguments().skipStandardMaterials)
		return std::make_unique<ExportableMaterialPBR>(resources, shaderNode);

	cout << prefix
		<< "Skipping non-PBR shader node type " << std::quoted(shaderNode.typeName().asChar()) << " #" << shaderNode.type()
		<< endl;

	return nullptr;
}

bool ExportableMaterial::tryCreateNormalTexture(
	ExportableResources& resources,
	const MObject& shaderObject,
	float& normalScale,
	GLTF::Texture*& outputTexture)
{
	MObject normalCamera = DagHelper::findSourceNodeConnectedTo(shaderObject, "normalCamera");
	if (normalCamera.isNull())
		return false;

	if (!getScalar(normalCamera, "bumpDepth", normalScale))
		return false;

	const ExportableTexture bumpTexture(resources, normalCamera, "bumpValue");
	if (!outputTexture)
		return false;

	outputTexture = bumpTexture;
	return true;
}

bool ExportableMaterial::getScalar(const MObject& obj, const char* attributeName, float& scalar)
{
	return DagHelper::getPlugValue(obj, attributeName, scalar);
}

bool ExportableMaterial::getString(const MObject& obj, const char* attributeName, MString& string)
{
	return DagHelper::getPlugValue(obj, attributeName, string);
}

bool ExportableMaterial::getColor(const MObject& obj, const char* attributeName, Float4& color)
{
	MColor c;
	if (!DagHelper::getPlugValue(obj, attributeName, c))
		return false;

	color = { c.r, c.g, c.b, c.a };
	return true;
}

ExportableMaterialBasePBR::ExportableMaterialBasePBR()
	: m_glBaseColorFactor{ 1,1,1,1 }
	, m_glEmissiveFactor{ 1,1,1,1 }
{
}

ExportableMaterialBasePBR::~ExportableMaterialBasePBR() = default;

bool ExportableMaterialBasePBR::hasTextures() const
{
	return m_glBaseColorTexture.texture ||
		m_glMetallicRoughnessTexture.texture ||
		m_glNormalTexture.texture ||
		m_glEmissiveTexture.texture ||
		m_glOcclusionTexture.texture;
}

ExportableMaterialPBR::ExportableMaterialPBR(ExportableResources& resources, const MFnDependencyNode& shaderNode)
{
	MStatus status;

	const auto shaderObject = shaderNode.object(&status);
	THROW_ON_FAILURE(status);

	const auto shaderType = shaderObject.apiType();

	switch (shaderType)
	{
	case MFn::kPhong:
		convert<MFnPhongShader>(resources, shaderObject);
		break;
	case MFn::kLambert:
		convert<MFnLambertShader>(resources, shaderObject);
		break;
	case MFn::kBlinn:
		convert<MFnBlinnShader>(resources, shaderObject);
		break;
	case MFn::kPluginHardwareShader:
		loadPBR(resources, shaderObject);
		break;
	default:
		cerr << prefix << "WARNING: skipping unsupported PBR shader type '" << shaderObject.apiTypeStr() << "' #" << shaderType << endl;
		break;
	}
}

template<class MFnShader>
void ExportableMaterialPBR::convert(ExportableResources& resources, const MObject& shaderObject)
{
	MStatus status;

	MFnShader shader(shaderObject, &status);
	THROW_ON_FAILURE(status);

	m_glMetallicRoughness.roughnessFactor = 1;
	m_glMetallicRoughness.metallicFactor = 0;
	m_glMaterial.metallicRoughness = &m_glMetallicRoughness;

	const ExportableTexture colorTexture(resources, shaderObject, "color");
	if (colorTexture)
	{
		m_glBaseColorTexture.texture = colorTexture;
		m_glMetallicRoughness.baseColorTexture = &m_glBaseColorTexture;
	}

	// TODO: Currently we expect the alpha channel of the color texture to hold the transparency.
	const bool hasTransparencyTexture = shader.findPlug("transparency").isConnected();

	const auto color = colorTexture ? MColor(1, 1, 1) : shader.color(&status);

	const auto diffuseFactor = shader.diffuseCoeff(&status);
	const auto transparency = hasTransparencyTexture ? 0 : shader.transparency(&status).r;
	const auto opacity = (1 - transparency) * resources.arguments().opacityFactor;

	// TODO: Currently we don't actually check the pixels of the transparency texture.
	const auto isTransparent = hasTransparencyTexture || opacity < 1;
	if (isTransparent)
	{
		m_glMaterial.alphaMode = "BLEND";
	}

	// TODO: Support MASK alphaMode and alphaCutoff

	// TODO: Support double-sides materials.

	m_glBaseColorFactor =
	{
		color.r * diffuseFactor,
		color.g * diffuseFactor,
		color.b * diffuseFactor,
		opacity
	};

	m_glMetallicRoughness.baseColorFactor = &m_glBaseColorFactor[0];

	// Normal
	float normalScale;
	GLTF::Texture* normalTexture;
	if (tryCreateNormalTexture(resources, shaderObject, normalScale, normalTexture))
	{
		m_glNormalTexture.texture = normalTexture;
		m_glNormalTexture.scale = normalScale;
		// TODO: m_glNormalTexture.texCoord = ...
		m_glMaterial.normalTexture = &m_glNormalTexture;
	}
}



void ExportableMaterialPBR::loadPBR(ExportableResources& resources, const MFnDependencyNode& shaderNode)
{
	MStatus status;
	const auto shaderObject = shaderNode.object(&status);
	THROW_ON_FAILURE(status);

	// Base color. For some reason Maya splits this attribute into separate RGB and A attributes
	m_glBaseColorFactor = { 1, 1, 1, resources.arguments().opacityFactor };

	Float4 customBaseColor = m_glBaseColorFactor;
	float customBaseAlpha = m_glBaseColorFactor[3];
	MString technique = "solid";

	const auto hasCustomColor = getColor(shaderObject, "u_BaseColorFactorRGB", customBaseColor);
	const auto hasCustomAlpha = getScalar(shaderObject, "u_BaseColorFactorA", customBaseAlpha);
	const auto hasTechnique = getString(shaderObject, "technique", technique);

	if (hasCustomColor | hasCustomAlpha)
	{
		customBaseColor[3] = customBaseAlpha;
		m_glBaseColorFactor = customBaseColor;
		m_glMetallicRoughness.baseColorFactor = &m_glBaseColorFactor[0];
		m_glMaterial.metallicRoughness = &m_glMetallicRoughness;
	}

	if (hasTechnique && technique.toLowerCase() == "transparent")
	{
		m_glMaterial.alphaMode = "BLEND";
	}

	const ExportableTexture baseColorTexture(resources, shaderObject, "u_BaseColorTexture");
	if (baseColorTexture)
	{
		m_glBaseColorTexture.texture = baseColorTexture;
		m_glMetallicRoughness.baseColorTexture = &m_glBaseColorTexture;
	}

	// Roughness and metallic 
	m_glMetallicRoughness.roughnessFactor = 0.5f;
	m_glMetallicRoughness.metallicFactor = 0.5f;
	const auto hasRoughnessStrength = getScalar(shaderObject, "u_RoughnessStrength", m_glMetallicRoughness.roughnessFactor);
	const auto hasMetallicStrength = getScalar(shaderObject, "u_MetallicStrength", m_glMetallicRoughness.metallicFactor);

	if (hasRoughnessStrength || hasMetallicStrength)
	{
		m_glMaterial.metallicRoughness = &m_glMetallicRoughness;
	}

	const ExportableTexture roughnessTexture(resources, shaderObject, "u_RoughnessTexture");
	const ExportableTexture metallicTexture(resources, shaderObject, "u_MetallicTexture");
	if (roughnessTexture || metallicTexture)
	{
		// TODO: Test this code!
		if (!metallicTexture || roughnessTexture == metallicTexture)
		{
			m_glMetallicRoughnessTexture.texture = roughnessTexture;
		}
		else if (!roughnessTexture)
		{
			m_glMetallicRoughnessTexture.texture = metallicTexture;
		}
		else
		{
			cerr << prefix << "WARNING: Merging roughness and metallic into one texture" << endl;

			MImage metallicImage;
			MImage roughnessImage;

			THROW_ON_FAILURE_WITH(metallicImage.readFromTextureNode(metallicTexture.connectedObject), 
				formatted("Failed to read metallic texture '%s'", metallicTexture.imageFilePath.asChar()));

			THROW_ON_FAILURE_WITH(roughnessImage.readFromTextureNode(roughnessTexture.connectedObject), 
				formatted("Failed to read roughness texture '%s'", roughnessTexture.imageFilePath.asChar()));
			
			unsigned width;
			unsigned height;
			THROW_ON_FAILURE(metallicImage.getSize(width, height));

			unsigned width2;
			unsigned height2;
			THROW_ON_FAILURE(roughnessImage.getSize(width2, height2));

			if (width != width2 || height != height2)
			{
				MayaException::printError(formatted("Images '%s' and '%s' have different size, not merging",
					metallicTexture.imageFilePath.asChar(),
					roughnessTexture.imageFilePath.asChar()));
			}
			else
			{
				// Merge metallic into roughness
				auto metallicPixels = reinterpret_cast<uint32_t*>(metallicImage.pixels());
				auto roughnessPixels = reinterpret_cast<uint32_t*>(roughnessImage.pixels());
				__int64 pixelCount = width * height;
				while (--pixelCount >= 0)
				{
					*roughnessPixels++ = (*roughnessPixels & 0xff00) | (*metallicPixels++ & 0xff0000) | 0xff000000;
				}

				// TODO: Add argument for output image file mime-type
				const path roughnessPath{ roughnessTexture.imageFilePath.asChar() };
				const path metallicPath{ metallicTexture.imageFilePath.asChar() };
				const path imageExtension { roughnessPath.extension() };
				path imageFilename{ roughnessPath.stem().string() + "-" + metallicPath.stem().string() };
				imageFilename.replace_extension(imageExtension);
				MString mergedImagePath{ (temp_directory_path() / imageFilename).c_str() };

				cout << prefix << "Saving merged roughness-metallic texture to " << mergedImagePath << endl;
				status = roughnessImage.writeToFile(mergedImagePath, imageExtension.c_str());
				THROW_ON_FAILURE_WITH(status,
					formatted("Failed to write merged metallic-roughness texture to '%s'", mergedImagePath.asChar()));

				const auto imagePtr = resources.getImage(mergedImagePath.asChar());
				assert(imagePtr);

				const auto texturePtr = resources.getTexture(imagePtr, roughnessTexture.glSampler);
				assert(texturePtr);

				m_glMetallicRoughnessTexture.texture = texturePtr;
			}
		}
		m_glMetallicRoughness.metallicRoughnessTexture = &m_glMetallicRoughnessTexture;
	}

	// Emissive 
	m_glEmissiveFactor = { 0,0,0,0 };
	if (getColor(shaderObject, "u_EmissiveColor", m_glEmissiveFactor))
	{
		m_glMaterial.emissiveFactor = &m_glEmissiveFactor[0];
	}

	const ExportableTexture emissiveTexture(resources, shaderObject, "u_EmissiveTexture");
	if (emissiveTexture)
	{
		m_glEmissiveTexture.texture = emissiveTexture;
		m_glMaterial.emissiveTexture = &m_glEmissiveTexture;
	}

	// Ambient occlusion
	getScalar(shaderObject, "u_OcclusionStrength", m_glOcclusionTexture.scale);

	const ExportableTexture occlusionTexture(resources, shaderObject, "u_OcclusionTexture");
	if (occlusionTexture)
	{
		m_glOcclusionTexture.texture = occlusionTexture;
		m_glMaterial.occlusionTexture = &m_glOcclusionTexture;
	}

	// Normal
	getScalar(shaderObject, "u_NormalScale", m_glNormalTexture.scale);

	const ExportableTexture normalTexture(resources, shaderObject, "u_NormalTexture");
	if (normalTexture)
	{
		m_glNormalTexture.texture = normalTexture;
		m_glMaterial.normalTexture = &m_glNormalTexture;
	}
}

ExportableMaterialPBR::~ExportableMaterialPBR() = default;

ExportableDefaultMaterial::ExportableDefaultMaterial()
{
	m_glBaseColorFactor = { 1, 1, 1, 1 };
	m_glMetallicRoughness.baseColorFactor = &m_glBaseColorFactor[0];

	m_glMetallicRoughness.metallicFactor = 0.5f;
	m_glMetallicRoughness.roughnessFactor = 0.5f;
	m_glMaterial.metallicRoughness = &m_glMetallicRoughness;

	m_glEmissiveFactor = { 0.1f, 0.1f, 0.1f };
	m_glMaterial.emissiveFactor = &m_glEmissiveFactor[0];
}

ExportableDefaultMaterial::~ExportableDefaultMaterial() = default;

ExportableDebugMaterial::ExportableDebugMaterial(const Float3& hsv)
{
	m_glBaseColorFactor = hsvToRgb(hsv, 1);
	m_glMetallicRoughness.baseColorFactor = &m_glBaseColorFactor[0];

	m_glMetallicRoughness.metallicFactor = 0;
	m_glMetallicRoughness.roughnessFactor = 1;
	m_glMaterial.metallicRoughness = &m_glMetallicRoughness;

	m_glEmissiveFactor = { 0, 0, 0 };
	m_glMaterial.emissiveFactor = &m_glEmissiveFactor[0];
}

ExportableDebugMaterial::~ExportableDebugMaterial() = default;
