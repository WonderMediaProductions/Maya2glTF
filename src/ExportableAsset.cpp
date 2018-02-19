#include "externals.h"
#include "ExportableAsset.h"
#include "Arguments.h"
#include "ExportableNode.h"
#include "MayaException.h"
#include "version.h"

ExportableAsset::ExportableAsset(const Arguments& args)
	: m_resources(args)
{
	m_glAsset.scenes.push_back(&m_glScene);
	m_glAsset.scene = 0;

	m_glAsset.metadata = &m_glMetadata;
	m_glMetadata.generator = "Maya2glTF";
	m_glMetadata.version = "2.0";

	auto& selection = args.selection;

	for (uint selectionIndex = 0; selectionIndex < selection.length(); ++selectionIndex)
	{
		MObject obj;
		THROW_ON_FAILURE(selection.getDependNode(selectionIndex, obj));

		MStatus status;
		MFnDependencyNode node(obj, &status);
		THROW_ON_FAILURE(status);

		if (obj.hasFn(MFn::kDagNode))
		{
			MDagPath dagPath;
			THROW_ON_FAILURE(selection.getDagPath(selectionIndex, dagPath));
			cout << prefix << "Processing " << dagPath.partialPathName() << "..." << endl;
			addNode(dagPath);
		}
		else
		{
			cerr << prefix << "WARNING: Skipping '" << node.name() << "' since it is not a DAG node" << endl;
		}
	}
}

ExportableAsset::~ExportableAsset()
{
}

const std::string& ExportableAsset::prettyJsonString() const
{
	if (m_prettyJsonString.empty() && !m_rawJsonString.empty())
	{
		// Pretty format the JSON
		rapidjson::Document jsonDocument;
		const bool hasParseErrors = jsonDocument.Parse(m_rawJsonString.c_str()).HasParseError();

		if (hasParseErrors)
		{
			cerr << "Failed to reformat glTF JSON, outputting raw JSON" << endl;
			m_prettyJsonString = m_rawJsonString;
		}
		else
		{
			rapidjson::StringBuffer jsonPrettyBuffer;

			// Write the pretty JSON
			rapidjson::PrettyWriter<rapidjson::StringBuffer> jsonPrettyWriter(jsonPrettyBuffer);
			jsonDocument.Accept(jsonPrettyWriter);
			m_prettyJsonString = jsonPrettyBuffer.GetString();
		}
	}

	return m_prettyJsonString;
}

void ExportableAsset::addNode(MDagPath& dagPath)
{
	auto exportableNode = ExportableNode::from(dagPath, m_resources);

	if (exportableNode)
	{
		m_glScene.nodes.push_back(&exportableNode->glNode);
		m_items.push_back(std::move(exportableNode));
	}
}

void ExportableAsset::save()
{
	const auto& args = m_resources.arguments();
	const auto buffer = m_glAsset.packAccessors();

	// Generate glTF JSON file
	rapidjson::StringBuffer jsonStringBuffer;
	rapidjson::Writer<rapidjson::StringBuffer> jsonWriter(jsonStringBuffer);
	jsonWriter.StartObject();

	const auto embed = !args.glb && !args.separate;

	GLTF::Options options;
	options.embeddedBuffers = embed;
	options.embeddedShaders = embed;
	options.embeddedTextures = embed;
	options.name = args.sceneName.asChar();
	options.binary = args.glb;

	m_glAsset.writeJSON(&jsonWriter, &options);
	jsonWriter.EndObject();

	m_rawJsonString = jsonStringBuffer.GetString();

	const auto outputFolder = path(args.outputFolder.asChar());
	create_directories(outputFolder);

	const auto outputFilename = std::string(args.sceneName.asChar()) + (args.glb ? ".glb" : ".glTF");
	const auto outputPath = outputFolder / outputFilename;

	cout << prefix << "Writing glTF file to '" << outputPath << "'" << endl;

	if (!options.embeddedTextures) {
		for (GLTF::Image* image : m_glAsset.getAllImages()) {
			path uri = outputFolder / image->uri;
			std::ofstream file;
			create(file, uri.generic_string(), ios::out | ios::binary);
			file.write(reinterpret_cast<char*>(image->data), image->byteLength);
			file.close();
		}
	}

	if (!options.embeddedBuffers) {

		if (buffer->data)
		{
			path uri = outputFolder / buffer->uri;
			std::ofstream file;
			create(file, uri.generic_string(), ios::out | ios::binary);
			file.write(reinterpret_cast<char*>(buffer->data), buffer->byteLength);
			file.close();
		}
		else
		{
			MayaException::printError(formatted("Buffer '%s' with URI '%s' has no data!", buffer->name, buffer->uri));
		}
	}

	if (!options.embeddedShaders) {
		for (GLTF::Shader* shader : m_glAsset.getAllShaders()) {
			path uri = outputFolder / shader->uri;
			std::ofstream file;
			create(file, uri.generic_string(), ios::out | ios::binary);
			file.write(shader->source.c_str(), shader->source.length());
			file.close();
		}
	}

	// Write glTF file.
	{
		const auto& jsonString = m_rawJsonString;

		std::ofstream file;
		create(file, outputPath.string(), ios::out | (args.glb ? ios::binary : ios::app));

		if (args.glb)
		{
			file.write("glTF", 4); // magic header

			const auto writeHeader = new uint32_t[2];
			writeHeader[0] = 2; // version

			const int jsonLength = static_cast<int>(jsonString.length());
			const int jsonPadding = (4 - (jsonLength & 3)) & 3;
			const int binPadding = (4 - (buffer->byteLength & 3)) & 3;

			const int headerLength = 12;
			const int chunkHeaderLength = 8;

			writeHeader[1] = headerLength +
				(chunkHeaderLength + jsonLength + jsonPadding) +
				(chunkHeaderLength + buffer->byteLength + binPadding); // length

			file.write(reinterpret_cast<char*>(writeHeader), sizeof(uint32_t) * 2); // GLB header

			writeHeader[0] = jsonLength + jsonPadding; // chunkLength
			writeHeader[1] = 0x4E4F534A; // chunkType JSON
			file.write(reinterpret_cast<char*>(writeHeader), sizeof(uint32_t) * 2);

			file.write(jsonString.c_str(), jsonLength);
			for (int i = 0; i < jsonPadding; i++) {
				file.write(" ", 1);
			}

			writeHeader[0] = buffer->byteLength + binPadding; // chunkLength
			writeHeader[1] = 0x004E4942; // chunkType BIN
			file.write(reinterpret_cast<char*>(writeHeader), sizeof(uint32_t) * 2);

			file.write(reinterpret_cast<char*>(buffer->data), buffer->byteLength);
			for (int i = 0; i < binPadding; i++) {
				file.write("\0", 1);
			}
		}
		else
		{
			file << prettyJsonString() << endl;
		}

		file.close();
	}

	if (args.dumpGLTF)
	{
		auto& out = *args.dumpGLTF;
		out << "glTF dump:" << endl;
		out << prettyJsonString();
		out << endl;
	}
}

void ExportableAsset::create(std::ofstream& file, const std::string& path, const std::ios_base::openmode mode)
{
	file.open(path, mode);

	if (!file.is_open())
	{
		std::ostringstream ss;
		ss << "Couldn't write to '" << path << "'";
		throw std::runtime_error(ss.str().c_str());
	}
}

