// @todo Replace static casts with c casts.
// @todo Remove std:: deps
// @todo Clean up header.

#include "../include/sync_max.h"
#include "../include/shared_data.h"

#include <bx/sharedbuffer.h>
#include <bx/string.h>

#include <queue>
#include <string>

static bx::SharedBufferI* s_buffer = NULL;
static bx::SharedBufferI* s_readbuffer = NULL;
static SharedData* s_shared = NULL;

static MCallbackIdArray s_callbackIdArray;

static std::queue<MObject> s_nodeAddedQueue;

struct QueueObject
{
	MString m_name;
	MObject m_object;
};

static std::queue<QueueObject> s_meshChangedQueue;
static std::queue<QueueObject> s_meshRemovedQueue;
static std::queue<QueueObject> s_transformChangedQueue;

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

	// Restore the original queue
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

void addTransformQueue(MObject& _node, const MString& _name)
{
	if (s_transformChangedQueue.empty() || s_transformChangedQueue.front().m_name != _name)
	{
		QueueObject object;
		object.m_name = _name;
		object.m_object = _node;

		if (!isInQueue(s_transformChangedQueue, object)) // @todo Optimize using something else instead of std::queue?
		{
			s_transformChangedQueue.push(object);
		}
	}
}

void addMeshRemoveQueue(MObject& _node, const MString& _name)
{
	QueueObject object;
	object.m_name = _name;
	object.m_object = _node;
	s_meshRemovedQueue.push(object);
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

		// 
		SharedData::CameraUpdate& event = s_shared->m_camera;
		for (uint32_t ii = 0; ii < 16; ++ii) event.m_view[ii] = static_cast<float>(view[ii / 4][ii % 4]);
		for (uint32_t ii = 0; ii < 16; ++ii) event.m_proj[ii] = static_cast<float>(proj[ii / 4][ii % 4]);
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

void callbackTimer(float _elapsedTime, float _lastTime, void* _clientData)
{
	uint32_t status = UINT32_MAX;
	s_readbuffer->read(&status, sizeof(uint32_t));

	if (!s_nodeAddedQueue.empty())
	{
		MObject& node = s_nodeAddedQueue.front();
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
				}
			}
		}
		s_nodeAddedQueue.pop();
		return;
	}
	
	// Update events.
	if (status == 1)
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
				MString uvSetName;
				fnMesh.getCurrentUVSetName(uvSetName);

				MFloatArray uArray, vArray;
				fnMesh.getUVs(uArray, vArray, &uvSetName);
				if (uArray.length() <= SHARED_DATA_CONFIG_MAX_VERTICES && vArray.length() <= SHARED_DATA_CONFIG_MAX_VERTICES)
				{
					for (uint32_t ii = 0; ii < uArray.length(); ++ii)
					{
						meshEvent.m_vertices[ii][6] = uArray[ii];
						meshEvent.m_vertices[ii][7] = vArray[ii];
					}
				}
				else
				{
					MGlobal::displayError("Mesh UVs are too big for shared memory.");
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

		// Update status.
		status = 0;
		s_readbuffer->write(&status, sizeof(uint32_t));
	}
}

constexpr const char* kPanel1 = "modelPanel1";
constexpr const char* kPanel2 = "modelPanel2";
constexpr const char* kPanel3 = "modelPanel3";
constexpr const char* kPanel4 = "modelPanel4";

/*
* Plugin entry point
* For remote control of maya
* open command port: commandPort -name ":1234"
* close command port: commandPort -cl -name ":1234"
* send command: see loadPlugin.py and unloadPlugin.py
*/
EXPORT MStatus initializePlugin(MObject obj) 
{
	MStatus status = MS::kSuccess;

	// Create plugin
	MFnPlugin plugin = MFnPlugin(obj, "Max Sync | Level Editor", "1.0", "Any", &status);

	// Initialize the shared memory
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

	// Create shared data
	s_shared = new SharedData();
	
	// Add timer callback
	s_callbackIdArray.append(MTimerMessage::addTimerCallback(
		0.01f,
		callbackTimer,
		NULL,
		&status
	));

	// Go over current scene and queue all meshes
	MItDag dagIt(MItDag::kBreadthFirst, MFn::kInvalid, &status);
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

	MGlobal::displayInfo("Max Sync plugin loaded!");
	return status;
}

EXPORT MStatus uninitializePlugin(MObject obj) 
{
	MStatus status = MS::kSuccess;

	// Remove the callback timer for update
	MMessage::removeCallbacks(s_callbackIdArray);

	// Delete shared data
	delete s_shared;
	s_shared = NULL;

	// Shutdown the shared memory 
	s_buffer->shutdown();
	delete s_buffer;
	s_buffer = NULL;

	s_readbuffer->shutdown();
	delete s_readbuffer;
	s_readbuffer = NULL;

	// 
	MFnPlugin plugin(obj);

	MGlobal::displayInfo(MString("Max Sync plugin unloaded!"));
	return status;
}