#pragma once

#include <memory> // @todo bx/uint32_t

///
#define SHARED_DATA_NUM_FLOAT_PER_VERTEX 3 + 3 + 2

#define SHARED_DATA_MB(x) ((x) * 1024 * 1024)

///
#ifndef SHARED_DATA_CONFIG_MAX_VERTICES
	#define SHARED_DATA_CONFIG_MAX_VERTICES SHARED_DATA_MB(4) / (sizeof(float) * SHARED_DATA_NUM_FLOAT_PER_VERTEX)
#endif

///
#ifndef SHARED_DATA_CONFIG_MAX_INDICES
	#define SHARED_DATA_CONFIG_MAX_INDICES SHARED_DATA_MB(4) / sizeof(uint32_t)
#endif

///
#define SHARED_DATA_MESSAGE_NONE         UINT32_C(0x00000000)  
#define SHARED_DATA_MESSAGE_RECEIVED     UINT32_C(0x00000001)  
#define SHARED_DATA_MESSAGE_RELOAD_SCENE UINT32_C(0x00000002)  

/// All data that is being updated.
///
struct SharedData
{
	//
	struct CameraUpdate
	{
		float m_view[16];
		float m_proj[16];
	} m_camera;

	struct MeshEvent
	{
		char m_name[1024]; //!< Entity name (This creates the entity.)
		bool m_changed;

		// Vertex layout: position, normal, uv
		float m_vertices[SHARED_DATA_CONFIG_MAX_VERTICES][SHARED_DATA_NUM_FLOAT_PER_VERTEX];
		uint32_t m_numVertices;

		uint32_t m_indices[SHARED_DATA_CONFIG_MAX_INDICES];
		uint32_t m_numIndices;

	} m_meshChanged;

	struct MaterialEvent
	{
		char m_name[1024]; //!< Entity name (This expects entity to be created.)
		bool m_changed;

		char m_colorPath[1024];
		char m_normalPath[1024];

	} m_materialChanged;

	struct TransformEvent
	{
		char m_name[1024]; //!< Entity name (This expects entity to be created.)
		bool m_changed;

		float m_pos[3];
		float m_rotation[4];
		float m_scale[3];

	} m_transformChanged;
};