#pragma once

#define NT_PLUGIN
#define REQUIRE_IOSTREAM
#define EXPORT __declspec(dllexport)

#include <maya/MFloatMatrix.h>
#include <maya/MItMeshVertex.h>
#include <maya/MItMeshFaceVertex.h>
#include <maya/MPxSurfaceShape.h>
#include <maya/MPlugArray.h>
#include <maya/MFnPlugin.h>
#include <maya/MFnMesh.h>
#include <maya/MFnMeshData.h>
#include <maya/MFnTransform.h>
#include <maya/MFnCamera.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MItDependencyGraph.h>
#include <maya/MFnLambertShader.h>
#include <maya/MFnBlinnShader.h>
#include <maya/MFnPhongShader.h>
#include <maya/MFnPointLight.h>
#include "maya/MFnStandardSurfaceShader.h">
#include <maya/MFnAmbientLight.h>
#include <maya/MFnDirectionalLight.h>
#include <maya/MFnSpotLight.h>
#include <maya/MTimer.h>
#include <maya/MImage.h>
#include <maya/MFloatPointArray.h>
#include <maya/MPointArray.h>
#include <maya/MIntArray.h>
#include <maya/MPoint.h>
#include <maya/MMatrix.h>
#include <maya/MEulerRotation.h>
#include <maya/MVector.h>
#include <maya/MQuaternion.h>
#include <maya/MItDag.h>
#include <maya/M3dView.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MPlugArray.h>
#include <maya/MSelectionList.h>
#include <maya/MItDependencyNodes.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MPx3dModelView.h>
#include <maya/MFnSet.h>

// Wrappers
#include <maya/MGlobal.h>
#include <maya/MCallbackIdArray.h>

// Messages
#include <maya/MMessage.h>
#include <maya/MTimerMessage.h>
#include <maya/MDGMessage.h>
#include <maya/MEventMessage.h>
#include <maya/MPolyMessage.h>
#include <maya/MNodeMessage.h>
#include <maya/MDagPath.h>
#include <maya/MDagMessage.h>
#include <maya/MUiMessage.h>
#include <maya/MModelMessage.h>
#include <maya/MSceneMessage.h>
#include <maya/MUserEventMessage.h>
#include <maya/MPxCommand.h>
#include <maya/MStreamUtils.h>

// Libraries to link from Maya
// This can be also done in the properties setting for the project.
#pragma comment(lib,"Foundation.lib")
#pragma comment(lib,"OpenMaya.lib")
#pragma comment(lib,"OpenMayaUI.lib")