#include "pch.h"
#include "Object.h"

Object::Object()
{
	objectInfo = make_unique<Protocol::ObjectInfo>();
	axial = objectInfo->mutable_axial();
}

Object::~Object()
{
}
