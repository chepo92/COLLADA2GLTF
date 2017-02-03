#include "COLLADA2GLTFWriter.h"
#include "Base64.h"

COLLADA2GLTF::Writer::Writer(GLTF::Asset* asset, COLLADA2GLTF::Options* options) : _asset(asset), _options(options) {
	_indicesBufferView = new GLTF::BufferView(NULL, 0, GLTF::Constants::WebGL::ELEMENT_ARRAY_BUFFER);
	_attributesBufferView = new GLTF::BufferView(NULL, 0, GLTF::Constants::WebGL::ARRAY_BUFFER);
	_animationsBufferView = new GLTF::BufferView(NULL, 0);
}

void COLLADA2GLTF::Writer::cancel(const std::string& errorMessage) {

}

void COLLADA2GLTF::Writer::start() {

}

void COLLADA2GLTF::Writer::finish() {

}

bool COLLADA2GLTF::Writer::writeGlobalAsset(const COLLADAFW::FileInfo* asset) {
	return true;
}

COLLADABU::Math::Matrix4 getMatrixFromTransform(const COLLADAFW::Transformation* transform) {
	switch (transform->getTransformationType()) {
	case COLLADAFW::Transformation::ROTATE: {
		COLLADAFW::Rotate* rotate = (COLLADAFW::Rotate*)transform;
		COLLADABU::Math::Vector3 axis = rotate->getRotationAxis();
		axis.normalise();
		double angle = rotate->getRotationAngle();
		return COLLADABU::Math::Matrix4(COLLADABU::Math::Quaternion(COLLADABU::Math::Utils::degToRad(angle), axis));
	}
	case COLLADAFW::Transformation::TRANSLATE: {
		COLLADAFW::Translate* translate = (COLLADAFW::Translate*)transform;
		const COLLADABU::Math::Vector3& translation = translate->getTranslation();
		COLLADABU::Math::Matrix4 translationMatrix;
		translationMatrix.makeTrans(translation);
		return translationMatrix;
	}
	case COLLADAFW::Transformation::SCALE: {
		COLLADAFW::Scale* scale = (COLLADAFW::Scale*)transform;
		const COLLADABU::Math::Vector3& scaleVector = scale->getScale();
		COLLADABU::Math::Matrix4 scaleMatrix;
		scaleMatrix.makeScale(scaleVector);
		return scaleMatrix;
	}
	case COLLADAFW::Transformation::MATRIX: {
		COLLADAFW::Matrix* transformMatrix = (COLLADAFW::Matrix*)transform;
		return transformMatrix->getMatrix();
	}
	case COLLADAFW::Transformation::LOOKAT: {
		COLLADAFW::Lookat* lookAt = (COLLADAFW::Lookat*)transform;
		const COLLADABU::Math::Vector3& eye = lookAt->getEyePosition();
		const COLLADABU::Math::Vector3& center = lookAt->getInterestPointPosition();
		const COLLADABU::Math::Vector3& up = lookAt->getUpAxisDirection();
		COLLADABU::Math::Matrix4 lookAtMatrix = COLLADABU::Math::Matrix4::IDENTITY;
		if ((eye.x != center.x) || (eye.y != center.y) || (eye.z != center.z)) {
			COLLADABU::Math::Vector3 z = (eye - center);
			z.normalise();
			COLLADABU::Math::Vector3 x = up.crossProduct(z);
			x.normalise();
			COLLADABU::Math::Vector3 y = z.crossProduct(x);
			y.normalise();
			lookAtMatrix.setAllElements(
				x.x, y.x, z.x, 0,
				x.y, y.y, z.y, 0,
				x.z, y.z, z.z, 0,
				-(x.x * eye.x + x.y  * eye.y + x.z * eye.z),
				-(y.x * eye.x + y.y * eye.y + y.z * eye.z),
				-(z.x * eye.x + z.y * eye.y + z.z * eye.z),
				1);
			lookAtMatrix = lookAtMatrix.inverse();
			lookAtMatrix = lookAtMatrix.transpose();
		}
		return lookAtMatrix;
	}}
	return COLLADABU::Math::Matrix4::IDENTITY;
}

COLLADABU::Math::Matrix4 getFlattenedTransform(std::vector<const COLLADAFW::Transformation*> transforms) {
	COLLADABU::Math::Matrix4 matrix = COLLADABU::Math::Matrix4::IDENTITY;
	for (const COLLADAFW::Transformation* transform : transforms) {
		matrix = matrix * getMatrixFromTransform(transform);
	}
	return matrix;
}

void packColladaMatrix(COLLADABU::Math::Matrix4 matrix, GLTF::Node::TransformMatrix* transform) {
	for (int j = 0; j < 4; j++) {
		for (int k = 0; k < 4; k++) {
			transform->matrix[k * 4 + j] = (float)matrix.getElement(j, k);
		}
	}
}

bool COLLADA2GLTF::Writer::writeNodeToGroup(std::vector<GLTF::Node*>* group, const COLLADAFW::Node* colladaNode) {
	GLTF::Asset* asset = this->_asset;
	GLTF::Node* node = new GLTF::Node();
	COLLADABU::Math::Matrix4 matrix;
	GLTF::Node::TransformMatrix* transform;
	// Add root node to group
	group->push_back(node);
	std::string id = colladaNode->getOriginalId();
	node->id = id;
	node->name = colladaNode->getName();
	COLLADAFW::TransformationPointerArray transformations = colladaNode->getTransformations();

	std::vector<const COLLADAFW::Transformation*> nodeTransforms;
	bool isAnimated = false;
	int bufferNodes = 0;
	for (int i = 0; i < transformations.getCount(); i++) {
		const COLLADAFW::Transformation* transformation = transformations[i];
		const COLLADAFW::UniqueId& animationListId = transformation->getAnimationList();
		if (animationListId.isValid()) {
			GLTF::Node* animatedNode = new GLTF::Node();
			// These animated nodes are assigned the same ids initially, and a suffix is added when the animation gets applied
			animatedNode->id = id;

			// If the current top level node is animated, we need to make a buffer node so the transform is not changed
			if (isAnimated) {
				GLTF::Node* bufferNode = new GLTF::Node();
				bufferNode->id = id + "_" + std::to_string(bufferNodes);
				bufferNodes++;
				node->children.push_back(bufferNode);
				node = bufferNode;
			}
			node->children.push_back(animatedNode);

			// Any prior node transforms get flattened out onto the last node
			COLLADABU::Math::Matrix4 matrix = COLLADABU::Math::Matrix4::IDENTITY;
			if (nodeTransforms.size() > 0) {
				matrix = getFlattenedTransform(nodeTransforms);
			}
			transform = new GLTF::Node::TransformMatrix();
			packColladaMatrix(matrix, transform);
			node->transform = transform;
			nodeTransforms.clear();

			// The animated node has the current transformation
			matrix = getMatrixFromTransform(transformation);
			transform = new GLTF::Node::TransformMatrix();
			packColladaMatrix(matrix, transform);
			animatedNode->transform = transform;

			_animatedNodes[animationListId] = node;
			node = animatedNode;
			isAnimated = true;
		}
		else {
			nodeTransforms.push_back(transformation);
		}
	}
	// Write out remaining transforms onto top-level node
	matrix = COLLADABU::Math::Matrix4::IDENTITY;
	if (nodeTransforms.size() > 0) {
		// If the current top level node is animated, we need to make a buffer node so the transform is not changed
		matrix = getFlattenedTransform(nodeTransforms);
		if (matrix != COLLADABU::Math::Matrix4::IDENTITY) {
			if (isAnimated) {
				GLTF::Node* bufferNode = new GLTF::Node();
				bufferNode->id = id + "_" + std::to_string(bufferNodes);
				bufferNodes++;
				node->children.push_back(bufferNode);
				node = bufferNode;
			}
		}
	}
	transform = new GLTF::Node::TransformMatrix();
	packColladaMatrix(matrix, transform);
	if (node->transform == NULL) {
		node->transform = transform;
	}

	// Instance Geometries
	const COLLADAFW::InstanceGeometryPointerArray& instanceGeometries = colladaNode->getInstanceGeometries();
	int count = instanceGeometries.getCount();
	if (count > 0) {
		for (int i = 0; i < count; i++) {
			COLLADAFW::InstanceGeometry* instanceGeometry = instanceGeometries[i];
			COLLADAFW::MaterialBindingArray& materialBindings = instanceGeometry->getMaterialBindings();
			const COLLADAFW::UniqueId& objectId = instanceGeometry->getInstanciatedObjectId();
			std::map<COLLADAFW::UniqueId, GLTF::Mesh*>::iterator iter = _meshInstances.find(objectId);
			if (iter != _meshInstances.end()) {
				GLTF::Mesh* mesh = iter->second;
				int materialBindingsCount = materialBindings.getCount();
				if (materialBindingsCount > 0) {
					for (int j = 0; j < materialBindingsCount; j++) {
						COLLADAFW::MaterialBinding materialBinding = materialBindings[j];
						GLTF::Primitive* primitive = mesh->primitives[j];
						COLLADAFW::UniqueId materialId = materialBinding.getReferencedMaterial();
						COLLADAFW::UniqueId effectId = this->_materialEffects[materialId];
						GLTF::Material* material = _effectInstances[effectId];
						material->id = materialBinding.getName();
						if (primitive->material != NULL && primitive->material != material) {
							// This mesh primitive has a different material from a previous instance, clone the mesh and primitives
							mesh = (GLTF::Mesh*)mesh->clone();
							primitive = mesh->primitives[j];
						}
						primitive->material = material;
					}
				}
				node->meshes.push_back(mesh);
			}
		}
	}

	// Recurse child nodes
	const COLLADAFW::NodePointerArray& childNodes = colladaNode->getChildNodes();
	if (childNodes.getCount() > 0) {
		return this->writeNodesToGroup(&node->children, childNodes);
	}
	return true;
}

bool COLLADA2GLTF::Writer::writeNodesToGroup(std::vector<GLTF::Node*>* group, const COLLADAFW::NodePointerArray& nodes) {
	for (unsigned int i = 0; i < nodes.getCount(); i++) {
		if (!this->writeNodeToGroup(group, nodes[i])) {
			return false;
		}
	}
	return true;
}

bool COLLADA2GLTF::Writer::writeVisualScene(const COLLADAFW::VisualScene* visualScene) {
	GLTF::Asset* asset = this->_asset;
	GLTF::Scene* scene;
	if (asset->scene >= 0) {
		scene = new GLTF::Scene();
		asset->scenes.push_back(scene);
	}
	else {
		scene = asset->getDefaultScene();
	}
	return this->writeNodesToGroup(&scene->nodes, visualScene->getRootNodes());
}

bool COLLADA2GLTF::Writer::writeScene(const COLLADAFW::Scene* scene) {
	return true;
}

bool COLLADA2GLTF::Writer::writeLibraryNodes(const COLLADAFW::LibraryNodes* libraryNodes) {
	GLTF::Asset* asset = this->_asset;
	GLTF::Scene* scene = asset->getDefaultScene();
	return this->writeNodesToGroup(&scene->nodes, libraryNodes->getNodes());
}

void mapAttributeIndices(const unsigned int* rootIndices, const unsigned* indices, int count, std::string semantic, std::map<std::string, GLTF::Accessor*>* attributes, std::map<std::string, std::map<int, int>>* indicesMapping) {
	indicesMapping->emplace(semantic, std::map<int, int>());
	for (int i = 0; i < count; i++) {
		unsigned int rootIndex = rootIndices[i];
		unsigned int index = indices[i];
		if (rootIndex != index) {
			indicesMapping->at(semantic).emplace(rootIndex, index);
		}
	}
	attributes->emplace(semantic, (GLTF::Accessor*)NULL);
}

void mapAttributeIndicesArray(const unsigned int* rootIndices, const COLLADAFW::IndexListArray& indicesArray, int count, std::string baseSemantic, std::map<std::string, GLTF::Accessor*>* attributes, std::map<std::string, std::map<int, int>>* indicesMapping) {
	int indicesArrayCount = indicesArray.getCount();
	for (int i = 0; i < indicesArrayCount; i++) {
		std::string semantic = baseSemantic;
		if (indicesArrayCount > 1) {
			semantic += "_" + std::to_string(i);
		}
		mapAttributeIndices(rootIndices, indicesArray[i]->getIndices().getData(), count, semantic, attributes, indicesMapping);
	}
}

GLTF::Accessor* bufferAndMapVertexData(GLTF::BufferView* bufferView, GLTF::Accessor::Type type, const COLLADAFW::MeshVertexData& vertexData, std::map<int, int> indicesMapping) {
	int count = vertexData.getValuesCount();
	float* floatBuffer = new float[count];
	COLLADAFW::FloatOrDoubleArray::DataType dataType = vertexData.getType();
	for (int i = 0; i < count; i++) {
		int index = i;
		std::map<int, int>::iterator mappedIndex = indicesMapping.find(index);
		if (mappedIndex != indicesMapping.end()) {
			index = mappedIndex->second;
		}
		switch (dataType) {
		case COLLADAFW::FloatOrDoubleArray::DATA_TYPE_DOUBLE:
			floatBuffer[index] = (float)(vertexData.getDoubleValues()->getData()[i]);
			break;
		case COLLADAFW::FloatOrDoubleArray::DATA_TYPE_FLOAT:
			floatBuffer[index] = vertexData.getFloatValues()->getData()[i];
			break;
		default:
			free(floatBuffer);
			return NULL;
		}
	}
	GLTF::Accessor* accessor = new GLTF::Accessor(type, GLTF::Constants::WebGL::FLOAT, (unsigned char*)floatBuffer, count / GLTF::Accessor::getNumberOfComponents(type), bufferView);
	free(floatBuffer);
	return accessor;
}

float getMeshVertexDataAtIndex(const COLLADAFW::MeshVertexData& data, int index) {
	COLLADAFW::FloatOrDoubleArray::DataType type = data.getType();
	if (type == COLLADAFW::FloatOrDoubleArray::DATA_TYPE_DOUBLE) {
		return (float)data.getDoubleValues()->getData()[index];
	}
	return data.getFloatValues()->getData()[index];
}

std::string buildAttributeId(const COLLADAFW::MeshVertexData& data, int index, int count) {
	std::string id;
	for (int i = 0; i < count; i++) {
		id += std::to_string(getMeshVertexDataAtIndex(data, index * count + i)) + ":";
	}
	return id;
}

/**
 * Converts and writes a <COLLADAFW::Mesh> to a <GLTF::Mesh>.
 * The produced meshes are stored in `this->_meshInstances` indexed by their <COLLADAFW::UniqueId>.
 *
 * COLLADA has different sets of indices per attribute in primitives while glTF uses a single indices
 * accessor for a primitive and requires attributes to be aligned. Attributes are built using the
 * the COLLADA indices, and duplicate attributes are referenced by index.
 *
 * @param colladaMesh The COLLADA mesh to write to glTF
 * @return `true` if the operation completed succesfully, `false` if an error occured
 */
bool COLLADA2GLTF::Writer::writeMesh(const COLLADAFW::Mesh* colladaMesh) {
	GLTF::Mesh* mesh = new GLTF::Mesh();
	mesh->id = colladaMesh->getOriginalId();
	mesh->name = colladaMesh->getName();

	const COLLADAFW::MeshPrimitiveArray& meshPrimitives = colladaMesh->getMeshPrimitives();
	int meshPrimitivesCount = meshPrimitives.getCount();
	if (meshPrimitivesCount > 0) {
		// Create primitives
		for (int i = 0; i < meshPrimitivesCount; i++) {
			std::map<std::string, std::vector<float>> buildAttributes;
			std::map<std::string, unsigned short> attributeIndicesMapping;
			std::vector<unsigned short> buildIndices;
			COLLADAFW::MeshPrimitive* colladaPrimitive = meshPrimitives[i];
			GLTF::Primitive* primitive = new GLTF::Primitive();

			COLLADAFW::MeshPrimitive::PrimitiveType type = colladaPrimitive->getPrimitiveType();
			switch (colladaPrimitive->getPrimitiveType()) {
			case COLLADAFW::MeshPrimitive::LINES:
				primitive->mode = GLTF::Primitive::Mode::LINES;
				break;
			case COLLADAFW::MeshPrimitive::LINE_STRIPS:
				primitive->mode = GLTF::Primitive::Mode::LINE_STRIP;
				break;
			case COLLADAFW::MeshPrimitive::POLYLIST:
			case COLLADAFW::MeshPrimitive::POLYGONS:
			case COLLADAFW::MeshPrimitive::TRIANGLES:
				primitive->mode = GLTF::Primitive::Mode::TRIANGLES;
				break;
			case COLLADAFW::MeshPrimitive::TRIANGLE_STRIPS:
				primitive->mode = GLTF::Primitive::Mode::TRIANGLE_STRIP;
				break;
			case COLLADAFW::MeshPrimitive::TRIANGLE_FANS:
				primitive->mode = GLTF::Primitive::Mode::TRIANGLE_FAN;
				break;
			case COLLADAFW::MeshPrimitive::POINTS:
				primitive->mode = GLTF::Primitive::Mode::POINTS;
				break;
				primitive->mode = GLTF::Primitive::Mode::TRIANGLES;
				break;
			}

			if (primitive->mode == GLTF::Primitive::Mode::UNKNOWN) {
				continue;
			}
			int count = colladaPrimitive->getPositionIndices().getCount();
			std::map<std::string, const unsigned int*> semanticIndices;
			std::map<std::string, const COLLADAFW::MeshVertexData*> semanticData;
			std::string semantic = "POSITION";
			buildAttributes[semantic] = std::vector<float>();
			semanticIndices[semantic] = colladaPrimitive->getPositionIndices().getData();
			semanticData[semantic] = &colladaMesh->getPositions();
			primitive->attributes[semantic] = (GLTF::Accessor*)NULL;
			if (colladaPrimitive->hasNormalIndices()) {
				semantic = "NORMAL";
				buildAttributes[semantic] = std::vector<float>();
				semanticIndices[semantic] = colladaPrimitive->getNormalIndices().getData();
				semanticData[semantic] = &colladaMesh->getNormals();
				primitive->attributes[semantic] = (GLTF::Accessor*)NULL;
			}
			if (colladaPrimitive->hasBinormalIndices()) {
				semantic = "BINORMAL";
				buildAttributes[semantic] = std::vector<float>();
				semanticIndices[semantic] = colladaPrimitive->getBinormalIndices().getData();
				semanticData[semantic] = &colladaMesh->getBinormals();
				primitive->attributes[semantic] = (GLTF::Accessor*)NULL;
			}
			if (colladaPrimitive->hasTangentIndices()) {
				semantic = "TANGENT";
;				buildAttributes[semantic] = std::vector<float>();
				semanticIndices[semantic] = colladaPrimitive->getTangentIndices().getData();
				semanticData[semantic] = &colladaMesh->getTangents();
				primitive->attributes[semantic] = (GLTF::Accessor*)NULL;
			}
			if (colladaPrimitive->hasUVCoordIndices()) {
				COLLADAFW::IndexListArray& uvCoordIndicesArray = colladaPrimitive->getUVCoordIndicesArray();
				int uvCoordIndicesArrayCount = uvCoordIndicesArray.getCount();
				for (int j = 0; j < uvCoordIndicesArrayCount; j++) {
					semantic = "TEXCOORD_" + std::to_string(j);
					buildAttributes[semantic] = std::vector<float>();
					semanticIndices[semantic] = uvCoordIndicesArray[j]->getIndices().getData();
					semanticData[semantic] = &colladaMesh->getUVCoords();
					primitive->attributes[semantic] = (GLTF::Accessor*)NULL;
				}
			}
			if (colladaPrimitive->hasColorIndices()) {
				COLLADAFW::IndexListArray& colorIndicesArray = colladaPrimitive->getColorIndicesArray();
				int colorIndicesArrayCount = colorIndicesArray.getCount();
				for (int j = 0; j < colorIndicesArrayCount; j++) {
					semantic = "COLOR_" + std::to_string(j);
					buildAttributes[semantic] = std::vector<float>();
					semanticIndices[semantic] = colorIndicesArray[j]->getIndices().getData();
					semanticData[semantic] = &colladaMesh->getColors();
					primitive->attributes[semantic] = (GLTF::Accessor*)NULL;
				}
			}
			int index = 0;
			for (int j = 0; j < count; j++) {
				std::string attributeId;
				for (const auto& entry : semanticIndices) {
					semantic = entry.first;
					int numberOfComponents = 3;
					if (semantic.find("TEXCOORD") == 0) {
						numberOfComponents = 2;
					}
					attributeId += buildAttributeId(*semanticData[semantic], semanticIndices[semantic][j], numberOfComponents);
				}
				std::map<std::string, unsigned short>::iterator search = attributeIndicesMapping.find(attributeId);
				if (search != attributeIndicesMapping.end()) {
					buildIndices.push_back(search->second);
				}
				else {
					for (const auto& entry : buildAttributes) {
						semantic = entry.first;
						int numberOfComponents = 3;
						bool flipY = false;
						if (semantic.find("TEXCOORD") == 0) {
							numberOfComponents = 2;
							flipY = true;
						}
						for (int k = 0; k < numberOfComponents; k++) {
							float value = getMeshVertexDataAtIndex(*semanticData[semantic], semanticIndices[semantic][j] * numberOfComponents + k);
							if (flipY && k == 1) {
								value = 1 - value;
							}
							buildAttributes[semantic].push_back(value);
						}
					}
					attributeIndicesMapping[attributeId] = index;
					buildIndices.push_back(index);
					index++;
				}
			}
			// Create indices accessor
			primitive->indices = new GLTF::Accessor(GLTF::Accessor::Type::SCALAR, GLTF::Constants::WebGL::UNSIGNED_SHORT, (unsigned char*)&buildIndices[0], buildIndices.size(), this->_indicesBufferView);
			mesh->primitives.push_back(primitive);
			// Create attribute accessors
			for (const auto& entry : buildAttributes) {
				std::string semantic = entry.first;
				std::vector<float> attributeData = entry.second;
				GLTF::Accessor::Type type = GLTF::Accessor::Type::VEC3;
				if (semantic.find("TEXCOORD") == 0) {
					type = GLTF::Accessor::Type::VEC2;
				}
				GLTF::Accessor* accessor = new GLTF::Accessor(type, GLTF::Constants::WebGL::FLOAT, (unsigned char*)&attributeData[0], attributeData.size() / GLTF::Accessor::getNumberOfComponents(type), this->_attributesBufferView);
				primitive->attributes[semantic] = accessor;
			}
		}
	}
	this->_meshInstances[colladaMesh->getUniqueId()] = mesh;
	return true;
}

bool COLLADA2GLTF::Writer::writeGeometry(const COLLADAFW::Geometry* geometry) {
	switch (geometry->getType()) {
	case COLLADAFW::Geometry::GEO_TYPE_MESH:
		if (!this->writeMesh((COLLADAFW::Mesh*)geometry)) {
			return false;
		}
		break;
	default:
		return false;
	}
	return true;
}

bool COLLADA2GLTF::Writer::writeMaterial(const COLLADAFW::Material* material) {
	this->_materialEffects[material->getUniqueId()] = material->getInstantiatedEffect();
	return true;
}

void packColladaColor(COLLADAFW::Color color, float* packArray) {
	packArray[0] = (float)color.getRed();
	packArray[1] = (float)color.getGreen();
	packArray[2] = (float)color.getBlue();
	packArray[3] = (float)color.getAlpha();
}

bool COLLADA2GLTF::Writer::writeEffect(const COLLADAFW::Effect* effect) {
	const COLLADAFW::CommonEffectPointerArray& commonEffects = effect->getCommonEffects();
	if (commonEffects.getCount() > 0) {
		GLTF::MaterialCommon* material = new GLTF::MaterialCommon();

		// One effect makes one template material, it really isn't possible to process more than one of these
		const COLLADAFW::EffectCommon* effectCommon = commonEffects[0];
		switch (effectCommon->getShaderType()) {
		case COLLADAFW::EffectCommon::SHADER_BLINN: 
			material->technique = GLTF::MaterialCommon::BLINN;
			break;
		case COLLADAFW::EffectCommon::SHADER_CONSTANT: 
			material->technique = GLTF::MaterialCommon::CONSTANT;
			break;
		case COLLADAFW::EffectCommon::SHADER_PHONG: 
			material->technique = GLTF::MaterialCommon::PHONG;
			break;
		case COLLADAFW::EffectCommon::SHADER_LAMBERT: 
			material->technique = GLTF::MaterialCommon::LAMBERT;
			break;
		}

		COLLADAFW::ColorOrTexture ambient = effectCommon->getAmbient();
		if (ambient.isColor()) {
			packColladaColor(ambient.getColor(), material->values->ambient);
		}

		COLLADAFW::ColorOrTexture diffuse = effectCommon->getDiffuse();
		if (diffuse.isTexture()) {
			COLLADAFW::Texture colladaTexture = diffuse.getTexture();
			GLTF::Texture* texture = new GLTF::Texture();
			const COLLADAFW::SamplerPointerArray& samplers = effectCommon->getSamplerPointerArray();
			COLLADAFW::Sampler* colladaSampler = (COLLADAFW::Sampler*)samplers[colladaTexture.getSamplerId()];
			GLTF::Sampler* sampler = new GLTF::Sampler();
			GLTF::Image* image = _images[colladaSampler->getSourceImage()];
			texture->source = image;
			texture->sampler = sampler;
			material->values->diffuseTexture = texture;
		}
		else if (diffuse.isColor()) {
			packColladaColor(diffuse.getColor(), material->values->diffuse);
		}

		COLLADAFW::ColorOrTexture emission = effectCommon->getEmission();
		if (emission.isColor()) {
			packColladaColor(emission.getColor(), material->values->emission);
		}

		COLLADAFW::ColorOrTexture specular = effectCommon->getSpecular();
		if (specular.isColor()) {
			packColladaColor(specular.getColor(), material->values->specular);
		}

		COLLADAFW::FloatOrParam shininess = effectCommon->getShininess();
		if (shininess.getType() == COLLADAFW::FloatOrParam::FLOAT) {
			material->values->shininess[0] = shininess.getFloatValue();
		}

		this->_effectInstances[effect->getUniqueId()] = material;
	}

	return true;
}

bool COLLADA2GLTF::Writer::writeCamera(const COLLADAFW::Camera* camera) {
	return true;
}

bool COLLADA2GLTF::Writer::writeImage(const COLLADAFW::Image* colladaImage) {
	const COLLADABU::URI imageUri = colladaImage->getImageURI();
	std::string relativePathFile = _options->basePath + imageUri.getURIString();
	std::string mimeType = "image/" + imageUri.getPathExtension();
	std::string uri = COLLADABU::URI::uriDecode(imageUri.getPathDir() + relativePathFile);

	GLTF::Image* image;
	if (_options->embedded) {
		FILE* file = fopen(uri.c_str(), "rb");
		if (file == NULL) {
			return false;
		}
		fseek(file, 0, SEEK_END);
		long int size = ftell(file);
		fclose(file);
		file = fopen(uri.c_str(), "rb");
		unsigned char* buffer = (unsigned char*)malloc(size);
		int bytes_read = fread(buffer, sizeof(unsigned char), size, file);
		fclose(file);

		image = new GLTF::Image("data:" + mimeType + ";base64," + GLTF::Base64::encode(buffer, size));
		free(buffer);
	}
	else {
		image = new GLTF::Image(uri);
	}
	image->id = colladaImage->getOriginalId();
	_images[colladaImage->getUniqueId()] = image;
	return true;
}

bool COLLADA2GLTF::Writer::writeLight(const COLLADAFW::Light* light) {
	return true;
}

bool COLLADA2GLTF::Writer::writeAnimation(const COLLADAFW::Animation* animation) {
	GLTF::Animation::Sampler* sampler = new GLTF::Animation::Sampler();
	sampler->id = animation->getOriginalId();

	if (animation->getAnimationType() == COLLADAFW::Animation::ANIMATION_CURVE) {
		COLLADAFW::AnimationCurve *animationCurve = (COLLADAFW::AnimationCurve*)animation;
		COLLADAFW::FloatOrDoubleArray inputArray = animationCurve->getInputValues();
		COLLADAFW::FloatOrDoubleArray outputArray = animationCurve->getOutputValues();
		
		int length = inputArray.getValuesCount();
		float* inputValues = new float[length];
		float* outputValues = new float[length];

		float value;
		for (int i = 0; i < length; i++) {
			switch (inputArray.getType()) {
			case COLLADAFW::FloatOrDoubleArray::DATA_TYPE_DOUBLE:
				value = (float)(inputArray.getDoubleValues()->getData()[i]);
				break;
			case COLLADAFW::FloatOrDoubleArray::DATA_TYPE_FLOAT:
				value = inputArray.getFloatValues()->getData()[i];
				break;
			}
			inputValues[i] = value;

			switch (outputArray.getType()) {
			case COLLADAFW::FloatOrDoubleArray::DATA_TYPE_DOUBLE:
				value = (float)(outputArray.getDoubleValues()->getData()[i]);
				break;
			case COLLADAFW::FloatOrDoubleArray::DATA_TYPE_FLOAT:
				value = outputArray.getFloatValues()->getData()[i];
				break;
			}
			outputValues[i] = value;
		}

		GLTF::Accessor* inputAccessor = new GLTF::Accessor(GLTF::Accessor::Type::SCALAR, GLTF::Constants::WebGL::FLOAT, (unsigned char*)inputValues, length, _animationsBufferView);
		// The type is unknown at this point, so this output accessor doesn't get packed yet, since we may need to modify the data
		GLTF::Accessor* outputAccessor = new GLTF::Accessor(GLTF::Accessor::Type::SCALAR, GLTF::Constants::WebGL::FLOAT, (unsigned char*)outputValues, length, GLTF::Constants::WebGL::ARRAY_BUFFER);
		
		sampler->input = inputAccessor;
		sampler->output = outputAccessor;
		
		_animationSamplers[animation->getUniqueId()] = sampler;
	}
	return true;
}

bool COLLADA2GLTF::Writer::writeAnimationList(const COLLADAFW::AnimationList* animationList) {
	const COLLADAFW::AnimationList::AnimationBindings& bindings = animationList->getAnimationBindings();
	GLTF::Node* node = _animatedNodes[animationList->getUniqueId()];
	GLTF::Animation* animation = new GLTF::Animation();

	double* component = new double[1];
	for (int i = 0; i < bindings.getCount(); i++) {
		const COLLADAFW::AnimationList::AnimationBinding& binding = bindings[i];
		GLTF::Animation::Channel* channel = new GLTF::Animation::Channel();
		GLTF::Animation::Channel::Target* target = new GLTF::Animation::Channel::Target();
		channel->target = target;
		animation->channels.push_back(channel);
		target->node = node;
		GLTF::Animation::Sampler* sampler = _animationSamplers[binding.animation];
		channel->sampler = sampler;
		GLTF::Accessor* output = sampler->output;

		int usesIndex = -1;
		float* outputData = NULL;
		GLTF::Accessor::Type type;
		int count = output->count;

		GLTF::Node::Transform* transform = node->transform;
		if (transform->type == GLTF::Node::Transform::MATRIX) {
			transform = ((GLTF::Node::TransformMatrix*)transform)->getTransformTRS();
			node->transform = transform;
		}

		switch (binding.animationClass) {
		case COLLADAFW::AnimationList::AnimationClass::MATRIX4X4: {
			float* translation = new float[count / 16 * 3];
			float* rotation = new float[count / 16 * 4];
			float* scale = new float[count / 16 * 3];
			GLTF::Node::TransformMatrix* transformMatrix = new GLTF::Node::TransformMatrix();
			GLTF::Node::TransformTRS* transformTRS = new GLTF::Node::TransformTRS();
			for (int j = 0; j < count / 16; j++) {
				for (int k = 0; k < 16; k++) {
					output->getComponentAtIndex(j * 16 + k, component);
					transformMatrix->matrix[k] = component[0];
				}
				transformMatrix->getTransformTRS(transformTRS);
				for (int k = 0; k < 3; k++) {
					translation[j * 3 + k] = transformTRS->translation[k];
				}
				for (int k = 0; k < 4; k++) {
					rotation[j * 4 + k] = transformTRS->rotation[k];
				}
				for (int k = 0; k < 3; k++) {
					scale[j * 3 + k] = transformTRS->scale[k];
				}
			}
			animation->channels.clear();
			GLTF::Animation::Channel* translationChannel = new GLTF::Animation::Channel();
			GLTF::Animation::Channel::Target* translationTarget = new GLTF::Animation::Channel::Target();
			translationTarget->path = GLTF::Animation::Channel::Target::Path::TRANSLATION;
			translationTarget->node = node;
			translationChannel->target = translationTarget;
			animation->channels.push_back(translationChannel);
			GLTF::Animation::Sampler* translationSampler = new GLTF::Animation::Sampler();
			translationChannel->sampler = translationSampler;
			translationSampler->id = sampler->id + "_translation";
			translationSampler->input = sampler->input;
			translationSampler->output = new GLTF::Accessor(GLTF::Accessor::Type::VEC3, GLTF::Constants::WebGL::FLOAT, (unsigned char*)translation, count / 16, _animationsBufferView);

			GLTF::Animation::Channel* rotationChannel = new GLTF::Animation::Channel();
			GLTF::Animation::Channel::Target* rotationTarget = new GLTF::Animation::Channel::Target();
			rotationTarget->path = GLTF::Animation::Channel::Target::Path::ROTATION;
			rotationTarget->node = node;
			rotationChannel->target = rotationTarget;
			animation->channels.push_back(rotationChannel);
			GLTF::Animation::Sampler* rotationSampler = new GLTF::Animation::Sampler();
			rotationChannel->sampler = rotationSampler;
			rotationSampler->id = sampler->id + "_rotation";
			rotationSampler->input = sampler->input;
			rotationSampler->output = new GLTF::Accessor(GLTF::Accessor::Type::VEC4, GLTF::Constants::WebGL::FLOAT, (unsigned char*)rotation, count / 16, _animationsBufferView);

			GLTF::Animation::Channel* scaleChannel = new GLTF::Animation::Channel();
			GLTF::Animation::Channel::Target* scaleTarget = new GLTF::Animation::Channel::Target();
			scaleTarget->path = GLTF::Animation::Channel::Target::Path::SCALE;
			scaleTarget->node = node;
			scaleChannel->target = scaleTarget;
			animation->channels.push_back(scaleChannel);
			GLTF::Animation::Sampler* scaleSampler = new GLTF::Animation::Sampler();
			scaleChannel->sampler = scaleSampler;
			scaleSampler->id = sampler->id + "_scale";
			scaleSampler->input = sampler->input;
			scaleSampler->output = new GLTF::Accessor(GLTF::Accessor::Type::VEC3, GLTF::Constants::WebGL::FLOAT, (unsigned char*)scale, count / 16, _animationsBufferView);
			node->id += "_transform";
			break;
		}
		case COLLADAFW::AnimationList::AnimationClass::POSITION_XYZ: {
			// The output data is already in the correct format
			outputData = (float*)output->bufferView->buffer->data;
			type = GLTF::Accessor::Type::VEC3;
			target->path = GLTF::Animation::Channel::Target::Path::TRANSLATION;
			node->id += "_translate";
			output = new GLTF::Accessor(type, GLTF::Constants::WebGL::FLOAT, (unsigned char*)outputData, count, _animationsBufferView);
			sampler->output = output;
			break;
		}
		case COLLADAFW::AnimationList::AnimationClass::POSITION_X: {
			usesIndex = 0;
		}
		case COLLADAFW::AnimationList::AnimationClass::POSITION_Y: {
			if (usesIndex < 0) {
				usesIndex = 1;
			}
		}
		case COLLADAFW::AnimationList::AnimationClass::POSITION_Z: {
			if (usesIndex < 0) {
				usesIndex = 2;
			}
			// The output data needs to be padded with 0's 
			float* outputData = new float[count * 3];
			type = GLTF::Accessor::Type::VEC3;
			target->path = GLTF::Animation::Channel::Target::Path::TRANSLATION;
			node->id += "_translate";
			for (int j = 0; j < count; j++) {
				output->getComponentAtIndex(j, component);
				for (int k = 0; k < 3; k++) {
					if (k == usesIndex) {
						outputData[j * 3 + k] = (float)component[0];
					}
					else {
						outputData[j * 3 + k] = 0;
					}
				}
			}
			output = new GLTF::Accessor(type, GLTF::Constants::WebGL::FLOAT, (unsigned char*)outputData, count, _animationsBufferView);
			sampler->output = output;
			break;
		}
		case COLLADAFW::AnimationList::AXISANGLE: {
			// The output data is already in the correct format
			outputData = (float*)output->bufferView->buffer->data;
			type = GLTF::Accessor::Type::VEC4;
			target->path = GLTF::Animation::Channel::Target::Path::ROTATION;
			node->id += "_rotate";
			output = new GLTF::Accessor(type, GLTF::Constants::WebGL::FLOAT, (unsigned char*)outputData, count, _animationsBufferView);
			sampler->output = output;
			break;
		}
		case COLLADAFW::AnimationList::ANGLE: {
			// The angle needs to be applied to the existing quaternion
			if (transform != NULL) {
				if (transform->type == GLTF::Node::Transform::TRS) {
					GLTF::Node::TransformTRS* transformTRS = (GLTF::Node::TransformTRS*)transform;
					float* rotation = transformTRS->rotation;
					outputData = new float[count * 4];
					type = GLTF::Accessor::Type::VEC4;
					target->path = GLTF::Animation::Channel::Target::Path::ROTATION;
					node->id += "_rotate";

					COLLADABU::Math::Real angle;
					COLLADABU::Math::Vector3 axis;
					for (int j = 0; j < count; j++) {
						COLLADABU::Math::Quaternion quaternion = COLLADABU::Math::Quaternion(rotation[0], rotation[1], rotation[2], rotation[3]);
						quaternion.toAngleAxis(angle, axis);
						output->getComponentAtIndex(j, component);
						angle = COLLADABU::Math::Utils::degToRad(component[0]);
						quaternion.fromAngleAxis(angle, axis);
						outputData[j * 4] = (float)quaternion.x;
						outputData[j * 4 + 1] = (float)quaternion.y;
						outputData[j * 4 + 2] = (float)quaternion.z;
						outputData[j * 4 + 3] = (float)quaternion.w;
					}
				}
			}
			output = new GLTF::Accessor(type, GLTF::Constants::WebGL::FLOAT, (unsigned char*)outputData, count, _animationsBufferView);
			sampler->output = output;
			break;
		}}
	}
	_asset->animations.push_back(animation);
	return true;
}

bool COLLADA2GLTF::Writer::writeSkinControllerData(const COLLADAFW::SkinControllerData* skinControllerData) {
	return true;
}

bool COLLADA2GLTF::Writer::writeController(const COLLADAFW::Controller* controller) {
	if (controller->getControllerType() == COLLADAFW::Controller::CONTROLLER_TYPE_SKIN) {
		COLLADAFW::SkinController* skinController = (COLLADAFW::SkinController*)controller;
		GLTF::Skin* skin = new GLTF::Skin();
	}
	return true;
}

bool COLLADA2GLTF::Writer::writeFormulas(const COLLADAFW::Formulas* formulas) {
	return true;
}

bool COLLADA2GLTF::Writer::writeKinematicsScene(const COLLADAFW::KinematicsScene* kinematicsScene) {
	return true;
}
