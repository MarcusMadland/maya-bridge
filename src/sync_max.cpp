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
static SharedData* s_shared = NULL;

static bool s_processed = false;

static MCallbackIdArray s_callbackIdArray;

struct QueueObject
{
	MString m_name;
	MObject m_object;
};

std::queue<QueueObject> s_meshChangedQueue;
std::queue<QueueObject> s_meshRemovedQueue;

std::queue<QueueObject> s_transformChangedQueue;

static const MString getParentName(MObject _node)
{
	MFnDagNode dn(_node);
	return MFnDependencyNode(dn.parent(0)).name().asChar();
}

static const MString getNodeName(MObject _node)
{
	MFnDagNode dn(_node);
	return dn.name().asChar();
}

static const MString getMeshName(MObject _mesh)
{
	MFnDagNode dn(_mesh);
	MDagPath path;
	MStatus status = dn.getPath(path);
	if (status != MStatus::kFailure)
	{
		std::string pathName = path.fullPathName().asChar();
		int index = pathName.find_last_of("|");
		std::string meshName = pathName.substr(1, index - 1);
		return meshName.c_str();
	}
	return "";
}

void write()
{
	// Update synced.
	SharedData::SyncEvent& event = s_shared->m_sync;
	event.m_isSynced = true;

	// Write to shared buffer.
	s_shared->m_processed = s_processed;
	s_buffer->write(s_shared, sizeof(SharedData));

	//MGlobal::displayInfo("MAYA is synced with MAX...");
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
		SharedData::CameraEvent& event = s_shared->m_camera;
		for (uint32_t ii = 0; ii < 16; ++ii) event.m_view[ii] = static_cast<float>(view[ii / 4][ii % 4]);
		for (uint32_t ii = 0; ii < 16; ++ii) event.m_proj[ii] = static_cast<float>(proj[ii / 4][ii % 4]);

		write();
	}
}

void callbackNodeAttributeChanged(MNodeMessage::AttributeMessage _msg, MPlug& _plug, MPlug& _otherPlug, void* _clientData)
{
	MObject node = _plug.node();

	if (MNodeMessage::AttributeMessage::kAttributeSet & _msg)
	{
		//
	}
	else if (MNodeMessage::AttributeMessage::kAttributeEval & _msg)
	{
		if (node.hasFn(MFn::kMesh))
		{
			QueueObject object;
			object.m_name = getMeshName(node);
			object.m_object = node;
			s_meshChangedQueue.push(object);
		}
	}
}

void callbackNodeAdded(MObject& _node, void* _clientData)
{
	MFnDependencyNode fn(_node);

	if (_node.hasFn(MFn::kMesh))
	{
		if ((s_meshChangedQueue.size() == 0) || (_node != s_meshChangedQueue.front().m_object))
		{
			QueueObject object;
			object.m_name = getMeshName(_node);
			object.m_object = _node;
			s_meshChangedQueue.push(object);
			MGlobal::displayInfo("Mesh added to queue.");
		}
	}
}

void callbackNodeRemoved(MObject& _node, void* _clientData)
{
	if (_node.hasFn(MFn::kMesh))
	{
		if ((s_meshRemovedQueue.size() == 0) || (_node != s_meshRemovedQueue.front().m_object))
		{
			QueueObject object;
			object.m_name = getParentName(_node);
			object.m_object = _node;
			s_meshRemovedQueue.push(object);
			MGlobal::displayInfo("Mesh added to removal queue.");
		}
	}
}

void callbackWorldMatrixModified(MObject& _node, MDagMessage::MatrixModifiedFlags& _modified, void* _clientData)
{
	MFnDependencyNode fn(_node);
	
	if ((s_transformChangedQueue.size() == 0) || (_node != s_transformChangedQueue.front().m_object))
	{
		QueueObject object;
		object.m_name = getNodeName(_node);
		object.m_object = _node;
		s_transformChangedQueue.push(object);
		MGlobal::displayInfo("Transform added to queue.");
	}
}

void callbackTimer(float _elapsedTime, float _lastTime, void* _clientData)
{
	// Update meshes. (Node expected to be MFn::kMesh)
	SharedData::MeshEvent& meshEvent = s_shared->m_meshChanged;
	if (!s_meshRemovedQueue.empty())
	{
		MString& name = s_meshRemovedQueue.front().m_name;
		MObject& node = s_meshRemovedQueue.front().m_object;

		// On removed.
		MString message = "Removing node: ";
		message += name;
		MGlobal::displayInfo(message);

		//
		bx::strCopy(meshEvent.m_name, 1024, name.asChar());

		//
		meshEvent.m_numVertices = 0;
		meshEvent.m_numIndices = 0;

		// 
		s_meshRemovedQueue.pop();
		meshEvent.m_changed = true;
		write();
	}
	else if (!s_meshChangedQueue.empty())
	{
		MString& name = s_meshChangedQueue.front().m_name;
		MObject& node = s_meshChangedQueue.front().m_object;
		
		// Callback Node Attribute Changed
		{
			MStatus status;
			MCallbackId id = MNodeMessage::addAttributeChangedCallback(
				node,
				callbackNodeAttributeChanged,
				NULL,
				&status
			);
			if (status == MStatus::kSuccess) // Only add if not already added.
			{
				s_callbackIdArray.append(id);
			}
		}

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
				callbackWorldMatrixModified,
				NULL,
				&status
			);
			if (status == MStatus::kSuccess) // Only add if not already added.
			{
				s_callbackIdArray.append(id);
			}
		}
		
		// On changed.
		MString message = "Changing node: ";
		message += name;
		MGlobal::displayInfo(message);

		//
		bx::strCopy(meshEvent.m_name, 1024, name.asChar());

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

		//
		s_meshChangedQueue.pop();
		meshEvent.m_changed = true;
		write();
	}
	else
	{
		meshEvent.m_changed = false;
		write();
	}

	// Update transforms.
	SharedData::TransformEvent& transformEvent = s_shared->m_transformChanged;
	if (!s_transformChangedQueue.empty())
	{
		MString& name = s_transformChangedQueue.front().m_name;
		MObject& node = s_transformChangedQueue.front().m_object;

		// On changed.
		MString message = "Transforming node: ";
		message += name;
		MGlobal::displayInfo(message);

		//
		bx::strCopy(transformEvent.m_name, 1024, name.asChar());

		//
		MDagPath path;
		MFnDagNode(node).getPath(path);
		MFnTransform transform(path);

		MVector translation = transform.getTranslation(MSpace::kWorld);

		MQuaternion rotation;
		transform.getRotationQuaternion(rotation.x, rotation.y, rotation.z, rotation.w, MSpace::kWorld);

		double scaleValue[3];
		transform.getScale(scaleValue);
		MVector scale = MVector(scaleValue[0], scaleValue[1], scaleValue[2]);

		transformEvent.m_pos[0]      = (float)translation.x;
		transformEvent.m_pos[1]      = (float)translation.y;
		transformEvent.m_pos[2]      = (float)translation.z;
		transformEvent.m_rotation[0] = (float)rotation.x;
		transformEvent.m_rotation[1] = (float)rotation.y;
		transformEvent.m_rotation[2] = (float)-rotation.z;
		transformEvent.m_rotation[3] = (float)rotation.w;
		transformEvent.m_scale[0]    = (float)scale.x;
		transformEvent.m_scale[1]    = (float)scale.y;
		transformEvent.m_scale[2]    = (float)scale.z;

		//
		s_transformChangedQueue.pop();
		transformEvent.m_changed = true;
		write();
	}
	else
	{
		transformEvent.m_changed = false;
		write();
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

	// 
	s_processed = true;

	// Create plugin
	MFnPlugin plugin = MFnPlugin(obj, "Max Sync | Level Editor", "1.0", "Any", &status);

	// Initialize the shared memory
	s_buffer = new bx::SharedBuffer();
	if (!s_buffer->init("MayaToMax", sizeof(SharedData)))
	{
		MGlobal::displayError("Failed to sync shared memory");
		return status;
	}

	// Create shared data
	s_shared = new SharedData();

	// Add timer callback
	s_callbackIdArray.append(MTimerMessage::addTimerCallback(
		0.03f,
		callbackTimer,
		NULL,
		&status
	));
	
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

	// Make sure we signal that the session is over.
	s_processed = false;
	write();

	// Remove the callback timer for update
	MMessage::removeCallbacks(s_callbackIdArray);

	// Delete shared data
	delete s_shared;
	s_shared = NULL;

	// Shutdown the shared memory 
	s_buffer->shutdown();
	delete s_buffer;
	s_buffer = NULL;

	// 
	MFnPlugin plugin(obj);

	MGlobal::displayInfo(MString("Max Sync plugin unloaded!"));
	return status;
}