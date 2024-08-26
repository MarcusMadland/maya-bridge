// @todo Replace static casts with c casts.
// @todo Remove std:: deps
// @todo Clean up header.

#include "../include/maya_bridge.h"
#include "../include/shared_data.h"

#include <bx/sharedbuffer.h>
#include <bx/string.h>

#include <queue>
#include <string>

static bx::SharedBufferI* s_buffer = NULL;
static bx::SharedBufferI* s_readbuffer = NULL;
static SharedData* s_shared = NULL;

static MCallbackIdArray s_callbackIdArray;

void init();
void shutdown();

static std::queue<MObject> s_nodeAddedQueue;

struct QueueObject
{
	MString m_name;
	MObject m_object;
};

static std::queue<QueueObject> s_meshChangedQueue;
static std::queue<QueueObject> s_meshRemovedQueue;
static std::queue<QueueObject> s_transformChangedQueue;
static std::queue<QueueObject> s_materialChangedQueue;

static void clearAllQueue()
{
	while (!s_nodeAddedQueue.empty()) 
	{
		s_nodeAddedQueue.pop();
	}
	while (!s_meshChangedQueue.empty())
	{
		s_meshChangedQueue.pop();
	}
	while (!s_meshRemovedQueue.empty())
	{
		s_meshRemovedQueue.pop();
	}
	while (!s_transformChangedQueue.empty())
	{
		s_transformChangedQueue.pop();
	}
	while (!s_materialChangedQueue.empty())
	{
		s_materialChangedQueue.pop();
	}
}

// @todo Optimize using something else instead of std::queue?
static bool isInQueue(std::queue<QueueObject>& _queue, const QueueObject& _object)
{
	std::queue<QueueObject> tempQueue;
	bool found = false;

	while (!_queue.empty()) 
	{
		QueueObject frontItem = _queue.front();
		_queue.pop();
		if (frontItem.m_name == _object.m_name) 
		{
			found = true;
		}
		tempQueue.push(frontItem);
	}

	while (!tempQueue.empty()) 
	{
		_queue.push(tempQueue.front());
		tempQueue.pop();
	}

	return found;
}

void addMeshQueue(MObject& _node, const MString& _name)
{
	QueueObject object;
	object.m_name = _name;
	object.m_object = _node;
	s_meshChangedQueue.push(object);
}

void addMeshRemoveQueue(MObject& _node, const MString& _name)
{
	QueueObject object;
	object.m_name = _name;
	object.m_object = _node;
	s_meshRemovedQueue.push(object);
}

void addTransformQueue(MObject& _node, const MString& _name)
{
	if (s_transformChangedQueue.empty() || s_transformChangedQueue.front().m_name != _name)
	{
		QueueObject object;
		object.m_name = _name;
		object.m_object = _node;

		if (!isInQueue(s_transformChangedQueue, object)) 
		{
			s_transformChangedQueue.push(object);
		}
	}
}

void addMaterialQueue(MObject& _node, const MString& _name)
{
	if (s_materialChangedQueue.empty() || s_materialChangedQueue.front().m_name != _name)
	{
		QueueObject object;
		object.m_name = _name;
		object.m_object = _node;

		if (!isInQueue(s_materialChangedQueue, object)) 
		{
			s_materialChangedQueue.push(object);
		}
	}
}

void callbackPanelPreRender(const MString& _str, void* _clientData)
{
	M3dView activeView;
	MStatus result;
	if (_clientData != NULL)
	{
		result = M3dView::getM3dViewFromModelPanel((char*)_clientData, activeView);
	}
	else
	{
		activeView = M3dView::active3dView(&result);
	}
	
	// Update camera.
	if (result == MS::kSuccess)
	{
		// Get camera matrices.
		MMatrix view;
		{
			MStatus status = activeView.modelViewMatrix(view);
			if (status != MStatus::kSuccess) return;
		}

		MMatrix proj;
		{
			MStatus status = activeView.projectionMatrix(proj);
			if (status != MStatus::kSuccess) return;
		}

		// Check for reflection.
		if (((view[0][0] == 1 && view[1][0] == 0 && view[2][0] == 0 && view[3][0] == 0) ||
			 (view[0][0] == 0 && view[1][0] == 0 && view[2][0] == -1 && view[3][0] == 0)))
		{
			// Not active.
			return;
		}

		// Construct
		SharedData::CameraUpdate& camera = s_shared->m_camera;

		for (uint32_t ii = 0; ii < 16; ++ii)
		{
			camera.m_view[ii] = static_cast<float>(view[ii / 4][ii % 4]);
		}
		for (uint32_t ii = 0; ii < 16; ++ii)
		{
			camera.m_proj[ii] = static_cast<float>(proj[ii / 4][ii % 4]);
		}

		// Write
		s_buffer->write(s_shared, sizeof(SharedData));
	}
}

void callbackNodeMatrixModified(MObject& _node, MDagMessage::MatrixModifiedFlags& _modified, void* _clientData)
{
	MFnDagNode dagNode = MFnDagNode(_node);
	MString nodeName = dagNode.fullPathName();

	addTransformQueue(_node, nodeName);

	for (uint32_t ii = 0; ii < dagNode.childCount(); ++ii)
	{
		callbackNodeMatrixModified(dagNode.child(ii), _modified, _clientData);
	}
}

void callbackNodeAttributeChanged(MNodeMessage::AttributeMessage _msg, MPlug& _plug, MPlug& _otherPlug, void* _clientData)
{
	// We can't add QueueObject directly here because the node doesnt have a scene name yet.
	s_nodeAddedQueue.push(_plug.node());
}

void callbackNodeAdded(MObject& _node, void* _clientData)
{
	// We can't add QueueObject directly here because the node doesnt have a scene name yet.
	s_nodeAddedQueue.push(_node);
}

void callbackNodeRemoved(MObject& _node, void* _clientData)
{
	if (_node.hasFn(MFn::kMesh))
	{
		MFnDagNode dagNode = MFnDagNode(_node);
		MString nodeName = MFnDagNode(dagNode.parent(0)).fullPathName();

		addMeshRemoveQueue(_node, nodeName);
	}
}

void printAllAttributes(MObject materialNode) 
{
	MFnStandardSurfaceShader standardSurface(materialNode);

	MPlugArray plugs;
	standardSurface.getConnections(plugs);

	if (plugs.length() > 0)
	{
		for (uint32_t ii = 0; ii < plugs.length(); ++ii)
		{
			MPlug plug = plugs[ii];

			// Color Map
			if (plug.partialName() == "bc")
			{
				// Find texture
				MPlugArray connectedPlugs;
				plug.connectedTo(connectedPlugs, true, false);

				for (unsigned int j = 0; j < connectedPlugs.length(); ++j)
				{
					MPlug connectedPlug = connectedPlugs[j];
					MObject connectedNode = connectedPlug.node();

					// Check if the connected node is a file texture node
					MFnDependencyNode connectedNodeFn(connectedNode);
					if (connectedNodeFn.typeName() == "file")
					{
						MFnDependencyNode fileNodeFn(connectedNode);
						MPlug filePathPlug = fileNodeFn.findPlug("fileTextureName", true);
						if (!filePathPlug.isNull())
						{
							MString textureFilePath = filePathPlug.asString();
							MStreamUtils::stdOutStream() << "	color map: " << textureFilePath << "\n";
						}
					}
				}
			}
			// Normal Map
			else if (plug.partialName() == "n")
			{
				// Find texture
				MPlugArray connectedPlugs;
				plug.connectedTo(connectedPlugs, true, false);

				for (unsigned int j = 0; j < connectedPlugs.length(); ++j)
				{
					MPlug connectedPlug = connectedPlugs[j];
					MObject connectedNode = connectedPlug.node();

					// Check if the connected node is a file texture node
					MFnDependencyNode connectedNodeFn(connectedNode);

					if (connectedNodeFn.typeName() == "bump2d" || connectedNodeFn.typeName() == "bump3d" || connectedNodeFn.typeName() == "aiNormalMap")
					{
						MPlug bumpMapPlug = connectedNodeFn.findPlug("bumpValue", true);
						MFnDependencyNode normalConnectedNodeFn(bumpMapPlug);

						if (normalConnectedNodeFn.typeName() == "file")
						{
							MFnDependencyNode fileNodeFn(bumpMapPlug);
							MPlug filePathPlug = fileNodeFn.findPlug("fileTextureName", true);
							if (!filePathPlug.isNull())
							{
								MString textureFilePath = filePathPlug.asString();
								MStreamUtils::stdOutStream() << "	normal map: " << textureFilePath << "\n";
							}
						}
					}

					
				}
			}
			// Other...
			else
			{
				MStreamUtils::stdOutStream() << "	connection: " << plug.partialName() << "\n";
			}
		}
	}
	else
	{
		MStreamUtils::stdOutStream() << "No connections on material..." << "\n";
	}
}

void callbackTimer(float _elapsedTime, float _lastTime, void* _clientData)
{
	uint32_t status = UINT32_MAX;
	s_readbuffer->read(&status, sizeof(uint32_t));

	if (!s_nodeAddedQueue.empty())
	{
		MObject& node = s_nodeAddedQueue.front();

		// On entity add.
		if (node.hasFn(MFn::kTransform))
		{
			MFnDagNode dagNode = MFnDagNode(node);
			MString nodeName = dagNode.fullPathName();

			for (uint32_t ii = 0; ii < dagNode.childCount(); ++ii)
			{
				MObject child = dagNode.child(ii);
				if (child.hasFn(MFn::kMesh))
				{
					addTransformQueue(node, nodeName);
					addMeshQueue(child, nodeName);
					addMaterialQueue(child, nodeName);
				}
			}
		}

		s_nodeAddedQueue.pop();
		return;
	}
	
	// Reload scene.
	if (status & SHARED_DATA_MESSAGE_RELOAD_SCENE)
	{
		shutdown();
		clearAllQueue();
		init();
	}

	// Update events.
	if (status & SHARED_DATA_MESSAGE_RECEIVED)
	{
		// Update mesh. 
		SharedData::MeshEvent& meshEvent = s_shared->m_meshChanged;
		if (!s_meshRemovedQueue.empty())
		{
			MString& name = s_meshRemovedQueue.front().m_name;
			MObject& node = s_meshRemovedQueue.front().m_object;

			//
			bx::strCopy(meshEvent.m_name, 1024, name.asChar());
			MStreamUtils::stdOutStream() << "MeshEvent: Removing... [" << meshEvent.m_name << "]" << "\n";

			//
			meshEvent.m_numVertices = 0;
			meshEvent.m_numIndices = 0;

			s_meshRemovedQueue.pop();

			meshEvent.m_changed = true;
			s_buffer->write(s_shared, sizeof(SharedData));
		}
		else if (!s_meshChangedQueue.empty())
		{
			MString& name = s_meshChangedQueue.front().m_name;
			MObject& node = s_meshChangedQueue.front().m_object;

			// Callback Node Removed
			{
				MStatus status;
				MCallbackId id = MNodeMessage::addNodePreRemovalCallback(
					node,
					callbackNodeRemoved,
					NULL,
					&status
				);
				if (status == MStatus::kSuccess) // Only add if not already added.
				{
					s_callbackIdArray.append(id);
				}
			}

			// Callback World Matrix Modified
			{
				MDagPath path;
				MFnDagNode meshNode(node);
				meshNode.getPath(path);

				MStatus status;
				MCallbackId id = MDagMessage::addWorldMatrixModifiedCallback(
					path,
					callbackNodeMatrixModified,
					NULL,
					&status
				);
				if (status == MStatus::kSuccess) // Only add if not already added.
				{
					s_callbackIdArray.append(id);
				}
			}

			//
			bx::strCopy(meshEvent.m_name, 1024, name.asChar());
			MStreamUtils::stdOutStream() << "MeshEvent: Changing... [" << meshEvent.m_name << "]" << "\n";

			//
			MStatus status;
			MFnMesh fnMesh(node, &status);
			if (status == MS::kSuccess)
			{
				// Get positions
				MFloatPointArray vertexArray;
				fnMesh.getPoints(vertexArray, MSpace::kObject);
				meshEvent.m_numVertices = vertexArray.length();
				if (meshEvent.m_numVertices <= SHARED_DATA_CONFIG_MAX_VERTICES)
				{
					for (uint32_t ii = 0; ii < meshEvent.m_numVertices; ++ii)
					{
						meshEvent.m_vertices[ii][0] = vertexArray[ii].x;
						meshEvent.m_vertices[ii][1] = vertexArray[ii].y;
						meshEvent.m_vertices[ii][2] = vertexArray[ii].z; // Conversion to left handed
					}
				}
				else
				{
					MGlobal::displayError("Mesh vertices are too big for shared memory.");
					s_meshChangedQueue.pop();
					return;
				}

				// Get normals
				MFloatVectorArray normals;
				fnMesh.getNormals(normals, MSpace::kObject);
				if (normals.length() <= SHARED_DATA_CONFIG_MAX_VERTICES)
				{
					for (uint32_t ii = 0; ii < normals.length(); ++ii)
					{
						meshEvent.m_vertices[ii][3] = normals[ii].x;
						meshEvent.m_vertices[ii][4] = normals[ii].y;
						meshEvent.m_vertices[ii][5] = normals[ii].z;
					}
				}
				else
				{
					MGlobal::displayError("Mesh normals are too big for shared memory.");
					s_meshChangedQueue.pop();
					return;
				}

				// Get UVs
				MFloatArray uArray, vArray;
				fnMesh.getUVs(uArray, vArray);  // Get the UV arrays

				if (uArray.length() <= SHARED_DATA_CONFIG_MAX_VERTICES && vArray.length() <= SHARED_DATA_CONFIG_MAX_VERTICES)
				{
					MItMeshPolygon faceIter(fnMesh.object());  // Iterate over the faces of the mesh
					uint32_t uvIdx = 0;

					for (; !faceIter.isDone(); faceIter.next())
					{
						int numVertices = faceIter.polygonVertexCount();

						for (int i = 0; i < numVertices; ++i)
						{
							int vertexIndex = faceIter.vertexIndex(i);
							int uvIndex;
							faceIter.getUVIndex(i, uvIndex);

							meshEvent.m_vertices[vertexIndex][6] = uArray[uvIndex];
							meshEvent.m_vertices[vertexIndex][7] = 1.0f - vArray[uvIndex];  // Invert V if necessary

							uvIdx++;
						}
					}
				}
				else
				{
					MGlobal::displayError("Mesh UVs are too many for shared memory.");
					s_meshChangedQueue.pop();
					return;
				}

				// Get indices
				MIntArray triangleCounts, triangleVertices;
				fnMesh.getTriangles(triangleCounts, triangleVertices);
				meshEvent.m_numIndices = triangleVertices.length();
				if (meshEvent.m_numIndices <= SHARED_DATA_CONFIG_MAX_INDICES)
				{
					for (uint32_t ii = 0; ii < meshEvent.m_numIndices; ++ii)
					{
						meshEvent.m_indices[ii] = static_cast<uint16_t>(triangleVertices[ii]);
					}
				}
				else
				{
					MGlobal::displayError("Mesh indices are too many for shared memory.");
					s_meshChangedQueue.pop();
					return;
				}
			}

			s_meshChangedQueue.pop();

			meshEvent.m_changed = true;
			s_buffer->write(s_shared, sizeof(SharedData));
		}
		else
		{
			meshEvent.m_changed = false;
			s_buffer->write(s_shared, sizeof(SharedData));
		}

		// Update transform.
		SharedData::TransformEvent& transformEvent = s_shared->m_transformChanged;
		if (!s_transformChangedQueue.empty() && s_meshChangedQueue.empty())
		{
			MString& name = s_transformChangedQueue.front().m_name;
			MObject& node = s_transformChangedQueue.front().m_object;

			//
			bx::strCopy(transformEvent.m_name, 1024, name.asChar());
			MStreamUtils::stdOutStream() << "TransformEvent: Changing... [" << transformEvent.m_name << "]" << "\n";

			//
			MDagPath path;
			MFnDagNode(node).getPath(path);
			MFnTransform transform(path);

			MMatrix worldMatrix = path.inclusiveMatrix();
			MTransformationMatrix matrix(worldMatrix);
			double scaleArr[3];
			matrix.getScale(scaleArr, MSpace::kWorld);
			MVector scale = MVector(scaleArr[0], scaleArr[1], scaleArr[2]);

			MQuaternion rotation;
			transform.getRotationQuaternion(rotation.x, rotation.y, rotation.z, rotation.w, MSpace::kWorld);
			rotation = rotation.inverse();

			MVector translation = transform.getTranslation(MSpace::kWorld);

			transformEvent.m_pos[0] = (float)translation.x;
			transformEvent.m_pos[1] = (float)translation.y;
			transformEvent.m_pos[2] = (float)translation.z;
			transformEvent.m_rotation[0] = (float)rotation.x;
			transformEvent.m_rotation[1] = (float)rotation.y;
			transformEvent.m_rotation[2] = (float)rotation.z;
			transformEvent.m_rotation[3] = (float)rotation.w;
			transformEvent.m_scale[0] = (float)scale.x;
			transformEvent.m_scale[1] = (float)scale.y;
			transformEvent.m_scale[2] = (float)scale.z;

			s_transformChangedQueue.pop();

			transformEvent.m_changed = true;
			s_buffer->write(s_shared, sizeof(SharedData));
		}
		else
		{
			transformEvent.m_changed = false;
			s_buffer->write(s_shared, sizeof(SharedData));
		}

		// Update material.
		SharedData::MaterialEvent& materialEvent = s_shared->m_materialChanged;
		if (!s_materialChangedQueue.empty() && s_meshChangedQueue.empty())
		{
			MString& name = s_materialChangedQueue.front().m_name;
			MObject& node = s_materialChangedQueue.front().m_object;

			//
			bx::strCopy(materialEvent.m_name, 1024, name.asChar());
			MStreamUtils::stdOutStream() << "MaterialEvent: Changing... [" << materialEvent.m_name << "]" << "\n";

			//
			MFnDagNode dagNode = MFnDagNode(node);
			MString meshName = MFnDagNode(dagNode.parent(0)).fullPathName();
			MStreamUtils::stdOutStream() << "	mesh: " << meshName << "\n";

			MFnMesh meshForShade(node);
			MObjectArray shaders;
			unsigned int instanceNumber = meshForShade.instanceCount(false, NULL);
			MIntArray shade_indices;
			meshForShade.getConnectedShaders(0, shaders, shade_indices);
			int lLength = shaders.length();
			MPlug surfaceShade;
			MPlugArray connectedPlugs;
			MObject connectedPlug;

			switch (shaders.length())
			{
				case 0:
					break;

				case 1:
				{
					surfaceShade = MFnDependencyNode(shaders[0]).findPlug("surfaceShader", &status);
					if (surfaceShade.isNull())
					{
						break;
					}

					surfaceShade.connectedTo(connectedPlugs, true, false);
					if (connectedPlugs.length() != 1)
					{
						break;
					}

					connectedPlug = connectedPlugs[0].node();
					MStreamUtils::stdOutStream() << "	material: " << MFnDependencyNode(connectedPlug).name() << "\n";

					MFnStandardSurfaceShader standardSurface(connectedPlug);

					MPlugArray plugs;
					standardSurface.getConnections(plugs);

					if (plugs.length() > 0)
					{
						for (uint32_t ii = 0; ii < plugs.length(); ++ii)
						{
							MPlug plug = plugs[ii];

							// Color Map
							if (plug.partialName() == "bc")
							{
								// Find texture
								MPlugArray connectedPlugs;
								plug.connectedTo(connectedPlugs, true, false);

								for (unsigned int j = 0; j < connectedPlugs.length(); ++j)
								{
									MPlug connectedPlug = connectedPlugs[j];
									MObject connectedNode = connectedPlug.node();

									// Check if the connected node is a file texture node
									MFnDependencyNode connectedNodeFn(connectedNode);
									if (connectedNodeFn.typeName() == "file")
									{
										MFnDependencyNode fileNodeFn(connectedNode);
										MPlug filePathPlug = fileNodeFn.findPlug("fileTextureName", true);
										if (!filePathPlug.isNull())
										{
											MString textureFilePath = filePathPlug.asString();
											MStreamUtils::stdOutStream() << "	color map: " << textureFilePath << "\n";
											bx::strCopy(materialEvent.m_colorPath, 1024, textureFilePath.asChar());
										}
									}
								}
							}
							// Normal Map
							else if (plug.partialName() == "n")
							{
								
							}
							// Other...
							else
							{
								MStreamUtils::stdOutStream() << "	connection: " << plug.partialName() << "\n";
							}
						}
					}
					else
					{
						MStreamUtils::stdOutStream() << "No connections on material..." << "\n";
					}
				}
				break;
				default:
				{
					break;
				}
			}

			s_materialChangedQueue.pop();

			materialEvent.m_changed = true;
			s_buffer->write(s_shared, sizeof(SharedData));
		}
		else
		{
			materialEvent.m_changed = false;
			s_buffer->write(s_shared, sizeof(SharedData));
		}

		// Update status.
		status = SHARED_DATA_MESSAGE_NONE;
		s_readbuffer->write(&status, sizeof(uint32_t));
	}
}

constexpr const char* kPanel1 = "modelPanel1";
constexpr const char* kPanel2 = "modelPanel2";
constexpr const char* kPanel3 = "modelPanel3";
constexpr const char* kPanel4 = "modelPanel4";

void init()
{
	MStatus status;

	// Add timer callback
	s_callbackIdArray.append(MTimerMessage::addTimerCallback(
		0.01f,
		callbackTimer,
		NULL,
		&status
	));

	// Go over current scene and queue all meshes
	MItDag dagIt = MItDag(MItDag::kBreadthFirst, MFn::kInvalid, &status);
	for (; !dagIt.isDone(); dagIt.next())
	{
		MObject node = dagIt.currentItem();
		callbackNodeAdded(node, NULL);
	}

	// Add camera callback 
	M3dView activeView = M3dView::active3dView();
	MDagPath dag;
	activeView.getCamera(dag);
	MFnCamera camera = MFnCamera(dag);
	MObject parent = camera.parent(0);

	s_callbackIdArray.append(MUiMessage::add3dViewPreRenderMsgCallback(
		MString(kPanel1),
		callbackPanelPreRender,
		(void*)kPanel1,
		&status
	));

	s_callbackIdArray.append(MUiMessage::add3dViewPreRenderMsgCallback(
		MString(kPanel2),
		callbackPanelPreRender,
		(void*)kPanel2,
		&status
	));

	s_callbackIdArray.append(MUiMessage::add3dViewPreRenderMsgCallback(
		MString(kPanel3),
		callbackPanelPreRender,
		(void*)kPanel3,
		&status
	));

	s_callbackIdArray.append(MUiMessage::add3dViewPreRenderMsgCallback(
		MString(kPanel4),
		callbackPanelPreRender,
		(void*)kPanel4,
		&status
	));

	// Set and write camera data once on load.
	callbackPanelPreRender(MString(""), NULL);

	// Add node added callback.
	s_callbackIdArray.append(MDGMessage::addNodeAddedCallback(
		callbackNodeAdded,
		"dependNode",
		NULL,
		&status
	));

	MGlobal::displayInfo("Initialized plugin!");
}

void shutdown()
{
	MMessage::removeCallbacks(s_callbackIdArray);

	MGlobal::displayInfo("Shutdown plugin!");
}

EXPORT MStatus initializePlugin(MObject obj) 
{
	MStatus status = MS::kSuccess;

	// Create plugin.
	MFnPlugin plugin = MFnPlugin(obj, "Maya Bridge | MAX Level Editor", "2.1", "Any", &status);

	// Initialize the shared memory.
	s_buffer = new bx::SharedBuffer();
	if (!s_buffer->init("maya-bridge", sizeof(SharedData)))
	{
		MGlobal::displayError("Failed to sync shared memory");
		return status;
	}

	s_readbuffer = new bx::SharedBuffer();
	if (!s_readbuffer->init("maya-bridge-read", sizeof(uint32_t)))
	{
		MGlobal::displayError("Failed to sync shared memory");
		return status;
	}

	// Create shared data.
	s_shared = new SharedData();
	
	// Initialize plugin.
	init();

	return status;
}

EXPORT MStatus uninitializePlugin(MObject obj) 
{
	MStatus status = MS::kSuccess;

	// Shutdown plugin.
	shutdown();

	// Delete shared data.
	delete s_shared;
	s_shared = NULL;

	// Shutdown the shared memory.
	s_buffer->shutdown();
	delete s_buffer;
	s_buffer = NULL;

	s_readbuffer->shutdown();
	delete s_readbuffer;
	s_readbuffer = NULL;

	// Destroy plugin.
	MFnPlugin plugin(obj);

	return status;
}