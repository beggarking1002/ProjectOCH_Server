#include "pch.h"
#include "Object.h"

Object::Object()
{
	objectInfo = new Protocol::ObjectInfo();
	axial = new Protocol::AxialCoord();
	objectInfo->set_allocated_axial(axial);
}

Object::~Object()
{
	delete objectInfo;
}
