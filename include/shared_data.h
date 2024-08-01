#pragma once

#include <memory> // @todo bx/uint32_t

///
#ifndef SHARED_DATA_CONFIG_MAX_VERTICES
	#define SHARED_DATA_CONFIG_MAX_VERTICES 400
#endif

///
#ifndef SHARED_DATA_CONFIG_MAX_INDICES
	#define SHARED_DATA_CONFIG_MAX_INDICES 400
#endif

/// All data that is being updated.
///
struct SharedData
{
	bool m_processed;

	//
	struct SyncEvent
	{
		bool m_isSynced;
	} m_sync;

	//
	struct CameraEvent
	{
		float m_view[16];
		float m_proj[16];
	} m_camera;

	// If name doesnt exist in scene, add it. If it does exist, change it.
	//
	struct MeshEvent
	{
		char m_name[1024];
		bool m_changed;

		// Vertex layout: position, normal, uv
		float m_vertices[SHARED_DATA_CONFIG_MAX_VERTICES][3+3+2];
		uint32_t m_numVertices;

		uint16_t m_indices[SHARED_DATA_CONFIG_MAX_INDICES];
		uint32_t m_numIndices;

	} m_meshChanged;

	struct TransformEvent
	{
		char m_name[1024];
		bool m_changed;

		float m_pos[3];
		float m_rotation[4];
		float m_scale[3];

	} m_transformChanged;
};