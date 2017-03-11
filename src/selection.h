/**
    Copyright (c) 2017 Ryan Porter    
    You may use, distribute, or modify this code under the terms of the MIT license.
*/

#ifndef POLY_SYMMETRY_SELECTION_H
#define POLY_SYMMETRY_SELECTION_H

#include "meshData.h"

#include <vector>
#include <utility>

#include <maya/MDagPath.h>
#include <maya/MFn.h>
#include <maya/MSelectionList.h>

using namespace std;

/*
    User selection of the symmetrical components on a shell, with a 
    stand-alone vertex on the left side of the shell. 
    
    The first edge index is connected to the first face index. 
    The first vertex index is connected to the first edge index.
*/
struct ComponentSelection
{
    pair<int, int> edgeIndices;
    pair<int, int> faceIndices;
    pair<int, int> vertexIndices;

    int leftVertexIndex = -1;

    ComponentSelection() {}
};

void            getSelectedComponents(MDagPath &selectedMesh, MSelectionList &activeSelection, MSelectionList &selection, MFn::Type componentType);
void            getSelectedComponentIndices(MSelectionList &activeSelection,  vector<int> &indices, MFn::Type componentType);
bool            getSymmetricalComponentSelection(MeshData &meshData, MSelectionList &selection,  ComponentSelection &componentSelection, bool leftSideVertexSelected);
void            getAllVertices(int &numberOfVertices, MObject &components);

#endif