/**
    Copyright (c) 2017 Ryan Porter    
    You may use, distribute, or modify this code under the terms of the MIT license.
*/

#include "meshData.h"
#include "polySymmetryCmd.h"
#include "polySymmetryNode.h"
#include "sceneCache.h"
#include "selection.h"

#include <sstream>
#include <vector>

#include <maya/MArgList.h>
#include <maya/MArgDatabase.h>
#include <maya/MDGModifier.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MPlug.h>
#include <maya/MSelectionList.h>
#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MSyntax.h>

using namespace std;

#define RETURN_IF_ERROR(s) if (!s) { return s; }

PolySymmetryCommand::PolySymmetryCommand() 
{
    meshSymmetryNode = MObject();

    meshData = MeshData();
    meshSymmetryData = PolySymmetryData();

    symmetryComponents = vector<ComponentSelection>();
    leftSideVertexIndices = vector<int>();
}


PolySymmetryCommand::~PolySymmetryCommand() 
{
    meshData.clear();
    symmetryComponents.clear();
    leftSideVertexIndices.clear();
}


void* PolySymmetryCommand::creator()
{
    return new PolySymmetryCommand();
}


MSyntax PolySymmetryCommand::getSyntax()
{
    MSyntax syntax;

    syntax.useSelectionAsDefault(true);    
    syntax.setObjectType(MSyntax::kSelectionList, 1, 1);
    syntax.enableQuery(true);

    syntax.addFlag(
        SYMMETRY_COMPONENTS_FLAG,
        SYMMETRY_COMPONENTS_LONG_FLAG,
        MSyntax::MArgType::kString,
        MSyntax::MArgType::kString,
        MSyntax::MArgType::kString,
        MSyntax::MArgType::kString,
        MSyntax::MArgType::kString,
        MSyntax::MArgType::kString
    );

    syntax.addFlag(
        LEFT_SIDE_VERTEX_FLAG,
        LEFT_SIDE_VERTEX_LONG_FLAG,
        MSyntax::MArgType::kString
    );

    syntax.addFlag(
        CONSTRUCTION_HISTORY_FLAG,
        CONSTRUCTION_HISTORY_LONG_FLAG,
        MSyntax::MArgType::kBoolean
    );

    syntax.addFlag(
        EXISTS_FLAG,
        EXISTS_LONG_FLAG,
        MSyntax::MArgType::kBoolean
    );

    syntax.makeFlagMultiUse(SYMMETRY_COMPONENTS_FLAG);
    syntax.makeFlagMultiUse(LEFT_SIDE_VERTEX_FLAG);

    return syntax;
}


MStatus PolySymmetryCommand::doIt(const MArgList& argList)
{
    MStatus status;

    MArgDatabase argsData(syntax(), argList, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    if (argsData.isQuery())
    {
        status = this->parseQueryArguments(argsData);
        RETURN_IF_ERROR(status);
    } else {
        status = this->parseArguments(argsData);        
        RETURN_IF_ERROR(status);

        meshSymmetryData.initialize(selectedMesh);
    }

    return this->redoIt();
}


MStatus PolySymmetryCommand::redoIt()
{
    MStatus status;

    if (this->isQuery)
    {
        if (this->selectedMesh.isValid())
        {            
            status = this->doQueryMeshAction();
        } else {
            status = this->doQueryDataAction();
        }
    } else {
        status = this->doUndoableCommand();
    }

    return status;
}


MStatus PolySymmetryCommand::doQueryDataAction()
{
    MStatus status = this->getSymmetricalComponentsFromNode();
    CHECK_MSTATUS_AND_RETURN_IT(status);

    this->createResultString();

    return MStatus::kSuccess;
}


MStatus PolySymmetryCommand::doQueryMeshAction()
{
    bool cacheHit = PolySymmetryCache::getNodeFromCache(this->selectedMesh, this->meshSymmetryNode);

    if (this->isQueryExists)
    {
        this->setResult(cacheHit);
    } else {
        if (cacheHit)
        {
            MString resultName = MFnDependencyNode(this->meshSymmetryNode).name();
            this->setResult(resultName);
        } else {
            MString warningMsg("No polySymmetryData node in memory matches the mesh ^1s.");
            warningMsg.format(warningMsg, this->selectedMesh.partialPathName());

            MGlobal::displayWarning(warningMsg);
        }
    }

    return MStatus::kSuccess;
}


MStatus PolySymmetryCommand::doUndoableCommand()
{
    MStatus status;

    status = this->getSymmetricalComponentsFromScene();
    CHECK_MSTATUS_AND_RETURN_IT(status);

    if (constructionHistory)
    {
        status = this->createResultNode();
        CHECK_MSTATUS_AND_RETURN_IT(status);
        
    } else {
        this->createResultString();
    }

    return MStatus::kSuccess;
}


MStatus PolySymmetryCommand::undoIt()
{
    MStatus status;

    MDGModifier dgModifier;

    dgModifier.deleteNode(this->meshSymmetryNode);

    status = dgModifier.doIt();
    CHECK_MSTATUS_AND_RETURN_IT(status);

    return MStatus::kSuccess;
}


const bool PolySymmetryCommand::isUndoable()
{
    return !this->meshSymmetryNode.isNull();
}


MStatus PolySymmetryCommand::parseQueryArguments(MArgDatabase &argsData)
{
    MStatus status;
        
    this->isQuery = true;
    this->isQueryExists = argsData.isFlagSet(EXISTS_FLAG);

    MSelectionList selection;    
    status = argsData.getObjects(selection);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MObject obj;
    status = selection.getDependNode(0, obj);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MString objectType = MFnDependencyNode(obj).typeName();

    if (objectType == PolySymmetryNode::NODE_NAME)
    {
        this->meshSymmetryNode = obj;
        return MStatus::kSuccess;
    }
    
    MDagPath dagPath;
    status = selection.getDagPath(0, dagPath);    

    if (status && dagPath.hasFn(MFn::kMesh))
    {
        this->selectedMesh.set(dagPath);
        return MStatus::kSuccess;
    }

    MString errorMsg("polySymmetry command requires a mesh or ^1s in query node, not a(n) ^2s");
    errorMsg.format(errorMsg, PolySymmetryNode::NODE_NAME, objectType);
    MGlobal::displayError(errorMsg);

    return MStatus::kFailure;
}


MStatus PolySymmetryCommand::parseArguments(MArgDatabase &argsData)
{
    MStatus status;

    bool constructionHistoryFlagSet = argsData.isFlagSet(CONSTRUCTION_HISTORY_FLAG, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    if (constructionHistoryFlagSet)
    {
        status = argsData.getFlagArgument(CONSTRUCTION_HISTORY_FLAG, 0, this->constructionHistory);
        CHECK_MSTATUS_AND_RETURN_IT(status);
    } else {
        constructionHistory = true;
    }

    status = this->getSelectedMesh(argsData);
    RETURN_IF_ERROR(status);

    status = this->getSymmetryComponents(argsData);
    RETURN_IF_ERROR(status);

    status = this->getLeftSideVertexIndices(argsData);
    RETURN_IF_ERROR(status);

    return MStatus::kSuccess;
}


MStatus PolySymmetryCommand::getSelectedMesh(MArgDatabase &argsData)
{
    MStatus status;
    MSelectionList selection;

    status = argsData.getObjects(selection);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = selection.getDagPath(0, selectedMesh);

    if (!status || !selectedMesh.hasFn(MFn::Type::kMesh))
    {
        MGlobal::displayError("Must select a mesh.");
        return MStatus::kFailure;
    }

    meshData.unpackMesh(selectedMesh);

    if (selectedMesh.node().hasFn(MFn::kMesh)) 
    {
        selectedMesh.pop();
    }

    return MStatus::kSuccess;
}


MStatus PolySymmetryCommand::getSymmetryComponents(MArgDatabase &argsData)
{
    MStatus status;

    uint numberOfSymmetricalComponentLists = argsData.numberOfFlagUses(SYMMETRY_COMPONENTS_FLAG);

    for (uint i = 0; i < numberOfSymmetricalComponentLists; i++)
    {
        MArgList args;
        MSelectionList selection;

        status = argsData.getFlagArgumentList(SYMMETRY_COMPONENTS_FLAG, i, args);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        status = this->getFlagStringArguments(args, selection);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        ComponentSelection scs;        
        MSelectionList filteredSelection;

        getSelectedComponents(selectedMesh, selection, filteredSelection, MFn::Type::kMeshEdgeComponent);
        getSelectedComponents(selectedMesh, selection, filteredSelection, MFn::Type::kMeshVertComponent);
        getSelectedComponents(selectedMesh, selection, filteredSelection, MFn::Type::kMeshPolygonComponent);

        bool isValidSelection = getSymmetricalComponentSelection(meshData, filteredSelection, scs, false);

        if (isValidSelection)
        {
            symmetryComponents.push_back(scs);
        } else {
            MGlobal::displayError("Must select a symmetrical edge, face, and vertex on both sides of the mesh.");
            return MStatus::kFailure;
        }
    }

    if (symmetryComponents.empty())
    {
        MGlobal::displayError("Must specify at least one pair of symmetrical components.");
        return MStatus::kFailure;
    }

    return MStatus::kSuccess;
}


void PolySymmetryCommand::setSymmetryComponents(vector<ComponentSelection> &components)
{
    for (ComponentSelection &cs : components)
    {
        this->symmetryComponents.push_back(cs);
    }
}


MStatus PolySymmetryCommand::getLeftSideVertexIndices(MArgDatabase &argsData)
{
    MStatus status;

    MSelectionList selection;
    uint numberOfFlagUses = argsData.numberOfFlagUses(LEFT_SIDE_VERTEX_FLAG);

    for (uint i = 0; i < numberOfFlagUses; i++)
    {
        MArgList args;
        status = argsData.getFlagArgumentList(LEFT_SIDE_VERTEX_FLAG, i, args);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        status = this->getFlagStringArguments(args, selection);
    }

    getSelectedComponentIndices(selection, leftSideVertexIndices, MFn::kMeshVertComponent);

    if (leftSideVertexIndices.empty())
    {
        MGlobal::displayError("Must specify at least one vertex on the left side of the mesh.");
        return MStatus::kFailure;
    }

    return MStatus::kSuccess;
}


void PolySymmetryCommand::setLeftSideVertexIndices(vector<int> &indices)
{
    for (int &i : indices)
    {
        this->leftSideVertexIndices.push_back(i);
    }    
}


MStatus PolySymmetryCommand::getFlagStringArguments(MArgList &args, MSelectionList &selection)
{
    MStatus status;
    MString mesh = this->selectedMesh.partialPathName();

    for (uint argIndex = 0; argIndex < args.length(); argIndex++)
    {
        MString obj = args.asString(argIndex, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);           
        
        if (obj.index('.') == -1) 
        {
            obj = mesh + "." + obj;
        }

        status = selection.add(obj);

        if (!status) 
        { 
            MGlobal::displayError("No object matches name: " + obj);
            return status;
        }
    }

    return MStatus::kSuccess;
}


MStatus PolySymmetryCommand::getSymmetricalComponentsFromNode()
{
    MStatus status;
    MFnDependencyNode fnNode(this->meshSymmetryNode);

    status = PolySymmetryNode::getValues(fnNode, EDGE_SYMMETRY, this->meshSymmetryData.edgeSymmetryIndices);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = PolySymmetryNode::getValues(fnNode, FACE_SYMMETRY, this->meshSymmetryData.faceSymmetryIndices);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = PolySymmetryNode::getValues(fnNode, VERTEX_SYMMETRY, this->meshSymmetryData.vertexSymmetryIndices);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = PolySymmetryNode::getValues(fnNode, EDGE_SIDES, this->meshSymmetryData.edgeSides);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = PolySymmetryNode::getValues(fnNode, FACE_SIDES, this->meshSymmetryData.faceSides);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = PolySymmetryNode::getValues(fnNode, VERTEX_SIDES, this->meshSymmetryData.vertexSides);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    return MStatus::kSuccess;
}


MStatus PolySymmetryCommand::getSymmetricalComponentsFromScene()
{
    MStatus status;

    for (ComponentSelection &s : symmetryComponents)
    {
        this->meshSymmetryData.findSymmetricalVertices(s);
    }

    this->meshSymmetryData.findVertexSides(leftSideVertexIndices);
    this->meshSymmetryData.finalizeSymmetry();

    return MStatus::kSuccess;
}


MStatus PolySymmetryCommand::createResultNode()
{
    MDGModifier dgModifier;

    MStatus status;
    meshSymmetryNode = dgModifier.createNode(PolySymmetryNode::NODE_NAME, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    PolySymmetryCache::cacheNodes = false;
    status = dgModifier.doIt();
    PolySymmetryCache::cacheNodes = true;

    CHECK_MSTATUS_AND_RETURN_IT(status);

    MDGModifier renameModifier;
    status = renameModifier.renameNode(meshSymmetryNode, selectedMesh.partialPathName() + "Symmetry");
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MFnDependencyNode fnNode(meshSymmetryNode);

    status = PolySymmetryNode::setValues(fnNode, EDGE_SYMMETRY, this->meshSymmetryData.edgeSymmetryIndices);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = PolySymmetryNode::setValues(fnNode, FACE_SYMMETRY, this->meshSymmetryData.faceSymmetryIndices);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = PolySymmetryNode::setValues(fnNode, VERTEX_SYMMETRY, this->meshSymmetryData.vertexSymmetryIndices);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = PolySymmetryNode::setValues(fnNode, EDGE_SIDES, this->meshSymmetryData.edgeSides);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = PolySymmetryNode::setValues(fnNode, FACE_SIDES, this->meshSymmetryData.faceSides);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = PolySymmetryNode::setValues(fnNode, VERTEX_SIDES, this->meshSymmetryData.vertexSides);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = PolySymmetryNode::setValue(fnNode, NUMBER_OF_EDGES, this->meshData.numberOfEdges);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = PolySymmetryNode::setValue(fnNode, NUMBER_OF_FACES, this->meshData.numberOfFaces);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = PolySymmetryNode::setValue(fnNode, NUMBER_OF_VERTICES, this->meshData.numberOfVertices);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    int vertexChecksum = (int) MeshData::getVertexChecksum(this->selectedMesh);
    status = PolySymmetryNode::setValue(fnNode, VERTEX_CHECKSUM, vertexChecksum);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    this->setResult(fnNode.name());

    PolySymmetryCache::addNodeToCache(meshSymmetryNode);

    return MStatus::kSuccess;
}


MStatus PolySymmetryCommand::createResultString()
{
    stringstream output;
    output << "{";

        output << "\"e\": {";
            setJSONData("symmetry", output, this->meshSymmetryData.edgeSymmetryIndices);
            setJSONData("whichSide", output, this->meshSymmetryData.edgeSides, true);
        output << "}, ";

        output << "\"f\": {";
            setJSONData("symmetry", output, this->meshSymmetryData.faceSymmetryIndices);
            setJSONData("whichSide", output, this->meshSymmetryData.faceSides, true);
        output << "}, ";

        output << "\"vtx\": {";
            setJSONData("symmetry", output, this->meshSymmetryData.vertexSymmetryIndices);
            setJSONData("whichSide", output, this->meshSymmetryData.vertexSides, true);
        output << "}";

    output << "}";

    this->setResult(output.str().c_str());

    return MStatus::kSuccess;
}


void PolySymmetryCommand::setJSONData(const char* key, stringstream &output, vector<int> &data, bool isLast)
{
    int numberOfItems = (int) data.size();

    output << " \"" << key << "\"" << ": ";

    output << '[';

    if (numberOfItems > 0) 
    {    
        for (int i = 0; i < numberOfItems - 1; i++)
        {
            output << data[i] << ", ";
        }

        output << data[numberOfItems - 1];
    }

    output << ']';

    if (!isLast)
    {
        output << ", ";
    }
}


MStatus PolySymmetryCommand::finalize()
{
    MArgList command;

    command.addArg(commandString());

    for (ComponentSelection &cs : symmetryComponents)
    {
        command.addArg(SYMMETRY_COMPONENTS_FLAG);

        command.addArg(MString("e[^1s]").format(MString() + cs.edgeIndices.first));
        command.addArg(MString("e[^1s]").format(MString() + cs.edgeIndices.second));

        command.addArg(MString("f[^1s]").format(MString() + cs.faceIndices.first));
        command.addArg(MString("f[^1s]").format(MString() + cs.faceIndices.second));

        command.addArg(MString("vtx[^1s]").format(MString() + cs.vertexIndices.first));
        command.addArg(MString("vtx[^1s]").format(MString() + cs.vertexIndices.second));
    }

    for (int &i : leftSideVertexIndices)
    {
        command.addArg(LEFT_SIDE_VERTEX_FLAG); 
        command.addArg(MString("vtx[^1s]").format(MString() + i));
    }

    command.addArg(this->selectedMesh.partialPathName());

    return MPxToolCommand::doFinalize( command );
}