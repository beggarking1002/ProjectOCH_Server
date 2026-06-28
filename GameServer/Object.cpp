#include "pch.h"
#include "Object.h"

Object::Object()
{
	objectInfo = make_unique<Protocol::ObjectInfo>();
	position = objectInfo->mutable_position();
}

Object::~Object()
{
}
